/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "tracing.h"

#include "../util/bytes.h"
#include "logger.h"
#include "server.h"

#include <curl/curl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <uv.h>

// ---- Internal structures -----------------------------------------------------------------------

typedef struct {
  uint8_t bytes[16]; // 128-bit trace id
} trace_id_t;

typedef struct {
  uint8_t bytes[8]; // 64-bit span id
} span_id_t;

typedef struct tag_entry {
  char*             key;
  char*             value_json; // valid JSON value (quoted already if string)
  struct tag_entry* next;
} tag_entry_t;

struct trace_span {
  trace_id_t   trace_id;
  span_id_t    span_id;
  span_id_t    parent_id;
  uint64_t     start_ms;
  uint64_t     end_ms;
  int          sampled; // 0/1
  char*        name;
  tag_entry_t* tags;
  int          finished;
};

typedef struct {
  int    enabled;
  double sample_rate;
  char*  url;
  char*  service_name;
} tracer_t;

static tracer_t g_tracer      = {0};
static buffer_t g_batch       = {0};
static int      g_batch_count = 0;

// External time function providing Unix epoch milliseconds
extern uint64_t current_unix_ms();

// ---- libcurl multi transport (non-blocking) ----------------------------------------------------
typedef struct trace_poll_ctx_t {
  uv_poll_t                poll_handle;
  curl_socket_t            socket;
  struct trace_poll_ctx_t* next;
} trace_poll_ctx_t;

typedef struct {
  buffer_t           body;
  struct curl_slist* headers;
  buffer_t           resp; // response buffer for diagnostics
} trace_easy_ctx_t;

static CURLM*            g_trace_multi = NULL;
static trace_poll_ctx_t* g_trace_polls = NULL;
static uv_timer_t        g_trace_curl_timer;
static bool              g_trace_curl_timer_initialized = false;

static void c4_trace_handle_curl_events();

static void c4_trace_poll_close_cb(uv_handle_t* h) {
  if (!h) return;
  trace_poll_ctx_t* ctx = (trace_poll_ctx_t*) h->data;
  if (ctx) safe_free(ctx);
}

static void c4_trace_uv_poll_cb(uv_poll_t* handle, int status, int events) {
  (void) status;
  if (!handle || !handle->data) return;
  trace_poll_ctx_t* c     = (trace_poll_ctx_t*) handle->data;
  int               flags = 0;
  if (events & UV_READABLE) flags |= CURL_CSELECT_IN;
  if (events & UV_WRITABLE) flags |= CURL_CSELECT_OUT;
  int running = 0;
  curl_multi_socket_action(g_trace_multi, c->socket, flags, &running);
  c4_trace_handle_curl_events();
}

static void c4_trace_timer_cb(uv_timer_t* handle) {
  (void) handle;
  int running = 0;
  if (!g_trace_multi) return;
  curl_multi_socket_action(g_trace_multi, CURL_SOCKET_TIMEOUT, 0, &running);
  c4_trace_handle_curl_events();
}

static int c4_trace_timer_callback(CURLM* multi, long timeout_ms, void* userp) {
  (void) multi;
  (void) userp;
  if (!g_trace_curl_timer_initialized) {
    uv_timer_init(uv_default_loop(), &g_trace_curl_timer);
    g_trace_curl_timer_initialized = true;
  }
  if (timeout_ms >= 0) {
    uv_timer_start(&g_trace_curl_timer, c4_trace_timer_cb, timeout_ms, 0);
  }
  return 0;
}

static int c4_trace_socket_callback(CURL* easy, curl_socket_t s, int what, void* userp, void* socketp) {
  (void) easy;
  (void) userp;
  trace_poll_ctx_t* ctx = (trace_poll_ctx_t*) socketp;
  if (what == CURL_POLL_REMOVE) {
    if (ctx) {
      uv_poll_stop(&ctx->poll_handle);
      uv_close((uv_handle_t*) &ctx->poll_handle, c4_trace_poll_close_cb);
      curl_multi_assign(g_trace_multi, s, NULL);
      // Remove from list
      trace_poll_ctx_t** p = &g_trace_polls;
      while (*p) {
        if (*p == ctx) {
          *p = ctx->next;
          break;
        }
        p = &(*p)->next;
      }
    }
    return 0;
  }
  if (!ctx) {
    ctx         = (trace_poll_ctx_t*) safe_calloc(1, sizeof(trace_poll_ctx_t));
    ctx->socket = s;
    uv_poll_init_socket(uv_default_loop(), &ctx->poll_handle, s);
    ctx->poll_handle.data = ctx;
    curl_multi_assign(g_trace_multi, s, ctx);
    // Track handle
    ctx->next     = g_trace_polls;
    g_trace_polls = ctx;
  }
  int events = 0;
  if (what & CURL_POLL_IN) events |= UV_READABLE;
  if (what & CURL_POLL_OUT) events |= UV_WRITABLE;
  uv_poll_start(&ctx->poll_handle, events, c4_trace_uv_poll_cb);
  return 0;
}

