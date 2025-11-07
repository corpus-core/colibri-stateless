/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "tracing.h"

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
  char*              key;
  char*              value_json; // valid JSON value (quoted already if string)
  struct tag_entry*  next;
} tag_entry_t;

struct trace_span {
  trace_id_t   trace_id;
  span_id_t    span_id;
  span_id_t    parent_id;
  uint64_t     start_ns;
  uint64_t     end_ns;
  int          sampled; // 0/1
  char*        name;
  tag_entry_t* tags;
  int          finished;
};

typedef struct {
  int         enabled;
  double      sample_rate;
  char*       url;
  char*       service_name;
} tracer_t;

static tracer_t g_tracer = {0};

// ---- Utilities ---------------------------------------------------------------------------------

static uint64_t now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (uint64_t) ts.tv_sec * 1000000000ull + (uint64_t) ts.tv_nsec;
}

static void bytes_to_hex(const uint8_t* in, size_t len, char* out) {
  static const char* hex = "0123456789abcdef";
  for (size_t i = 0; i < len; i++) {
    out[i * 2 + 0] = hex[(in[i] >> 4) & 0x0F];
    out[i * 2 + 1] = hex[(in[i] >> 0) & 0x0F];
  }
  out[len * 2] = '\0';
}

static void gen_random_bytes(uint8_t* out, size_t len) {
  // Use libuv's secure random. Fallback: memset zero on error.
  if (uv_random(NULL, NULL, out, (int) len, 0) != 0) {
    memset(out, 0, len);
  }
}

static span_id_t gen_span_id(void) {
  span_id_t id;
  gen_random_bytes(id.bytes, sizeof(id.bytes));
  // Ensure non-zero
  int all_zero = 1;
  for (size_t i = 0; i < sizeof(id.bytes); i++)
    if (id.bytes[i] != 0) { all_zero = 0; break; }
  if (all_zero) id.bytes[sizeof(id.bytes) - 1] = 1;
  return id;
}

