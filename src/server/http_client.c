#include "cache.h"
#include "server.h"
#include <stddef.h> // Added for offsetof

// container_of macro to get the pointer to the containing struct
#define container_of(ptr, type, member) ((type*) ((char*) (ptr) - offsetof(type, member)))

typedef struct {
  char** urls;
  size_t count;
} server_list_t;

typedef struct pending_request {
  single_request_t*       request;
  struct pending_request* next;
  struct pending_request* same_requests;
} pending_request_t;

static pending_request_t* pending_requests = NULL;
static CURLM*             multi_handle;
static mc_t*              memcache_client;
const char*               CURL_METHODS[] = {"GET", "POST", "PUT", "DELETE"};

static server_list_t eth_rpc_servers    = {0};
static server_list_t beacon_api_servers = {0};

static void cache_response(single_request_t* r);
static void trigger_uncached_curl_request(void* data, char* value, size_t value_len);

// Context structure to associate uv_poll_t with CURL easy handle
typedef struct {
  uv_poll_t poll_handle;
  CURL*     easy_handle;
  bool      is_done_processing; // Flag set by handle_curl_events
} curl_poll_context_t;

// Custom close callback for uv_poll_t handles
static void cleanup_easy_handle_and_context(uv_handle_t* handle) {
  if (!handle) return;
  // Use container_of to get the context struct from the poll_handle pointer
  curl_poll_context_t* context = container_of(handle, curl_poll_context_t, poll_handle);
  if (context) {
    // Only cleanup easy handle if CURLMSG_DONE was processed
    if (context->is_done_processing && context->easy_handle) {
      fprintf(stderr, "DEBUG: Cleaning up easy handle %p in uv_close callback\n", context->easy_handle);
      curl_easy_cleanup(context->easy_handle);
      context->easy_handle = NULL; // Prevent double free if called unexpectedly
    }
    else if (!context->is_done_processing && context->easy_handle) {
      fprintf(stderr, "WARNING: uv_close callback invoked for easy handle %p BEFORE CURLMSG_DONE was processed. Easy handle NOT cleaned up here.\n", context->easy_handle);
      // This might indicate a potential leak if CURLMSG_DONE never arrives for this handle.
    }
    // Free the context struct itself
    fprintf(stderr, "DEBUG: Freeing poll context %p (handle %p)\n", context, handle);
    free(context);
  }
  else {
    fprintf(stderr, "WARNING: cleanup_easy_handle_and_context called with handle not part of a context?\n");
  }
}

static pending_request_t* pending_find(single_request_t* req) {
  pending_request_t* current = pending_requests;
  while (current) {
    if (current->request == req) return current;
    current = current->next;
  }
  return NULL;
}

static inline bool pending_request_matches(data_request_t* in, data_request_t* pending) {
  if (in->type != pending->type || in->encoding != pending->encoding || in->method != pending->method) return false;
  if ((in->url == NULL) != (pending->url != NULL)) return false;
  if (in->url && strcmp(in->url, pending->url) != 0) return false;
  if (in->payload.len != pending->payload.len) return false;
  if (in->payload.len && memcmp(in->payload.data, pending->payload.data, in->payload.len) != 0) return false;
  return true;
}

static pending_request_t* pending_find_matching(single_request_t* req) {
  data_request_t*    in      = req->req;
  pending_request_t* current = pending_requests;
  while (current) {
    if (pending_request_matches(in, current->request->req)) return current;
    current = current->next;
  }
  return NULL;
}

static void pending_add(single_request_t* req) {
  pending_request_t* new_request = (pending_request_t*) calloc(1, sizeof(pending_request_t));
  new_request->request           = req;
  new_request->next              = pending_requests;
  pending_requests               = new_request;
}
static void pending_add_to_same_requests(pending_request_t* pending, single_request_t* req) {
  pending_request_t* new_request = (pending_request_t*) calloc(1, sizeof(pending_request_t));
  new_request->request           = req;
  new_request->next              = pending->same_requests;
  pending->same_requests         = new_request;
}