static void tracing_transport_init(void) {
  if (!g_trace_multi) {
    g_trace_multi = curl_multi_init();
    curl_multi_setopt(g_trace_multi, CURLMOPT_SOCKETFUNCTION, c4_trace_socket_callback);
    curl_multi_setopt(g_trace_multi, CURLMOPT_TIMERFUNCTION, c4_trace_timer_callback);
    // Use conservative connection reuse; no extra limits needed here
  }
  if (!g_trace_curl_timer_initialized) {
    uv_timer_init(uv_default_loop(), &g_trace_curl_timer);
    g_trace_curl_timer_initialized = true;
  }
}

// Small write callback to capture Tempo response for diagnostics
static size_t trace_write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
  buffer_t* buffer   = (buffer_t*) userp;
  size_t    realsize = size * nmemb;
  if (!buffer) return realsize;
  buffer_grow(buffer, buffer->data.len + realsize + 1);
  memcpy(buffer->data.data + buffer->data.len, contents, realsize);
  buffer->data.len += realsize;
  buffer->data.data[buffer->data.len] = '\0';
  return realsize;
}

static void c4_trace_handle_curl_events() {
  if (!g_trace_multi) return;
  CURLMsg* msg;
  int      left;
  while ((msg = curl_multi_info_read(g_trace_multi, &left))) {
    if (msg->msg != CURLMSG_DONE) continue;
    CURL*             easy = msg->easy_handle;
    trace_easy_ctx_t* ctx  = NULL;
    curl_easy_getinfo(easy, CURLINFO_PRIVATE, &ctx);
    // Inspect result and HTTP status for diagnostics
    long http_code = 0;
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &http_code);
    if (msg->data.result != CURLE_OK || http_code < 200 || http_code >= 300) {
      const char* err              = curl_easy_strerror(msg->data.result);
      char        snippet_raw[201] = {0};
      if (ctx && ctx->resp.data.data && ctx->resp.data.len > 0) {
        size_t n = ctx->resp.data.len > 200 ? 200 : ctx->resp.data.len;
        memcpy(snippet_raw, ctx->resp.data.data, n);
        snippet_raw[n] = '\0';
      }
      log_warn("Tracing export: HTTP %ld, CURL %d (%s) resp=\"%S\"",
               http_code, (int) msg->data.result, err ? err : "",
               snippet_raw);
    }
    curl_multi_remove_handle(g_trace_multi, easy);
    curl_easy_cleanup(easy);
    if (ctx) {
      if (ctx->headers) curl_slist_free_all(ctx->headers);
      buffer_free(&ctx->body);
      buffer_free(&ctx->resp);
      safe_free(ctx);
    }
  }
}

static void tracing_enqueue_body(buffer_t* body) {
  if (!g_tracer.url || !*g_tracer.url) {
    buffer_free(body);
    return;
  }
  tracing_transport_init();
  trace_easy_ctx_t* ctx = (trace_easy_ctx_t*) safe_calloc(1, sizeof(trace_easy_ctx_t));
  // Transfer ownership of body to ctx

  //  bytes_write(body->data, fopen("trace_body.json", "wb"), true);
  ctx->body       = *body;
  body->data.data = NULL;
  body->data.len  = 0;
  ctx->headers    = curl_slist_append(NULL, "Content-Type: application/json");

  CURL* easy = curl_easy_init();
  if (!easy) {
    if (ctx->headers) curl_slist_free_all(ctx->headers);
    buffer_free(&ctx->body);
    safe_free(ctx);
    return;
  }
  curl_easy_setopt(easy, CURLOPT_URL, g_tracer.url);
  curl_easy_setopt(easy, CURLOPT_HTTPHEADER, ctx->headers);
  curl_easy_setopt(easy, CURLOPT_POSTFIELDS, ctx->body.data.data ? (char*) ctx->body.data.data : "");
  curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, (long) ctx->body.data.len);
  // Short timeouts to avoid long-hung exports; still non-blocking via multi
  curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, 500L);
  curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, 250L);
  // Capture a small response body for diagnostics
  curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, trace_write_callback);
  curl_easy_setopt(easy, CURLOPT_WRITEDATA, &ctx->resp);
  curl_easy_setopt(easy, CURLOPT_PRIVATE, ctx);

  curl_multi_add_handle(g_trace_multi, easy);
}

