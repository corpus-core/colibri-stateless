#include "server.h"
#include "util/json.h"
#include "util/logger.h"
#include <string.h>

// Local write callback (like detection_write_callback) for head polling
static size_t head_write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
  buffer_t* buffer   = (buffer_t*) userp;
  size_t    realsize = size * nmemb;
  buffer_grow(buffer, buffer->data.len + realsize + 1);
  memcpy(buffer->data.data + buffer->data.len, contents, realsize);
  buffer->data.len += realsize;
  buffer->data.data[buffer->data.len] = '\0';
  return realsize;
}

// ---- RPC head polling (eth_blockNumber) - non-blocking via libuv+curl multi ----
typedef struct {
  size_t             server_index;
  buffer_t           response_buffer;
  struct curl_slist* headers;
  char*              url;
  uint64_t           start_ms;
} head_easy_ctx_t;

typedef struct head_poll_ctx_t {
  uv_poll_t               poll_handle;
  curl_socket_t           socket;
  struct head_poll_ctx_t* next;
} head_poll_ctx_t;

static uv_timer_t     g_head_timer;      // scheduling timer for submitting requests
static uv_timer_t     g_head_curl_timer; // curl timeout driver
static bool           g_head_timer_initialized      = false;
static bool           g_head_curl_timer_initialized = false;
static server_list_t*   g_head_servers = NULL;
static CURLM*           g_head_multi   = NULL;
static head_poll_ctx_t* g_head_polls   = NULL; // linked list of active poll handles

static void c4_head_handle_curl_events();

// Helper function to extract clean server name from URL (duplicate of metrics helper)
static const char* head_extract_server_name(const char* url) {
  if (!url) return "unknown";
  const char* start = url;
  if (strncmp(url, "https://", 8) == 0)
    start = url + 8;
  else if (strncmp(url, "http://", 7) == 0)
    start = url + 7;
  const char* slash = strchr(start, '/');
  if (slash) {
    static char server_name[256];
    size_t      len = (size_t) (slash - start);
    if (len >= sizeof(server_name)) len = sizeof(server_name) - 1;
    memcpy(server_name, start, len);
    server_name[len] = '\0';
    return server_name;
  }
  return start;
}

static void c4_head_uv_poll_cb(uv_poll_t* handle, int status, int events) {
  (void) status;
  if (!handle || !handle->data) return;
  head_poll_ctx_t* c     = (head_poll_ctx_t*) handle->data;
  int              flags = 0;
  if (events & UV_READABLE) flags |= CURL_CSELECT_IN;
  if (events & UV_WRITABLE) flags |= CURL_CSELECT_OUT;
  int running = 0;
  curl_multi_socket_action(g_head_multi, c->socket, flags, &running);
  c4_head_handle_curl_events();
}

static void c4_head_timer_cb(uv_timer_t* handle) {
  int running = 0;
  if (!g_head_multi) return;
  curl_multi_socket_action(g_head_multi, CURL_SOCKET_TIMEOUT, 0, &running);
  c4_head_handle_curl_events();
}

static int c4_head_timer_callback(CURLM* multi, long timeout_ms, void* userp) {
  (void) multi;
  (void) userp;
  if (!g_head_curl_timer_initialized) {
    uv_timer_init(uv_default_loop(), &g_head_curl_timer);
    g_head_curl_timer_initialized = true;
  }
  if (timeout_ms >= 0) {
    uv_timer_start(&g_head_curl_timer, c4_head_timer_cb, timeout_ms, 0);
  }
  return 0;
}

static void c4_head_poll_close_cb(uv_handle_t* h) {
  if (!h) return;
  head_poll_ctx_t* ctx = (head_poll_ctx_t*) h->data;
  if (ctx) safe_free(ctx);
}

static int c4_head_socket_callback(CURL* easy, curl_socket_t s, int what, void* userp, void* socketp) {
  (void) easy;
  (void) userp;
  head_poll_ctx_t* ctx = (head_poll_ctx_t*) socketp;
  if (what == CURL_POLL_REMOVE) {
    if (ctx) {
      uv_poll_stop(&ctx->poll_handle);
      uv_close((uv_handle_t*) &ctx->poll_handle, c4_head_poll_close_cb);
      curl_multi_assign(g_head_multi, s, NULL);
      // Remove from list
      head_poll_ctx_t** p = &g_head_polls;
      while (*p) {
        if (*p == ctx) { *p = ctx->next; break; }
        p = &(*p)->next;
      }
    }
    return 0;
  }
  if (!ctx) {
    ctx         = (head_poll_ctx_t*) safe_calloc(1, sizeof(head_poll_ctx_t));
    ctx->socket = s;
    uv_poll_init_socket(uv_default_loop(), &ctx->poll_handle, s);
    ctx->poll_handle.data = ctx;
    curl_multi_assign(g_head_multi, s, ctx);
    // Track handle
    ctx->next   = g_head_polls;
    g_head_polls = ctx;
  }
  int events = 0;
  if (what & CURL_POLL_IN) events |= UV_READABLE;
  if (what & CURL_POLL_OUT) events |= UV_WRITABLE;
  uv_poll_start(&ctx->poll_handle, events, c4_head_uv_poll_cb);
  return 0;
}