static void pending_remove(single_request_t* req) {
  pending_request_t* current = pending_requests;
  pending_request_t* prev    = NULL;
  while (current) {
    if (current->request == req) {
      if (prev)
        prev->next = current->next;
      else
        pending_requests = current->next;
      free(current);
      return;
    }
    prev    = current;
    current = current->next;
  }
}
static void call_callback_if_done(request_t* req) {
  for (size_t i = 0; i < req->request_count; i++) {
    if (c4_state_is_pending(req->requests[i].req)) return;
  }
  req->cb(req);
}
// Prüft abgeschlossene Übertragungen
static void handle_curl_events() {
  if (!multi_handle) {
    return;
  }

  CURLMsg* msg;
  int      msgs_left;
  while ((msg = curl_multi_info_read(multi_handle, &msgs_left))) {
    if (!msg || msg->msg != CURLMSG_DONE) continue;

    CURL* easy = msg->easy_handle;
    if (!easy) continue;

    single_request_t*  r       = NULL;
    CURLcode           res     = curl_easy_getinfo(easy, CURLINFO_PRIVATE, &r);
    pending_request_t* pending = pending_find(r);

    if (msg->data.result == CURLE_OK) {
      printf("   -> [%p] %s : %d bytes\n", easy, r->req->url, r->buffer.data.len);
      r->req->response = r->buffer.data; // set the response
      cache_response(r);                 // and write to cache
      r->buffer = (buffer_t) {0};        // reset the buffer, so we don't clean up the data
    }
    else {
      long  http_code     = 0;
      char* effective_url = NULL;
      curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &http_code);
      curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &effective_url);
      r->req->error = bprintf(NULL, "(%d) %s : %s", (uint32_t) http_code, curl_easy_strerror(res), bprintf(&r->buffer, " ")); // create error message
      printf("   -> [%p] %s : ERROR = %s (http code: %d)\n",
             // and log
             easy, effective_url ? effective_url : (r->url ? r->url : r->req->url),
             curl_easy_strerror(res), (uint32_t) http_code);
    }

    // Process any waiting requests
    if (pending) {
      pending_request_t* same = pending->same_requests;
      pending_remove(r);
      while (same) {
        pending_request_t* next = same->next;
        if (same->request)
          trigger_uncached_curl_request(same->request, r->req->response.data ? (char*) r->req->response.data : NULL, r->req->response.len);
        free(same);
        same = next;
      }
    }

    r->curl = NULL; // setting it to NULL marks it as done

    // Clean up the easy handle - DEFER THIS to cleanup_context
    curl_multi_remove_handle(multi_handle, easy);

    // Try to find the socket and context to initiate cleanup - REMOVED this block
    // RE-ADD logic to mark context as done
    curl_socket_t sockfd = -1;
    curl_easy_getinfo(easy, CURLINFO_ACTIVESOCKET, &sockfd);
    if (sockfd != -1) {
      void* socketp = NULL;
      curl_multi_assign(multi_handle, sockfd, &socketp);
      curl_poll_context_t* context = (curl_poll_context_t*) socketp;
      if (context) {
        fprintf(stderr, "DEBUG: Marking context %p (easy %p) as done processing.\n", context, easy);
        context->is_done_processing = true;
        // Check if uv_close was already called by CURL_POLL_REMOVE
        if (context->poll_handle.data == NULL) {
          fprintf(stderr, "DEBUG: Poll handle %p already closing for easy %p, cleanup deferred to uv_close callback.\n", &context->poll_handle, easy);
        }
      }
      else {
        // This might happen if socket closes extremely quickly before CURL_POLL_REMOVE runs?
        fprintf(stderr, "WARNING: Could not find context for socket %d (easy %p) in CURLMSG_DONE. is_done_processing not set! Potential leak if CURL_POLL_REMOVE doesn't run.\n", (int) sockfd, easy);
      }
    }
    else {
      fprintf(stderr, "WARNING: CURLINFO_ACTIVESOCKET failed for completed handle %p in CURLMSG_DONE. Cannot mark context. Potential leak if CURL_POLL_REMOVE doesn't run.\n", easy);
    }

    // continuue with the callback
    call_callback_if_done(r->parent);
  }
}

