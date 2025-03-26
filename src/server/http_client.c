#include "cache.h"
#include "server.h"

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

// Prüft abgeschlossene Übertragungen
static void handle_curl_events() {
  CURLMsg* msg;
  int      msgs_left;
  while ((msg = curl_multi_info_read(multi_handle, &msgs_left))) {
    if (msg->msg == CURLMSG_DONE) {
      CURL*      easy = msg->easy_handle;
      request_t* req;
      curl_easy_getinfo(easy, CURLINFO_PRIVATE, &req);
      if (!req) continue;
      for (size_t i = 0; i < req->request_count; i++) {
        if (req->requests[i].curl == easy) {
          single_request_t*  r       = req->requests + i;
          pending_request_t* pending = pending_find(r);
          CURLcode           res     = msg->data.result;
          if (res == CURLE_OK) {
            printf("recv: [%p] %s : %d\n", easy, r->req->url, r->buffer.data.len);
            r->req->response = r->buffer.data;
            cache_response(r);
            r->buffer = (buffer_t) {0};
          }
          else {
            r->req->error = bprintf(NULL, "%s : %s", curl_easy_strerror(res), bprintf(&r->buffer, " "));
            printf("recv: [%p] %s : %s\n", easy, r->req->url, r->req->error);
          }
          if (pending) {
            pending_request_t* same = pending->same_requests;
            pending_remove(r);
            while (same) {
              trigger_uncached_curl_request(same->request, (char*) r->req->response.data, r->req->response.len);
              pending_request_t* next = same->next;
              free(same);
              same = next;
            }
          }

          r->curl = NULL; // setting it to NULL marks it as done
          break;
        }
      }
      curl_multi_remove_handle(multi_handle, easy);
      curl_easy_cleanup(easy);
      bool all_done = true;
      for (size_t i = 0; i < req->request_count; i++) {
        if (req->requests[i].curl) {
          all_done = false;
          break;
        }
      }
      if (all_done)
        req->cb(req);
    }
  }
}

// Poll-Callback für Socket-Ereignisse
static void poll_cb(uv_poll_t* handle, int status, int events) {
  int running_handles;
  int flags = 0;
  if (events & UV_READABLE) flags |= CURL_CSELECT_IN;
  if (events & UV_WRITABLE) flags |= CURL_CSELECT_OUT;
  curl_multi_socket_action(multi_handle, handle->io_watcher.fd, flags, &running_handles);
  handle_curl_events();
}

static void timer_cb(uv_timer_t* handle) {
  int running_handles;
  curl_multi_socket_action(multi_handle, CURL_SOCKET_TIMEOUT, 0, &running_handles);
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
  uv_poll_t* poll   = (socketp) ? (uv_poll_t*) socketp : calloc(1, sizeof(uv_poll_t));
  int        events = 0;
  if (what & CURL_POLL_IN) events |= UV_READABLE;
  if (what & CURL_POLL_OUT) events |= UV_WRITABLE;
  uv_poll_init_socket(uv_default_loop(), poll, s);
  uv_poll_start(poll, events, poll_cb);
  curl_multi_assign(multi_handle, s, poll);
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
  http_response_t* res = (http_response_t*) req->ctx;
  res->cb(req->client, res->data, req->requests->req);
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
  if (ttl > 0) {
    char* key = generate_cache_key(r->req);
    memcache_set(memcache_client, key, 64, (char*) r->buffer.data.data, r->buffer.data.len, ttl, NULL, NULL);
    free(key);
  }
}

// Helper function to configure SSL settings for an easy handle
static void configure_ssl_settings(CURL* easy) {
  curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 0L);   // Disable SSL certificate verification
  curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 0L);   // Disable hostname verification
  curl_easy_setopt(easy, CURLOPT_SSL_VERIFYSTATUS, 0L); // Disable OCSP verification
  curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 0L);   // Allow self-signed certificates
  curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 0L);   // Allow self-signed certificates
  curl_easy_setopt(easy, CURLOPT_SSL_VERIFYSTATUS, 0L); // Allow self-signed certificates
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

    // Check if all requests are done
    request_t* parent   = r->parent;
    bool       all_done = true;
    for (size_t i = 0; i < parent->request_count; i++) {
      if (c4_state_is_pending(parent->requests[i].req)) {
        all_done = false;
        break;
      }
    }
    if (all_done) parent->cb(parent);
    // if !all_done, the callback will be called when the last one is done
  }
  else {
    // Cache miss - proceed with normal request handling
    pending_add(r);
    server_list_t* servers  = get_server_list(r->req->type);
    char*          base_url = servers ? servers->urls[r->req->response_node_index] : NULL;
    char*          req_url  = r->req->url;
    CURL*          easy     = curl_easy_init();
    r->curl                 = easy;

    if (!base_url)
      r->url = strdup(req_url);
    else if (!req_url)
      r->url = strdup(base_url);
    else
      r->url = bprintf(NULL, "%s%s", base_url, req_url);

    curl_easy_setopt(easy, CURLOPT_URL, r->url);
    if (r->req->payload.len && r->req->payload.data) {
      curl_easy_setopt(easy, CURLOPT_POSTFIELDS, r->req->payload.data);
      curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, (long) r->req->payload.len);
    }

    struct curl_slist* headers = NULL;
    if (r->req->payload.len && r->req->payload.data)
      headers = curl_slist_append(headers, r->req->encoding == C4_DATA_ENCODING_JSON ? "Content-Type: application/json" : "Content-Type: application/octet-stream");
    headers = curl_slist_append(headers, r->req->encoding == C4_DATA_ENCODING_JSON ? "Accept: application/json" : "Accept: application/octet-stream");
    headers = curl_slist_append(headers, "charsets: utf-8");
    headers = curl_slist_append(headers, "User-Agent: c4 curl ");
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, curl_append);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &r->buffer);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, (uint64_t) 120);
    curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, CURL_METHODS[r->req->method]);
    curl_easy_setopt(easy, CURLOPT_PRIVATE, r->parent);

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
    single_request_t* pendings = (single_request_t*) calloc(retry_requests, sizeof(single_request_t));
    int               j        = 0;
    for (size_t i = 0; i < req->request_count && j < retry_requests; i++) {
      data_request_t* pending = req->requests[i].req;
      if (!pending->error && !pending->response.data)
        pendings[j++].req = pending;
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
  // Initialize global curl state
  curl_global_init(CURL_GLOBAL_DEFAULT);

  // Initialize multi handle
  multi_handle = curl_multi_init();
  curl_multi_setopt(multi_handle, CURLMOPT_SOCKETFUNCTION, socket_callback);
  curl_multi_setopt(multi_handle, CURLMOPT_TIMERFUNCTION, timer_callback);
  curl_multi_setopt(multi_handle, CURLMOPT_TIMERDATA, timer);

  // Initialize memcached client
  memcache_client = memcache_new(http_server.memcached_pool, http_server.memcached_host, http_server.memcached_port); // Pool size of 10 connections
  if (!memcache_client) {
    fprintf(stderr, "Failed to create memcached client\n");
    return;
  }

  init_serverlist(&eth_rpc_servers, http_server.rpc_nodes);
  init_serverlist(&beacon_api_servers, http_server.beacon_nodes);
}

void c4_cleanup_curl() {
  curl_multi_cleanup(multi_handle);
  curl_global_cleanup();
  if (memcache_client) {
    memcache_free(&memcache_client);
  }
}
