/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

struct curl_slist;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Minimal tracing API for Zipkin v2 over HTTP with b3 propagation.
 * This module is intentionally self-contained within src/server.
 */

typedef struct trace_span trace_span_t;

/**
 * Configure tracer. Call early during server setup.
 * If enabled=false, all functions become no-ops and return NULL where applicable.
 *
 * @param enabled whether tracing is enabled
 * @param url Zipkin v2 ingest endpoint (e.g. http://localhost:9411/api/v2/spans)
 * @param service_name logical service name (e.g. "colibri-stateless")
 * @param sample_rate probability 0..1 (values outside are clamped)
 */
void tracing_configure(bool enabled, const char* url, const char* service_name, double sample_rate);

/**
 * Returns true when tracing is enabled and sampling didn't decide to drop everything.
 */
bool tracing_is_enabled(void);

/**
 * Start a new root span.
 * @param name span name
 * @return span or NULL when disabled/not sampled
 */
trace_span_t* tracing_start_root(const char* name);
trace_span_t* tracing_start_root_with_b3(const char* name, const char* trace_id_hex, const char* parent_span_id_hex, int sampled);
// Start a root span unconditionally (ignores sampling rate)
trace_span_t* tracing_start_root_forced(const char* name);
// Try to consume one debug trace token for the current minute window (rate limiting forced debug traces)
bool tracing_debug_quota_try_consume(void);

/**
 * Start a child span below parent.
 * @param parent parent span
 * @param name span name
 * @return span or NULL when disabled/not sampled (propagates parent's sampled decision)
 */
trace_span_t* tracing_start_child(trace_span_t* parent, const char* name);

/**
 * Add string/integer tag to a span. Safe to call with NULL span (no-op).
 * Values are copied.
 */
void tracing_span_tag_str(trace_span_t* span, const char* key, const char* value);
void tracing_span_tag_i64(trace_span_t* span, const char* key, int64_t value);
void tracing_span_tag_f64(trace_span_t* span, const char* key, double value);

/**
 * Add a pre-formatted JSON fragment as tag value (must be valid JSON value).
 * Example: key="servers.weights", value_json="[ {\"name\":\"a\",\"weight\":1.0} ]"
 */
void tracing_span_tag_json(trace_span_t* span, const char* key, const char* value_json);

/**
 * Finish span and enqueue for export (best-effort).
 * Safe to call with NULL span (no-op). Spans must be finished exactly once.
 */
void tracing_finish(trace_span_t* span);

/**
 * Inject b3 headers for an outgoing HTTP request. Uses span as current span.
 * The headers are appended to the provided list.
 * Safe to call with NULL span (no-op).
 */
void tracing_inject_b3_headers(trace_span_t* span, struct curl_slist** headers);

/**
 * Expose IDs for diagnostics. Returns hex strings owned by the span (valid until finish).
 * When span is NULL, returns NULLs.
 */
const char* tracing_span_trace_id_hex(trace_span_t* span);
const char* tracing_span_id_hex(trace_span_t* span);
const char* tracing_span_parent_id_hex(trace_span_t* span);

/**
 * Flush pending spans immediately (synchronous, short timeout). Optional.
 */
void tracing_flush_now(void);

#ifdef __cplusplus
}
#endif