// Poll-Callback für Socket-Ereignisse
static void poll_cb(uv_poll_t* handle, int status, int events) {
  // Check if handle has been marked as closing/invalid via its data field
  if (!handle || !handle->data) {
    // fprintf(stderr, "poll_cb: ignoring call on closing/invalid handle %p\n", handle);
    return;
  }
  // Retrieve context if needed (though not strictly necessary for current logic)
  // curl_poll_context_t* context = (curl_poll_context_t*) handle->data;

  // Check if there was an error
  if (status < 0) {
    fprintf(stderr, "Socket poll error: %s\n", uv_strerror(status));
    return;
  }

  int flags = 0;
  if (events & UV_READABLE) flags |= CURL_CSELECT_IN;
  if (events & UV_WRITABLE) flags |= CURL_CSELECT_OUT;

  int           running_handles;
  curl_socket_t socket = handle->io_watcher.fd;

  // Make sure the socket is valid
  if (socket < 0) {
    return;
  }

  CURLMcode rc = curl_multi_socket_action(multi_handle, socket, flags, &running_handles);
  if (rc != CURLM_OK) {
    fprintf(stderr, "curl_multi_socket_action error: %s\n", curl_multi_strerror(rc));
  }

  handle_curl_events();
}

static void timer_cb(uv_timer_t* handle) {
  // Check if handle is valid
  if (!handle) {
    return;
  }

  int       running_handles;
  CURLMcode rc = curl_multi_socket_action(multi_handle, CURL_SOCKET_TIMEOUT, 0, &running_handles);
  if (rc != CURLM_OK) {
    fprintf(stderr, "curl_multi_socket_action error in timer: %s\n", curl_multi_strerror(rc));
  }

  handle_curl_events();
}

// Timer-Callback für curl
static int timer_callback(CURLM* multi, long timeout_ms, void* userp) {
  uv_timer_t* timer = (uv_timer_t*) userp;
  if (timeout_ms >= 0) {
    uv_timer_start(timer, timer_cb, timeout_ms, 0);
  }
  return 0;
}