static void c4_head_handle_curl_events() {
  if (!g_head_multi) return;
  CURLMsg* msg;
  int      left;
  while ((msg = curl_multi_info_read(g_head_multi, &left))) {
    if (msg->msg != CURLMSG_DONE) continue;
    CURL*            easy = msg->easy_handle;
    head_easy_ctx_t* ctx  = NULL;
    curl_easy_getinfo(easy, CURLINFO_PRIVATE, &ctx);
    long code = 0;
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &code);
    if (msg->data.result == CURLE_OK && code == 200 && ctx && ctx->response_buffer.data.len > 0) {
      json_t   root  = json_parse((char*) ctx->response_buffer.data.data);
      uint64_t block = json_get_uint64(root, "result");
      if (block > 0 && g_head_servers && ctx->server_index < g_head_servers->count) {
        server_health_t* h   = &g_head_servers->health_stats[ctx->server_index];
        h->latest_block      = block;
        h->head_last_seen_ms = current_ms();
        // Debug log for polling result (server name + latency)
        const char* name   = head_extract_server_name(g_head_servers->urls[ctx->server_index]);
        uint64_t    elapse = current_ms() - ctx->start_ms;
        log_debug("head poll: [%d] head=%l latency_ms=%l ( %s )", (uint32_t) ctx->server_index, block, elapse, name);
      }
    }
    curl_multi_remove_handle(g_head_multi, easy);
    curl_easy_cleanup(easy);
    if (ctx) {
      if (ctx->headers) curl_slist_free_all(ctx->headers);
      if (ctx->url) safe_free(ctx->url);
      buffer_free(&ctx->response_buffer);
      safe_free(ctx);
    }
  }
}

static void c4_head_poll_cb(uv_timer_t* handle) {
  (void) handle;
  if (!g_head_servers || g_head_servers->count == 0 || !g_head_multi) return;
  const char* rpc_payload = "{\"jsonrpc\":\"2.0\",\"method\":\"eth_blockNumber\",\"params\":[],\"id\":1}";
  for (size_t i = 0; i < g_head_servers->count; i++) {
    char* base_url = g_head_servers->urls[i];
    if (!base_url || !*base_url) continue;
    head_easy_ctx_t* ctx = (head_easy_ctx_t*) safe_calloc(1, sizeof(head_easy_ctx_t));
    ctx->server_index    = i;
    ctx->response_buffer = (buffer_t) {0};
    ctx->headers         = NULL;
    ctx->url             = strdup(base_url);
    CURL* easy           = curl_easy_init();
    if (!easy) {
      safe_free(ctx->url);
      safe_free(ctx);
      continue;
    }
    curl_easy_setopt(easy, CURLOPT_URL, ctx->url);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, head_write_callback);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &ctx->response_buffer);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 0L);
    ctx->headers = curl_slist_append(ctx->headers, "Content-Type: application/json");
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, ctx->headers);
    curl_easy_setopt(easy, CURLOPT_POSTFIELDS, rpc_payload);
    curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, strlen(rpc_payload));
    ctx->start_ms = current_ms();
    curl_easy_setopt(easy, CURLOPT_PRIVATE, ctx);
    curl_multi_add_handle(g_head_multi, easy);
  }
}

bool c4_start_rpc_head_poller(server_list_t* servers) {
  if (!servers || servers->count == 0) return false;
  if (!http_server.rpc_head_poll_enabled) return false;
  g_head_servers = servers;
  if (!g_head_multi) {
    g_head_multi = curl_multi_init();
    curl_multi_setopt(g_head_multi, CURLMOPT_SOCKETFUNCTION, c4_head_socket_callback);
    curl_multi_setopt(g_head_multi, CURLMOPT_TIMERFUNCTION, c4_head_timer_callback);
  }
  if (!g_head_timer_initialized) {
    uv_timer_init(uv_default_loop(), &g_head_timer);
    g_head_timer_initialized = true;
  }
  uint64_t interval = (uint64_t) (http_server.rpc_head_poll_interval_ms > 0 ? http_server.rpc_head_poll_interval_ms : 6000);
  uv_timer_start(&g_head_timer, c4_head_poll_cb, interval, interval);
  fprintf(stderr, ":: RPC head polling started (interval %llu ms)\n", (unsigned long long) interval);
  return true;
}

void c4_stop_rpc_head_poller(void) {
  // Stop submission timer
  if (g_head_timer_initialized) {
    uv_timer_stop(&g_head_timer);
    uv_close((uv_handle_t*) &g_head_timer, NULL);
    g_head_timer_initialized = false;
  }
  // Stop curl timer
  if (g_head_curl_timer_initialized) {
    uv_timer_stop(&g_head_curl_timer);
    uv_close((uv_handle_t*) &g_head_curl_timer, NULL);
    g_head_curl_timer_initialized = false;
  }
  // Close all active poll handles
  head_poll_ctx_t* cur = g_head_polls;
  while (cur) {
    head_poll_ctx_t* next = cur->next;
    uv_poll_stop(&cur->poll_handle);
    uv_close((uv_handle_t*) &cur->poll_handle, c4_head_poll_close_cb);
    cur = next;
  }
  g_head_polls = NULL;

  // Cleanup CURL multi
  if (g_head_multi) {
    curl_multi_cleanup(g_head_multi);
    g_head_multi = NULL;
  }
  g_head_servers = NULL;
}