static trace_id_t gen_trace_id(void) {
  trace_id_t id;
  gen_random_bytes(id.bytes, sizeof(id.bytes));
  // Ensure non-zero
  int all_zero = 1;
  for (size_t i = 0; i < sizeof(id.bytes); i++)
    if (id.bytes[i] != 0) { all_zero = 0; break; }
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

static void add_tag(trace_span_t* span, const char* key, const char* value_json) {
  if (!span || !key || !value_json) return;
  tag_entry_t* e   = (tag_entry_t*) safe_calloc(1, sizeof(tag_entry_t));
  e->key           = strdup(key);
  e->value_json    = strdup(value_json);
  e->next          = span->tags;
  span->tags       = e;
}

// ---- Public API --------------------------------------------------------------------------------

void tracing_configure(bool enabled, const char* url, const char* service_name, double sample_rate) {
  g_tracer.enabled      = enabled ? 1 : 0;
  g_tracer.sample_rate  = sample_rate < 0 ? 0 : (sample_rate > 1 ? 1 : sample_rate);
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
  s->start_ns = now_ns();
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
  s->start_ns     = now_ns();
  s->sampled      = parent->sampled;
  s->name         = name ? strdup(name) : strdup("span");
  if (!s->sampled) return NULL; // keep behavior consistent (no-op)
  return s;
}

void tracing_span_tag_str(trace_span_t* span, const char* key, const char* value) {
  if (!span || !value) return;
  // JSON-escape minimal set: replace " with \"
  buffer_t buf = {0};
  bprintf(&buf, "\"");
  for (const char* p = value; *p; ++p) {
    if (*p == '\"') bprintf(&buf, "\\\"");
    else if (*p == '\\') bprintf(&buf, "\\\\");
    else if ((unsigned char) *p < 0x20) bprintf(&buf, "\\u%04x", (unsigned) (unsigned char) *p);
    else bprintf(&buf, "%c", *p);
  }
  bprintf(&buf, "\"");
  add_tag(span, key, (const char*) buf.data.data);
  buf.data.data = NULL;
  buffer_free(&buf);
}

void tracing_span_tag_i64(trace_span_t* span, const char* key, int64_t value) {
  if (!span) return;
  char tmp[64];
  snprintf(tmp, sizeof(tmp), "%lld", (long long) value);
  add_tag(span, key, tmp);
}

void tracing_span_tag_f64(trace_span_t* span, const char* key, double value) {
  if (!span) return;
  char tmp[64];
  // Avoid locale issues
  snprintf(tmp, sizeof(tmp), "%.6f", value);
  add_tag(span, key, tmp);
}

void tracing_span_tag_json(trace_span_t* span, const char* key, const char* value_json) {
  if (!span || !value_json) return;
  add_tag(span, key, value_json);
}

const char* tracing_span_trace_id_hex(trace_span_t* span) {
  static __thread char buf[33];
  if (!span) return NULL;
  bytes_to_hex(span->trace_id.bytes, sizeof(span->trace_id.bytes), buf);
  return buf;
}
const char* tracing_span_id_hex(trace_span_t* span) {
  static __thread char buf[17];
  if (!span) return NULL;
  bytes_to_hex(span->span_id.bytes, sizeof(span->span_id.bytes), buf);
  return buf;
}
const char* tracing_span_parent_id_hex(trace_span_t* span) {
  static __thread char buf[17];
  if (!span) return NULL;
  // If parent all zero, return empty string for Zipkin optional field
  int all_zero = 1;
  for (size_t i = 0; i < sizeof(span->parent_id.bytes); i++)
    if (span->parent_id.bytes[i] != 0) { all_zero = 0; break; }
  if (all_zero) return NULL;
  bytes_to_hex(span->parent_id.bytes, sizeof(span->parent_id.bytes), buf);
  return buf;
}

void tracing_inject_b3_headers(trace_span_t* span, struct curl_slist** headers) {
  if (!span || !headers) return;
  char tbuf[64], sbuf[32], pbuf[32];
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
    if (s->parent_id.bytes[i] != 0) { has_parent = 1; break; }
  if (has_parent) bytes_to_hex(s->parent_id.bytes, sizeof(s->parent_id.bytes), parent_hex);

  uint64_t ts_us  = s->start_ns / 1000ull;
  uint64_t dur_us = (s->end_ns > s->start_ns) ? ((s->end_ns - s->start_ns) / 1000ull) : 0ull;

  bprintf(out, "{");
  bprintf(out, "\"traceId\":\"%s\",\"id\":\"%s\"", trace_hex, id_hex);
  if (has_parent) bprintf(out, ",\"parentId\":\"%s\"", parent_hex);
  // name
  if (s->name) {
    buffer_t esc = {0};
    bprintf(&esc, "\"");
    for (const char* p = s->name; *p; ++p) {
      if (*p == '\"') bprintf(&esc, "\\\"");
      else if (*p == '\\') bprintf(&esc, "\\\\");
      else if ((unsigned char) *p < 0x20) bprintf(&esc, "\\u%04x", (unsigned) (unsigned char) *p);
      else bprintf(&esc, "%c", *p);
    }
    bprintf(&esc, "\"");
    bprintf(out, ",\"name\":%s", (char*) esc.data.data);
    esc.data.data = NULL;
    buffer_free(&esc);
  }
  bprintf(out, ",\"timestamp\":%llu,\"duration\":%llu", (unsigned long long) ts_us, (unsigned long long) dur_us);
  // localEndpoint
  if (g_tracer.service_name && *g_tracer.service_name) {
    buffer_t esc = {0};
    bprintf(&esc, "\"");
    for (const char* p = g_tracer.service_name; *p; ++p) {
      if (*p == '\"') bprintf(&esc, "\\\"");
      else if (*p == '\\') bprintf(&esc, "\\\\");
      else if ((unsigned char) *p < 0x20) bprintf(&esc, "\\u%04x", (unsigned) (unsigned char) *p);
      else bprintf(&esc, "%c", *p);
    }
    bprintf(&esc, "\"");
    bprintf(out, ",\"localEndpoint\":{\"serviceName\":%s}", (char*) esc.data.data);
    esc.data.data = NULL;
    buffer_free(&esc);
  }
  // tags
  if (s->tags) {
    bprintf(out, ",\"tags\":{");
    int first = 1;
    for (tag_entry_t* t = s->tags; t; t = t->next) {
      if (!first) bprintf(out, ",");
      first = 0;
      // keys are strings
      buffer_t esc = {0};
      bprintf(&esc, "\"");
      for (const char* p = t->key; *p; ++p) {
        if (*p == '\"') bprintf(&esc, "\\\"");
        else if (*p == '\\') bprintf(&esc, "\\\\");
        else if ((unsigned char) *p < 0x20) bprintf(&esc, "\\u%04x", (unsigned) (unsigned char) *p);
        else bprintf(&esc, "%c", *p);
      }
      bprintf(&esc, "\"");
      bprintf(out, "%s:%s", (char*) esc.data.data, t->value_json);
      esc.data.data = NULL;
      buffer_free(&esc);
    }
    bprintf(out, "}");
  }
  bprintf(out, "}");
}

static void export_single_span(trace_span_t* s) {
  if (!g_tracer.url || !*g_tracer.url) return;

  buffer_t body = {0};
  bprintf(&body, "[");
  zipkin_serialize_span(&body, s);
  bprintf(&body, "]");

  CURL* curl = curl_easy_init();
  if (!curl) {
    buffer_free(&body);
    return;
  }
  struct curl_slist* headers = NULL;
  headers                    = curl_slist_append(headers, "Content-Type: application/json");
  curl_easy_setopt(curl, CURLOPT_URL, g_tracer.url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data.data ? (char*) body.data.data : "");
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long) body.data.len);
  // Keep timeouts short to avoid blocking
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 150L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 100L);
  // Best-effort: ignore result
  CURLcode rc = curl_easy_perform(curl);
  if (rc != CURLE_OK) {
    // Silent failure to avoid noisy logs; enable if needed:
    // log_warn("Tracing export failed: %s", curl_easy_strerror(rc));
  }
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  buffer_free(&body);
}

void tracing_finish(trace_span_t* span) {
  if (!span) return;
  if (span->finished) return;
  span->finished = 1;
  span->end_ns   = now_ns();
  if (span->sampled) {
    export_single_span(span);
  }
  free_tags(span->tags);
  safe_free(span->name);
  safe_free(span);
}

void tracing_flush_now(void) {
  // No-op in the simple immediate-export implementation.
}