// Socket-Callback für curl
static int socket_callback(CURL* easy, curl_socket_t s, int what, void* userp, void* socketp) {
  // curl_poll_context_t* context = (curl_poll_context_t*) socketp;
  curl_poll_context_t* context = (curl_poll_context_t*) socketp;

  // Handle remove case first
  if (what == CURL_POLL_REMOVE) {
    // if (context) {
    if (context) {
      // Check if the handle is not already being closed (by CURLMSG_DONE path)
      // if (context->poll_handle.data != NULL) {
      if (context->poll_handle.data != NULL) {
        // fprintf(stderr, "DEBUG: Closing poll handle %p via CURL_POLL_REMOVE for socket %d\n", &context->poll_handle, (int) s);
        // fprintf(stderr, "DEBUG: Closing poll handle %p via CURL_POLL_REMOVE for socket %d\n", poll, (int) s);
        fprintf(stderr, "DEBUG: Initiating close for poll handle %p via CURL_POLL_REMOVE for socket %d\n", &context->poll_handle, (int) s);
        // Stop polling before closing
        // uv_poll_stop(&context->poll_handle);
        uv_poll_stop(&context->poll_handle);

        // Mark the poll handle as closing/invalid by setting data to NULL
        // context->poll_handle.data = NULL;
        context->poll_handle.data = NULL;

        // Close the handle using our custom callback for cleanup
        // uv_close((uv_handle_t*) &context->poll_handle, cleanup_context);
        // Use the simpler callback
        // uv_close((uv_handle_t*) &context->poll_handle, free_poll_context);
        // Use standard free
        uv_close((uv_handle_t*) &context->poll_handle, cleanup_easy_handle_and_context);

        // Clear the socket association in curl
        curl_multi_assign(multi_handle, s, NULL);
      }
      else {
        // fprintf(stderr, "DEBUG: Ignoring CURL_POLL_REMOVE for socket %d, context %p (already closing)\n", (int) s, poll);
        // Keep this debug message as it might be relevant if cleanup issues persist
        fprintf(stderr, "DEBUG: Ignoring CURL_POLL_REMOVE for socket %d, poll %p (already closing)\n", (int) s, context);
      }
    }
    return 0;
  }

  // If we don't have a context/poll handle yet, create one
  // if (!context) {
  if (!context) {
    // context = (curl_poll_context_t*) calloc(1, sizeof(curl_poll_context_t));
    // poll = (uv_poll_t*) calloc(1, sizeof(uv_poll_t));
    context = (curl_poll_context_t*) calloc(1, sizeof(curl_poll_context_t));
    // if (!context) {
    if (!context) {
      fprintf(stderr, "Failed to allocate poll context\n");
      return -1; // Return error (as per libcurl docs for socket_callback)
    }
    // context->easy_handle = easy; // Store the easy handle - REMOVED
    context->easy_handle        = easy;
    context->is_done_processing = false;

    // Initialize the poll handle
    // int err = uv_poll_init_socket(uv_default_loop(), &context->poll_handle, s);
    // int err = uv_poll_init_socket(uv_default_loop(), poll, s);
    int err = uv_poll_init_socket(uv_default_loop(), &context->poll_handle, s);
    if (err != 0) {
      fprintf(stderr, "Failed to initialize poll handle: %s\n", uv_strerror(err));
      // free(context);
      free(context);
      return -1; // Return error
    }
    // Store the context pointer in the handle's data field for poll_cb check
    // context->poll_handle.data = context;
    // Store the poll pointer in its own data field for the active check
    // poll->data = poll;
    context->poll_handle.data = context;
  }

  // Determine which events to monitor
  int events = 0;
  if (what & CURL_POLL_IN) events |= UV_READABLE;
  if (what & CURL_POLL_OUT) events |= UV_WRITABLE;

  // Start polling for events
  // int err = uv_poll_start(&context->poll_handle, events, poll_cb);
  int err = uv_poll_start(&context->poll_handle, events, poll_cb);
  if (err != 0) {
    fprintf(stderr, "Failed to start polling: %s\n", uv_strerror(err));
    if (!socketp) {
      // Only cleanup if we just created it and failed to start
      // Need to properly close the handle before freeing context
      // context->poll_handle.data = NULL; // Mark invalid
      // poll->data = NULL; // Mark invalid
      context->poll_handle.data = NULL;
      // uv_close((uv_handle_t*) &context->poll_handle, cleanup_context); // Use cleanup to free context
      // Use the simpler callback
      // uv_close((uv_handle_t*) &context->poll_handle, free_poll_context);
      // Use standard free
      uv_close((uv_handle_t*) &context->poll_handle, cleanup_easy_handle_and_context);
      // Return error after initiating close
      return -1;
    }
    // If socketp existed (update scenario), maybe just return error?
    // Libcurl docs suggest returning -1 on failure.
    return -1;
  }

  // Associate this context with the socket in curl
  // curl_multi_assign(multi_handle, s, context);
  curl_multi_assign(multi_handle, s, context);
  return 0;
}

static server_list_t* get_server_list(data_request_type_t type) {
  switch (type) {
    case C4_DATA_TYPE_ETH_RPC:
      return (server_list_t*) &eth_rpc_servers;
    case C4_DATA_TYPE_BEACON_API:
      return (server_list_t*) &beacon_api_servers;
    default:
      return NULL;
  }
}

static size_t curl_append(void* contents, size_t size, size_t nmemb, void* buf) {
  buffer_t* buffer = (buffer_t*) buf;
  buffer_grow(buffer, buffer->data.len + size * nmemb + 1);
  buffer_append(buffer, bytes(contents, size * nmemb));
  buffer->data.data[buffer->data.len] = '\0';
  return size * nmemb;
}

typedef struct {
  http_request_cb cb;
  void*           data;
  request_t*      req;
  client_t*       client;
} http_response_t;

static void c4_add_request_response(request_t* req) {
  if (!req || !req->ctx) {
    fprintf(stderr, "ERROR: Invalid request or context in c4_add_request_response\n");
    return;
  }

  http_response_t* res = (http_response_t*) req->ctx;

  // Check that client is still valid and not being closed
  if (!res->client || res->client->being_closed) {
    fprintf(stderr, "WARNING: Client is no longer valid or is being closed - discarding response\n");
  }
  else {
    // Client is still valid, deliver the response
    res->cb(req->client, res->data, req->requests->req);
  }

  // Clean up resources regardless of client state
  free(res);
  free(req->requests);
  free(req);
}