// ---- Utilities ---------------------------------------------------------------------------------

static void bytes_to_hex(const uint8_t* in, size_t len, char* out) {
  static const char* hex = "0123456789abcdef";
  for (size_t i = 0; i < len; i++) {
    out[i * 2 + 0] = hex[(in[i] >> 4) & 0x0F];
    out[i * 2 + 1] = hex[(in[i] >> 0) & 0x0F];
  }
  out[len * 2] = '\0';
}

static void gen_random_bytes(uint8_t* out, size_t len) {
  // Use libuv's secure random (synchronous when cb == NULL). Fallback: memset zero on error.
  if (uv_random(uv_default_loop(), NULL, out, len, 0, NULL) != 0) {
    memset(out, 0, len);
  }
}

static span_id_t gen_span_id(void) {
  span_id_t id;
  gen_random_bytes(id.bytes, sizeof(id.bytes));
  // Ensure non-zero
  int all_zero = 1;
  for (size_t i = 0; i < sizeof(id.bytes); i++)
    if (id.bytes[i] != 0) {
      all_zero = 0;
      break;
    }
  if (all_zero) id.bytes[sizeof(id.bytes) - 1] = 1;
  return id;
}

static trace_id_t gen_trace_id(void) {
  trace_id_t id;
  gen_random_bytes(id.bytes, sizeof(id.bytes));
  // Ensure non-zero
  int all_zero = 1;
  for (size_t i = 0; i < sizeof(id.bytes); i++)
    if (id.bytes[i] != 0) {
      all_zero = 0;
      break;
    }
  if (all_zero) id.bytes[sizeof(id.bytes) - 1] = 1;
  return id;
}

static int should_sample(double p) {
  if (p <= 0) return 0;
  if (p >= 1) return 1;
  uint32_t r = 0;
  gen_random_bytes((uint8_t*) &r, sizeof(r));
  double u = (double) (r / (double) UINT32_MAX);
  return u < p ? 1 : 0;
}

// Forced-debug sampling quota (per minute)
static uint32_t g_debug_quota_per_minute = 120;
static uint64_t g_debug_window_start_ms  = 0;
static uint32_t g_debug_window_count     = 0;

bool tracing_debug_quota_try_consume(void) {
  uint64_t now = current_unix_ms();
  if (g_debug_window_start_ms == 0 || now - g_debug_window_start_ms >= 60000) {
    g_debug_window_start_ms = now;
    g_debug_window_count    = 0;
  }
  if (g_debug_window_count >= g_debug_quota_per_minute) return false;
  g_debug_window_count++;
  return true;
}

static void add_tag(trace_span_t* span, const char* key, const char* value_json) {
  if (!span || !key || !value_json) return;
  tag_entry_t* e = (tag_entry_t*) safe_calloc(1, sizeof(tag_entry_t));
  e->key         = strdup(key);
  e->value_json  = strdup(value_json);
  e->next        = span->tags;
  span->tags     = e;
}

// ---- Public API --------------------------------------------------------------------------------

void tracing_configure(bool enabled, const char* url, const char* service_name, double sample_rate) {
  g_tracer.enabled     = enabled ? 1 : 0;
  g_tracer.sample_rate = sample_rate < 0 ? 0 : (sample_rate > 1 ? 1 : sample_rate);
  safe_free(g_tracer.url);
  safe_free(g_tracer.service_name);
  g_tracer.url          = url ? strdup(url) : NULL;
  g_tracer.service_name = service_name ? strdup(service_name) : strdup("colibri-stateless");
}

bool tracing_is_enabled(void) {
  return g_tracer.enabled && g_tracer.url && *g_tracer.url;
}

