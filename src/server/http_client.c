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

static const server_list_t eth_rpc_servers = {
    .urls = (char*[]) {
        "https://nameless-sly-reel.quiknode.pro/5937339c28c09a908994b74e2514f0f6cfdac584/",
        "https://eth-mainnet.g.alchemy.com/v2/B8W2IZrDkCkkjKxQOl70XNIy4x4PT20S",
        "https://rpc.ankr.com/eth/33d0414ebb46bda32a461ecdbd201f9cf5141a0acb8f95c718c23935d6febfcd"},
    .count = 3};

static const server_list_t beacon_api_servers = {
    .urls = (char*[]) {
        "https://lodestar-mainnet.chainsafe.io/"},
    .count = 1};

static void handle_successful_response(single_request_t* r, data_request_t* data_req);
static void memcache_get_cb(void* data, char* value, size_t value_len);

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
static void check_multi_info() {
  CURLMsg* msg;
  int      msgs_left;
  while ((msg = curl_multi_info_read(multi_handle, &msgs_left))) {
    if (msg->msg == CURLMSG_DONE) {
      CURL*      easy = msg->easy_handle;
      request_t* req;
      curl_easy_getinfo(easy, CURLINFO_PRIVATE, &req);
      for (size_t i = 0; i < req->request_count; i++) {
        if (req->requests[i].curl == easy) {
          // set response
          single_request_t*  r       = req->requests + i;
          pending_request_t* pending = pending_find(r);
          CURLcode           res     = msg->data.result;
          if (res == CURLE_OK) {
            printf("recv: [%p] %s : %d\n", easy, r->req->url, r->buffer.data.len);
            r->req->response = r->buffer.data;
            handle_successful_response(r, r->req);
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
              memcache_get_cb(same->request, (char*) r->req->response.data, r->req->response.len);
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
  check_multi_info();
}

static void timer_cb(uv_timer_t* handle) {
  int running_handles;
  curl_multi_socket_action(multi_handle, CURL_SOCKET_TIMEOUT, 0, &running_handles);
  check_multi_info();
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
static void handle_successful_response(single_request_t* r, data_request_t* data_req) {
  // Cache the response
  char*    key = generate_cache_key(data_req);
  uint32_t ttl = get_request_ttl(data_req);
  bytes_write(data_req->response, fopen(key, "wb"), true);
  memcache_set(memcache_client, key, 64, (char*) r->buffer.data.data, r->buffer.data.len, ttl, NULL, NULL);
  free(key);
}

// Callback for memcache get operations
static void memcache_get_cb(void* data, char* value, size_t value_len) {
  single_request_t*  r       = (single_request_t*) data;
  pending_request_t* pending = value == NULL ? pending_find_matching(r) : NULL;

  if (pending)
    pending_add_to_same_requests(pending, r);
  else if (value) {
    printf("cache: %s %s\n", r->req->url, r->req->payload.data ? (char*) r->req->payload.data : "");
    char* cache_key = generate_cache_key(r->req);
    char* fname     = bprintf(NULL, "%s_from_cache", cache_key);
    bytes_write(bytes(value, value_len), fopen(fname, "wb"), true);
    free(cache_key);
    free(fname);
    // Cache hit - create response from cached data
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
    if (all_done) {
      parent->cb(parent);
    }
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
    headers                    = curl_slist_append(headers, r->req->encoding == C4_DATA_ENCODING_JSON ? "Accept: application/json" : "Accept: application/octet-stream");
    if (r->req->payload.len && r->req->payload.data)
      headers = curl_slist_append(headers, r->req->encoding == C4_DATA_ENCODING_JSON ? "Content-Type: application/json" : "Content-Type: application/octet-stream");
    headers = curl_slist_append(headers, "charsets: utf-8");
    headers = curl_slist_append(headers, "User-Agent: c4 curl ");
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, curl_append);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &r->buffer);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, (uint64_t) 120);
    curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, CURL_METHODS[r->req->method]);
    curl_easy_setopt(easy, CURLOPT_PRIVATE, r->parent);
    curl_multi_add_handle(multi_handle, easy);
    printf("send: [%p] %s  %s\n", easy, r->url, r->req->payload.data ? (char*) r->req->payload.data : "");
  }
}

static void init_curl_requests(request_t* req) {
  for (size_t i = 0; i < req->request_count; i++) {
    single_request_t* r       = req->requests + i;
    data_request_t*   pending = r->req;
    r->parent                 = req; // Set the parent pointer

    // Check cache first
    char* key = generate_cache_key(pending);
    int   ret = memcache_get(memcache_client, key, strlen(key), r, memcache_get_cb);
    free(key);
    if (ret) {
      printf("CACHE-Error : %d %s %s\n", ret, r->req->url, r->req->payload.data ? (char*) r->req->payload.data : "");
      memcache_get_cb(r, NULL, 0);
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

  init_curl_requests(r);
}

void c4_start_curl_requests(request_t* req) {
  int            len = 0, i = 0;
  proofer_ctx_t* ctx = (proofer_ctx_t*) req->ctx;
  for (data_request_t* r = ctx->state.requests; r; r = r->next) {
    if (r->response.data == NULL && r->error == NULL) len++;
  }
  req->requests      = (single_request_t*) calloc(len, sizeof(single_request_t));
  req->request_count = len;

  for (data_request_t* r = ctx->state.requests; r; r = r->next) {
    if (r->response.data == NULL && r->error == NULL) req->requests[i++].req = r;
  }

  init_curl_requests(req);
}
static void free_single_request(single_request_t* r) {
  buffer_free(&r->buffer);
  free(r->url);
}
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

  //  free(req->pending_handles);
  //  req->pending_handles = NULL;
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

    init_curl_requests(req);

    return true;
  }
}

void c4_init_curl(uv_timer_t* timer) {
  multi_handle = curl_multi_init();
  curl_multi_setopt(multi_handle, CURLMOPT_SOCKETFUNCTION, socket_callback);
  curl_multi_setopt(multi_handle, CURLMOPT_TIMERFUNCTION, timer_callback);
  curl_multi_setopt(multi_handle, CURLMOPT_TIMERDATA, timer);

  // Initialize memcached client
  memcache_client = memcache_new(10); // Pool size of 10 connections
  if (!memcache_client) {
    fprintf(stderr, "Failed to create memcached client\n");
    return;
  }

  // Connect to memcached server using environment variables or defaults
  const char* memcached_host = getenv("MEMCACHED_HOST");
  const char* memcached_port = getenv("MEMCACHED_PORT");

  if (!memcached_host) memcached_host = "127.0.0.1";
  if (!memcached_port) memcached_port = "11211";

  int port = atoi(memcached_port);
  if (port <= 0) port = 11211; // Default if invalid port

  if (memcache_connect(memcache_client, memcached_host, port) != 0) {
    fprintf(stderr, "Failed to connect to memcached server at %s:%d\n", memcached_host, port);
    memcache_free(&memcache_client);
    return;
  }
}

void c4_cleanup_curl() {
  curl_multi_cleanup(multi_handle);
  if (memcache_client) {
    memcache_free(&memcache_client);
  }
}