// Function to determine TTL for different request types
static uint32_t get_request_ttl(data_request_t* req) {
  switch (req->type) {
    case C4_DATA_TYPE_BEACON_API:
      if (strcmp(req->url, "eth/v2/beacon/blocks/head") == 0) return 12;
      return 3600 * 24; // 1day
    case C4_DATA_TYPE_ETH_RPC:
      // ETH RPC responses can be cached longer
      return 3600 * 24; // 1day
    case C4_DATA_TYPE_REST_API:
      // REST API responses vary, use a default
      return 60; // 1 minute
    default:
      return 60; // Default 1 minute
  }
}

// Function to generate cache key from request
static char* generate_cache_key(data_request_t* req) {
  buffer_t key = {0};
  bprintf(&key, "%d:%s:%s:%s",
          req->type,
          req->url,
          req->method == C4_DATA_METHOD_POST ? (char*) req->payload.data : "",
          req->encoding == C4_DATA_ENCODING_JSON ? "json" : "ssz");
  bytes32_t hash;
  sha256(key.data, hash);
  buffer_reset(&key);
  bprintf(&key, "%x", bytes(hash, 32));
  return (char*) key.data.data;
}

// Function to handle successful response and cache it
static void cache_response(single_request_t* r) {
  uint32_t ttl = get_request_ttl(r->req);
  if (ttl > 0 && r->req->response.data && r->req->response.len > 0) { // Added check for valid response data
    char* key = generate_cache_key(r->req);
    if (key) { // Check if key generation succeeded
               // Use r->req->response directly instead of r->buffer
      memcache_set(memcache_client, key, strlen(key), (char*) r->req->response.data, r->req->response.len, ttl);
      free(key);
    }
  }
  // Note: r->buffer should already be empty or transferred to r->req->response in handle_curl_events
}

// Helper function to configure SSL settings for an easy handle
static void configure_ssl_settings(CURL* easy) {
  if (!easy) {
    fprintf(stderr, "configure_ssl_settings: NULL easy handle passed\n");
    return;
  }

  // Disable SSL verification for development/testing
  curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 0L);

  // Set SSL protocol version to be flexible - auto-negotiate
  curl_easy_setopt(easy, CURLOPT_SSLVERSION, CURL_SSLVERSION_DEFAULT);

  // Enable TLS 1.3 if available
  curl_easy_setopt(easy, CURLOPT_SSL_OPTIONS, CURLSSLOPT_ALLOW_BEAST | CURLSSLOPT_NO_REVOKE);

  // Disable SSL session reuse to avoid potential issues
  curl_easy_setopt(easy, CURLOPT_SSL_SESSIONID_CACHE, 0L);

  // Uncomment for debugging SSL issues
  // curl_easy_setopt(easy, CURLOPT_VERBOSE, 1L);

  // Enable connection timeout to avoid hanging connections
  curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 30L);
}