trace_span_t* tracing_start_root(const char* name) {
  if (!tracing_is_enabled()) return NULL;
  if (!should_sample(g_tracer.sample_rate)) return NULL;
  trace_span_t* s = (trace_span_t*) safe_calloc(1, sizeof(trace_span_t));
  s->trace_id     = gen_trace_id();
  s->span_id      = gen_span_id();
  memset(&s->parent_id, 0, sizeof(s->parent_id));
  s->start_ms = current_unix_ms();
  s->sampled  = 1;
  s->name     = name ? strdup(name) : strdup("span");
  return s;
}

trace_span_t* tracing_start_root_forced(const char* name) {
  if (!tracing_is_enabled()) return NULL;
  trace_span_t* s = (trace_span_t*) safe_calloc(1, sizeof(trace_span_t));
  s->trace_id     = gen_trace_id();
  s->span_id      = gen_span_id();
  memset(&s->parent_id, 0, sizeof(s->parent_id));
  s->start_ms = current_unix_ms();
  s->sampled  = 1;
  s->name     = name ? strdup(name) : strdup("span");
  return s;
}

trace_span_t* tracing_start_root_with_b3(const char* name, const char* trace_id_hex, const char* parent_span_id_hex, int sampled) {
  if (!tracing_is_enabled()) return NULL;
  // If sampling requested false, drop
  if (!sampled) return NULL;
  trace_span_t* s = (trace_span_t*) safe_calloc(1, sizeof(trace_span_t));
  // Accept 16- or 32-hex trace id (Zipkin allows 64 or 128 bit)
  size_t th = trace_id_hex ? strlen(trace_id_hex) : 0;
  if (trace_id_hex && (th == 32 || th == 16)) {
    if (th == 32) {
      hex_to_bytes(trace_id_hex, (int) th, bytes(s->trace_id.bytes, sizeof(s->trace_id.bytes)));
    }
    else {
      // 64-bit provided -> left-pad to 128
      memset(s->trace_id.bytes, 0, sizeof(s->trace_id.bytes));
      hex_to_bytes(trace_id_hex, (int) th, bytes(s->trace_id.bytes + 8, 8));
    }
  }
  else {
    s->trace_id = gen_trace_id();
  }
  s->span_id = gen_span_id();
  memset(&s->parent_id, 0, sizeof(s->parent_id));
  if (parent_span_id_hex && strlen(parent_span_id_hex) == 16) {
    hex_to_bytes(parent_span_id_hex, 16, bytes(s->parent_id.bytes, sizeof(s->parent_id.bytes)));
  }
  s->start_ms = current_unix_ms();
  s->sampled  = 1;
  s->name     = name ? strdup(name) : strdup("span");
  return s;
}

trace_span_t* tracing_start_child(trace_span_t* parent, const char* name) {
  if (!parent) return NULL;
  if (!tracing_is_enabled()) return NULL;
  trace_span_t* s = (trace_span_t*) safe_calloc(1, sizeof(trace_span_t));
  s->trace_id     = parent->trace_id;
  s->span_id      = gen_span_id();
  s->parent_id    = parent->span_id;
  s->start_ms     = current_unix_ms();
  s->sampled      = parent->sampled;
  s->name         = name ? strdup(name) : strdup("span");
  if (!s->sampled) return NULL; // keep behavior consistent (no-op)
  return s;
}

trace_span_t* tracing_start_child_at(trace_span_t* parent, const char* name, uint64_t start_unix_ms) {
  if (!parent) return NULL;
  if (!tracing_is_enabled()) return NULL;
  trace_span_t* s = (trace_span_t*) safe_calloc(1, sizeof(trace_span_t));
  s->trace_id     = parent->trace_id;
  s->span_id      = gen_span_id();
  s->parent_id    = parent->span_id;
  s->start_ms     = start_unix_ms ? start_unix_ms : current_unix_ms();
  s->sampled      = parent->sampled;
  s->name         = name ? strdup(name) : strdup("span");
  if (!s->sampled) return NULL;
  return s;
}

void tracing_span_tag_str(trace_span_t* span, const char* key, const char* value) {
  if (!span || !value) return;
  buffer_t buf = {0};
  add_tag(span, key, bprintf(&buf, "\"%S\"", value));
  buffer_free(&buf);
}

