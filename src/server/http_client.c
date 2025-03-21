#include "server.h"

typedef struct {
  char** urls;
  size_t count;
} server_list_t;

static CURLM* multi_handle;
const char*   CURL_METHODS[] = {"GET", "POST", "PUT", "DELETE"};

static const server_list_t eth_rpc_servers = {
    .urls = (char*[]) {
        "https://nameless-sly-reel.quiknode.pro/5937339c28c09a908994b74e2514f0f6cfdac584/",
        "https://eth-mainnet.g.alchemy.com/v2/B8W2IZrDkCkkjKxQOl70XNIy4x4PT20S",
        "https://rpc.ankr.com/eth/33d0414ebb46bda32a461ecdbd201f9cf5141a0acb8f95c718c23935d6febfcd"},
    .count = 3};

static const server_list_t beacon_api_servers = {
    .urls = (char*[]) {
        "https://lodestar-mainnet.chainsafe.io"},
    .count = 1};

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
          CURLcode res = msg->data.result;
          if (res == CURLE_OK) {
            req->requests[i].req->response = req->requests[i].buffer.data;
            req->requests[i].buffer        = (buffer_t) {0};
          }
          else
            req->requests[i].req->error = bprintf(NULL, "%s : %s", curl_easy_strerror(res), bprintf(&req->requests[i].buffer, " "));

          req->requests[i].curl = NULL; // setting it to NULL marks it as done
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

static void init_curl_requests(request_t* req) {
  for (size_t i = 0; i < req->request_count; i++) {
    single_request_t* r        = req->requests + i;
    data_request_t*   pending  = r->req;
    server_list_t*    servers  = get_server_list(pending->type);
    char*             base_url = servers ? servers->urls[pending->response_node_index] : NULL;
    char*             req_url  = pending->url;
    CURL*             easy     = curl_easy_init();
    r->curl                    = easy;

    if (!base_url)
      r->url = strdup(req_url);
    else if (!req_url)
      r->url = strdup(base_url);
    else
      r->url = bprintf(NULL, "%s%s", base_url, req_url);

    curl_easy_setopt(easy, CURLOPT_URL, r->url);
    if (pending->payload.len && pending->payload.data) {
      curl_easy_setopt(easy, CURLOPT_POSTFIELDS, pending->payload.data);
      curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, (long) pending->payload.len);
    }

    struct curl_slist* headers = NULL;
    headers                    = curl_slist_append(headers, pending->encoding == C4_DATA_ENCODING_JSON ? "Accept: application/json" : "Accept: application/octet-stream");
    if (pending->payload.len && pending->payload.data)
      headers = curl_slist_append(headers, pending->encoding == C4_DATA_ENCODING_JSON ? "Content-Type: application/json" : "Content-Type: application/octet-stream");
    headers = curl_slist_append(headers, "charsets: utf-8");
    headers = curl_slist_append(headers, "User-Agent: c4 curl ");
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, curl_append);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &r->buffer);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, (uint64_t) 120);
    curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, CURL_METHODS[pending->method]);
    curl_easy_setopt(easy, CURLOPT_PRIVATE, req);
    curl_multi_add_handle(multi_handle, easy);
  }
}

void c4_start_curl_requests(request_t* req) {
  int            len = 0, i = 0;
  proofer_ctx_t* ctx = (proofer_ctx_t*) req->ctx;
  for (data_request_t* r = ctx->state.requests; r; r = r->next) {
    if (r->response.data == NULL && r->error == NULL) len++;
  }
  req->requests      = (single_request_t*) calloc(len, sizeof(single_request_t));
  req->request_count = len;

  for (data_request_t* r = ctx->state.requests; r; r = r->next)
    req->requests[i].req = r;

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
}

void c4_cleanup_curl() {
  curl_multi_cleanup(multi_handle);
}
typedef struct {
  http_request_cb cb;
  void*           data;
} http_response_t;

static void c4_add_request_response(request_t* req) {
  http_response_t* res = (http_response_t*) req->ctx;
  res->cb(req->client, res->data, req->requests->req);
  free(res);
  free(req->requests);
  free(req);
}
void c4_add_request(client_t* client, data_request_t* req, void* data, http_request_cb cb) {
  http_response_t* res = calloc(1, sizeof(http_response_t));
  request_t*       r   = calloc(1, sizeof(request_t));
  r->client            = client;
  r->cb                = c4_add_request_response;
  r->requests          = calloc(1, sizeof(single_request_t));
  r->requests->req     = req;
  r->request_count     = 1;
  r->ctx               = res;
  res->cb              = cb;
  res->data            = data;

  init_curl_requests(r);
}