// Callback for memcache get operations
static void trigger_uncached_curl_request(void* data, char* value, size_t value_len) {
  single_request_t*  r       = (single_request_t*) data;
  pending_request_t* pending = value == NULL ? pending_find_matching(r) : NULL;

  if (pending) { // there is a pending request asking for the same result
    pending_add_to_same_requests(pending, r);
    printf("join : %s %s\n", r->req->url, r->req->payload.data ? (char*) r->req->payload.data : "");
    // callback will be called when the pending-request is done
  }
  else if (value) { // there is a cached response
    // Cache hit - create response from cached data
    printf("cache: %s %s\n", r->req->url, r->req->payload.data ? (char*) r->req->payload.data : "");
    r->req->response = bytes_dup(bytes(value, value_len));
    r->curl          = NULL; // Mark as done

    call_callback_if_done(r->parent);
  }
  else {
    // Cache miss - proceed with normal request handling
    server_list_t* servers  = get_server_list(r->req->type);
    char*          base_url = servers ? servers->urls[r->req->response_node_index] : NULL;
    char*          req_url  = r->req->url;

    // Safeguard against NULL URLs
    if (!req_url) req_url = "";
    if (!base_url) base_url = "";

    if (strlen(base_url) == 0 && strlen(req_url) > 0)
      r->url = strdup(req_url);
    else if (strlen(req_url) == 0 && strlen(base_url) > 0)
      r->url = strdup(base_url);
    else if (strlen(req_url) > 0 && strlen(base_url) > 0)
      r->url = bprintf(NULL, "%s%s", base_url, req_url);
    else {
      printf(":: ERROR: Empty URL\n");
      r->req->error = bprintf(NULL, "Empty URL");
      call_callback_if_done(r->parent);
      return;
    }

    pending_add(r);
    CURL* easy = curl_easy_init();
    r->curl    = easy;
    curl_easy_setopt(easy, CURLOPT_URL, r->url);
    if (r->req->payload.len && r->req->payload.data) {
      curl_easy_setopt(easy, CURLOPT_POSTFIELDS, r->req->payload.data);
      curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, (long) r->req->payload.len);
    }

    // Set up headers
    r->headers = NULL; // Initialize headers
    if (r->req->payload.len && r->req->payload.data)
      r->headers = curl_slist_append(r->headers, r->req->encoding == C4_DATA_ENCODING_JSON ? "Content-Type: application/json" : "Content-Type: application/octet-stream");
    r->headers = curl_slist_append(r->headers, r->req->encoding == C4_DATA_ENCODING_JSON ? "Accept: application/json" : "Accept: application/octet-stream");
    r->headers = curl_slist_append(r->headers, "charsets: utf-8");
    r->headers = curl_slist_append(r->headers, "User-Agent: c4 curl ");
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, r->headers);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, curl_append);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &r->buffer);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, (uint64_t) 120);
    curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, CURL_METHODS[r->req->method]);
    curl_easy_setopt(easy, CURLOPT_PRIVATE, r);

    // Configure SSL settings for this easy handle
    configure_ssl_settings(easy);

    curl_multi_add_handle(multi_handle, easy);
    printf("send: [%p] %s  %s\n", easy, r->url, r->req->payload.data ? (char*) r->req->payload.data : "");
    // callback will be called when the request by handle_curl_events when all are done.
  }
}

static void trigger_cached_curl_requests(request_t* req) {
  for (size_t i = 0; i < req->request_count; i++) {
    single_request_t* r       = req->requests + i;
    data_request_t*   pending = r->req;
    r->parent                 = req; // Set the parent pointer

    // Check cache first
    char* key = generate_cache_key(pending);
    int   ret = memcache_get(memcache_client, key, strlen(key), r, trigger_uncached_curl_request);
    free(key);
    if (ret) {
      printf("CACHE-Error : %d %s %s\n", ret, r->req->url, r->req->payload.data ? (char*) r->req->payload.data : "");
      trigger_uncached_curl_request(r, NULL, 0);
    }
  }
}

void c4_add_request(client_t* client, data_request_t* req, void* data, http_request_cb cb) {
  // Check if client is valid and not being closed
  if (!client || client->being_closed) {
    fprintf(stderr, "ERROR: Attempted to add request to invalid or closing client\n");
    // Clean up resources since we won't be processing this request
    if (req) {
      free(req->url);
      free(req);
    }
    return;
  }

  http_response_t* res = (http_response_t*) calloc(1, sizeof(http_response_t));
  request_t*       r   = (request_t*) calloc(1, sizeof(request_t));
  r->client            = client;
  r->cb                = c4_add_request_response;
  r->requests          = (single_request_t*) calloc(1, sizeof(single_request_t));
  r->requests->req     = req;
  r->request_count     = 1;
  r->ctx               = res;
  res->cb              = cb;
  res->data            = data;
  res->req             = r;
  res->client          = client;

  trigger_cached_curl_requests(r);
}

void c4_start_curl_requests(request_t* req) {
  int            len = 0, i = 0;
  proofer_ctx_t* ctx = (proofer_ctx_t*) req->ctx;
  for (data_request_t* r = ctx->state.requests; r; r = r->next) {
    if (c4_state_is_pending(r)) len++;
  }
  req->requests      = (single_request_t*) calloc(len, sizeof(single_request_t));
  req->request_count = len;

  for (data_request_t* r = ctx->state.requests; r; r = r->next) {
    if (c4_state_is_pending(r)) req->requests[i++].req = r;
  }

  trigger_cached_curl_requests(req);
}