void tracing_span_tag_i64(trace_span_t* span, const char* key, int64_t value) {
  if (!span) return;
  char     tmp[80];
  buffer_t buf = stack_buffer(tmp);
  // Zipkin v2 requires tag values to be strings
  add_tag(span, key, bprintf(&buf, "\"%l\"", (uint64_t) value));
}

void tracing_span_tag_f64(trace_span_t* span, const char* key, double value) {
  if (!span) return;
  // Zipkin v2 requires tag values to be strings
  char     tmp[80];
  buffer_t buf = stack_buffer(tmp);
  // Avoid locale issues, fixed precision
  add_tag(span, key, bprintf(&buf, "\"%f\"", value));
}

void tracing_span_tag_json(trace_span_t* span, const char* key, const char* value_json) {
  if (!span || !value_json) return;
  // Zipkin v2 tags must be strings; store JSON as escaped string
  buffer_t buf = {0};
  add_tag(span, key, bprintf(&buf, "\"%S\"", value_json));
  buffer_free(&buf);
}

const char* tracing_span_trace_id_hex(trace_span_t* span) {
  static char buf[33];
  if (!span) return NULL;
  bytes_to_hex(span->trace_id.bytes, sizeof(span->trace_id.bytes), buf);
  return buf;
}
const char* tracing_span_id_hex(trace_span_t* span) {
  static char buf[17];
  if (!span) return NULL;
  bytes_to_hex(span->span_id.bytes, sizeof(span->span_id.bytes), buf);
  return buf;
}
const char* tracing_span_parent_id_hex(trace_span_t* span) {
  static char buf[17];
  if (!span) return NULL;
  // If parent all zero, return empty string for Zipkin optional field
  int all_zero = 1;
  for (size_t i = 0; i < sizeof(span->parent_id.bytes); i++)
    if (span->parent_id.bytes[i] != 0) {
      all_zero = 0;
      break;
    }
  if (all_zero) return NULL;
  bytes_to_hex(span->parent_id.bytes, sizeof(span->parent_id.bytes), buf);
  return buf;
}

void tracing_inject_b3_headers(trace_span_t* span, struct curl_slist** headers) {
  if (!span || !headers) return;
  char        tbuf[64], sbuf[32], pbuf[32];
  const char* trace_hex  = tracing_span_trace_id_hex(span);
  const char* span_hex   = tracing_span_id_hex(span);
  const char* parent_hex = tracing_span_parent_id_hex(span);
  snprintf(tbuf, sizeof(tbuf), "X-B3-TraceId: %s", trace_hex ? trace_hex : "");
  snprintf(sbuf, sizeof(sbuf), "X-B3-SpanId: %s", span_hex ? span_hex : "");
  *headers = curl_slist_append(*headers, tbuf);
  *headers = curl_slist_append(*headers, sbuf);
  if (parent_hex && *parent_hex) {
    snprintf(pbuf, sizeof(pbuf), "X-B3-ParentSpanId: %s", parent_hex);
    *headers = curl_slist_append(*headers, pbuf);
  }
  *headers = curl_slist_append(*headers, span->sampled ? "X-B3-Sampled: 1" : "X-B3-Sampled: 0");
}

// ---- Export (Zipkin v2 JSON) -------------------------------------------------------------------

static void free_tags(tag_entry_t* t) {
  while (t) {
    tag_entry_t* n = t->next;
    safe_free(t->key);
    safe_free(t->value_json);
    safe_free(t);
    t = n;
  }
}