static void free_single_request(single_request_t* r) {
  buffer_free(&r->buffer);
  free(r->url);
  if (r->headers) {
    curl_slist_free_all(r->headers);
    r->headers = NULL;
  }
}

// we cleanup aftwe curl and retry if needed.
bool c4_check_retry_request(request_t* req) {
  if (!req->request_count) return false;
  int retry_requests = 0;

  for (size_t i = 0; i < req->request_count; i++) {
    single_request_t* r       = req->requests + i;
    data_request_t*   pending = r->req;
    server_list_t*    servers = get_server_list(pending->type);

    if (pending->error && servers && pending->response_node_index + 1 < servers->count) {
      int idx = pending->response_node_index + 1;
      for (int i = idx; i < servers->count; i++) {
        if (pending->node_exclude_mask & (1 << i))
          idx++;
        else
          break;
      }
      if (idx < servers->count) {
        printf(":: Retrying request with server %d: %s\n", idx,
               servers->urls[idx] ? servers->urls[idx] : "NULL");
        free(pending->error);
        pending->response_node_index = idx;
        pending->error               = NULL;
        retry_requests++;
      }
    }
  }

  if (retry_requests == 0) {
    for (int i = 0; i < req->request_count; i++) free_single_request(req->requests + i);
    free(req->requests);
    req->request_count = 0;
    req->requests      = NULL;
    return false;
  }
  else {
    printf(":: Retrying %d requests with different servers\n", retry_requests);
    single_request_t* pendings = (single_request_t*) calloc(retry_requests, sizeof(single_request_t));
    int               j        = 0;
    for (size_t i = 0; i < req->request_count && j < retry_requests; i++) {
      data_request_t* pending = req->requests[i].req;
      if (pending->error == NULL && !pending->response.data) {
        pendings[j++].req = pending;
      }
    }
    for (int i = 0; i < req->request_count; i++) free_single_request(req->requests + i);
    req->requests      = pendings;
    req->request_count = retry_requests;

    trigger_cached_curl_requests(req);

    return true;
  }
}

static void init_serverlist(server_list_t* list, char* servers) {
  if (!servers) return;
  char* servers_copy = strdup(servers);
  int   count        = 0;
  char* token        = strtok(servers_copy, ",");
  while (token) {
    count++;
    token = strtok(NULL, ",");
  }
  memcpy(servers_copy, servers, strlen(servers) + 1);
  list->urls  = (char**) calloc(count, sizeof(char*));
  list->count = count;
  count       = 0;
  token       = strtok(servers_copy, ",");
  while (token) {
    list->urls[count++] = strdup(token);
    token               = strtok(NULL, ",");
  }
  free(servers_copy);
}

void c4_init_curl(uv_timer_t* timer) {
  // Initialize global curl state with SSL support
  curl_global_init(CURL_GLOBAL_SSL | CURL_GLOBAL_DEFAULT);

  // Initialize multi handle
  multi_handle = curl_multi_init();
  curl_multi_setopt(multi_handle, CURLMOPT_SOCKETFUNCTION, socket_callback);
  curl_multi_setopt(multi_handle, CURLMOPT_TIMERFUNCTION, timer_callback);
  curl_multi_setopt(multi_handle, CURLMOPT_TIMERDATA, timer);

  // Initialize memcached client
  memcache_client = memcache_new(http_server.memcached_pool, http_server.memcached_host, http_server.memcached_port);
  if (!memcache_client) {
    fprintf(stderr, "Failed to create memcached client\n");
    return;
  }

  init_serverlist(&eth_rpc_servers, http_server.rpc_nodes);
  init_serverlist(&beacon_api_servers, http_server.beacon_nodes);
}

void c4_cleanup_curl() {
  // Close all handles and let the cleanup function free the resources
  curl_multi_setopt(multi_handle, CURLMOPT_SOCKETFUNCTION, NULL);
  curl_multi_cleanup(multi_handle);
  curl_global_cleanup();
  if (memcache_client) {
    memcache_free(&memcache_client);
  }
}