static void zipkin_serialize_span(buffer_t* out, trace_span_t* s) {
  // Zipkin v2 JSON span
  // { "traceId":"", "id":"", "parentId":"", "name":"", "timestamp":ns/1000, "duration":ns/1000, "tags":{...}, "localEndpoint":{"serviceName": "..."} }
  char trace_hex[33], id_hex[17], parent_hex[17];
  bytes_to_hex(s->trace_id.bytes, sizeof(s->trace_id.bytes), trace_hex);
  bytes_to_hex(s->span_id.bytes, sizeof(s->span_id.bytes), id_hex);
  int has_parent = 0;
  for (size_t i = 0; i < sizeof(s->parent_id.bytes); i++)
    if (s->parent_id.bytes[i] != 0) {
      has_parent = 1;
      break;
    }
  if (has_parent) bytes_to_hex(s->parent_id.bytes, sizeof(s->parent_id.bytes), parent_hex);

  uint64_t ts_us  = s->start_ms * 1000ull;
  uint64_t dur_us = (s->end_ms > s->start_ms) ? ((s->end_ms - s->start_ms) * 1000ull) : 0ull;

  bprintf(out, "{\"traceId\":\"%s\",\"id\":\"%s\"", trace_hex, id_hex);
  if (has_parent) bprintf(out, ",\"parentId\":\"%s\"", parent_hex);
  // name
  if (s->name) bprintf(out, ",\"name\":\"%S\"", s->name);
  // Use bprintf's %l (uint64) specifier; standard %llu would leak 'lu' into JSON
  bprintf(out, ",\"timestamp\":%l,\"duration\":%l", ts_us, dur_us);
  // localEndpoint
  if (g_tracer.service_name && *g_tracer.service_name)
    bprintf(out, ",\"localEndpoint\":{\"serviceName\":\"%S\"}", g_tracer.service_name);

  // tags
  if (s->tags) {
    bprintf(out, ",\"tags\":{");
    int first = 1;
    for (tag_entry_t* t = s->tags; t; t = t->next) {
      if (!first) bprintf(out, ",");
      first = 0;
      bprintf(out, "\"%S\":%s", t->key ? t->key : "", t->value_json ? t->value_json : "null");
    }
    bprintf(out, "}");
  }
  bprintf(out, "}");
}

static void export_batch_if_needed(int force) {
  if (!g_tracer.url || !*g_tracer.url) return;
  if (g_batch_count == 0) return;
  if (!force && g_batch_count < 8 && g_batch.data.len < 64 * 1024) return;

  // Close JSON array
  bprintf(&g_batch, "]");
  // Enqueue non-blocking send (transfer ownership)
  tracing_enqueue_body(&g_batch);
  g_batch.data.data = NULL;
  g_batch.data.len  = 0;
  g_batch_count     = 0;
}

void tracing_finish(trace_span_t* span) {
  if (!span || span->finished) return;
  span->finished = 1;
  span->end_ms   = current_unix_ms();
  if (span->sampled) {
    // Append to batch
    buffer_t tmp = {0};
    zipkin_serialize_span(&tmp, span);
    if (g_batch_count == 0)
      bprintf(&g_batch, "[%s", (char*) tmp.data.data);
    else
      bprintf(&g_batch, ",%s", (char*) tmp.data.data);
    g_batch_count++;
    // Log root span traceId for discoverability in Tempo
    int is_root = 1;
    for (size_t i = 0; i < sizeof(span->parent_id.bytes); i++) {
      if (span->parent_id.bytes[i] != 0) {
        is_root = 0;
        break;
      }
    }
    if (is_root) {
      const char* trace_hex = tracing_span_trace_id_hex(span);
      log_info("Tempo trace queued: traceId=%s name=\"%s\"", trace_hex ? trace_hex : "", span->name ? span->name : "");
    }
    buffer_free(&tmp);
    export_batch_if_needed(/*force=*/0);
  }
  free_tags(span->tags);
  safe_free(span->name);
  safe_free(span);
}

void tracing_finish_at(trace_span_t* span, uint64_t end_unix_ms) {
  if (!span || span->finished) return;
  span->finished = 1;
  span->end_ms   = end_unix_ms ? end_unix_ms : current_unix_ms();
  if (span->sampled) {
    buffer_t tmp = {0};
    zipkin_serialize_span(&tmp, span);
    if (g_batch_count == 0)
      bprintf(&g_batch, "[%s", (char*) tmp.data.data);
    else
      bprintf(&g_batch, ",%s", (char*) tmp.data.data);
    g_batch_count++;
    int is_root = 1;
    for (size_t i = 0; i < sizeof(span->parent_id.bytes); i++) {
      if (span->parent_id.bytes[i] != 0) {
        is_root = 0;
        break;
      }
    }
    if (is_root) {
      const char* trace_hex = tracing_span_trace_id_hex(span);
      log_info("Tempo trace queued: traceId=%s name=\"%s\"", trace_hex ? trace_hex : "", span->name ? span->name : "");
    }
    buffer_free(&tmp);
    export_batch_if_needed(/*force=*/0);
  }
  free_tags(span->tags);
  safe_free(span->name);
  safe_free(span);
}

void tracing_flush_now(void) {
  export_batch_if_needed(/*force=*/1);
}
