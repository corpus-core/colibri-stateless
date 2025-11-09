/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "cache.h"
#include "logger.h"
#include "server.h"
#include <stddef.h> // Added for offsetof
#include <string.h>
#ifdef _WIN32
#include "util/win_compat.h"
#endif
#ifdef TEST
#include "util/bytes.h"
#include "util/crypto.h"
#endif

// Provide strnstr implementation if it's not available (e.g., on non-BSD/non-GNU systems)
#ifndef HAVE_STRNSTR
static char* c4_strnstr(const char* haystack, const char* needle, size_t len) {
  size_t needle_len;

  if (!needle || *needle == '\0') {
    return (char*) haystack; // Empty needle matches immediately
  }
  needle_len = strlen(needle);
  if (needle_len == 0) {
    return (char*) haystack; // Also empty
  }
  if (needle_len > len) {
    return NULL; // Needle is longer than the search length
  }

  // Iterate up to the last possible starting position within len
  for (size_t i = 0; i <= len - needle_len; ++i) {
    // Stop if we hit the end of the haystack string itself
    if (haystack[i] == '\0') {
      break;
    }
    // Compare the needle at the current position
    if (strncmp(&haystack[i], needle, needle_len) == 0) {
      return (char*) &haystack[i]; // Found match
    }
  }

  return NULL; // Not found
}
// Map to our local implementation when the platform libc doesn't provide strnstr
#define strnstr c4_strnstr
#endif

// container_of macro to get the pointer to the containing struct
#define container_of(ptr, type, member) ((type*) ((char*) (ptr) - offsetof(type, member)))

typedef struct pending_request {
  single_request_t*       request;
  struct pending_request* next;
  struct pending_request* same_requests;
} pending_request_t;

static pending_request_t* pending_requests = NULL;
static CURLM*             multi_handle;
static CURLSH*            g_curl_share    = NULL;
static mc_t*              memcache_client = NULL;
const char*               CURL_METHODS[]  = {"GET", "POST", "PUT", "DELETE"};

// Tracing helper: annotate selection, weights and unsupported methods for a request attempt
static void c4_tracing_annotate_attempt(single_request_t* r, server_list_t* servers, int selected_index, const char* base_url) {
  if (!tracing_is_enabled() || !r || !r->parent || !r->parent->trace_root) return;
  int         level       = r->parent->client ? r->parent->client->trace_level : TRACE_LEVEL_MIN;
  const char* method_name = CURL_METHODS[r->req->method];
  const char* host_name   = c4_extract_server_name(base_url);
  // Build compact request description similar to c4_req_info(), but without ANSI colors
  const char* desc_colored = c4_req_info(r->req->type, r->req->url, r->req->payload);
  char        desc[512];
  // strip ANSI escape codes
  {
    size_t di = 0;
    for (const char* p = desc_colored; *p && di + 1 < sizeof(desc);) {
      if (*p == '\x1b') {
        // skip until 'm' or end
        while (*p && *p != 'm') p++;
        if (*p == 'm') p++;
        continue;
      }
      desc[di++] = *p++;
    }
    desc[di] = '\0';
  }
  char span_name[512];
  sbprintf(span_name, "HTTP %s %s | %s", method_name ? method_name : "REQ", host_name ? host_name : "", desc);
  r->attempt_span = tracing_start_child(r->parent->trace_root, span_name);
  if (!r->attempt_span) return;
  // Selected server and client type
  tracing_span_tag_str(r->attempt_span, "server.selected", host_name ? host_name : "");
  tracing_span_tag_i64(r->attempt_span, "client_type", (int64_t) servers->client_types[selected_index]);
  tracing_span_tag_i64(r->attempt_span, "exclude.mask", (int64_t) r->req->node_exclude_mask);
  if (servers->health_stats)
    tracing_span_tag_i64(r->attempt_span, "last_block", (int64_t) servers->health_stats[selected_index].latest_block);

  buffer_t excluded_methods = {0};
  if (servers[selected_index].health_stats) {
    for (method_support_t* m = servers[selected_index].health_stats->unsupported_methods; m; m = m->next) {
      if (!m->is_supported) bprintf(&excluded_methods, "%s%s", m->method_name, m->next ? "," : "");
    }
  }
  tracing_span_tag_str(r->attempt_span, "exclude.methods", excluded_methods.data.len > 0 ? buffer_as_string(&excluded_methods) : "-");
  buffer_free(&excluded_methods);

  // Weights of all servers
  if (level == TRACE_LEVEL_DEBUG && servers && servers->health_stats && servers->urls) {
    buffer_t w = {0};
    bprintf(&w, "[");
    for (size_t wi = 0; wi < servers->count; wi++) {
      const char* n = c4_extract_server_name(servers->urls[wi]);
      if (wi > 0) bprintf(&w, ",");
      // {"name":"...", "weight":X.XXXXXX}
      bprintf(&w, "{\"name\":\"%S\",\"weight\":%f}", n ? n : "", servers->health_stats[wi].weight);
    }
    bprintf(&w, "]");
    tracing_span_tag_json(r->attempt_span, "servers.weights", (char*) w.data.data);
    w.data.data = NULL;
    buffer_free(&w);
    // Unsupported methods per server
    buffer_t um = {0};
    bprintf(&um, "{");
    int first_server = 1;
    for (size_t si = 0; si < servers->count; si++) {
      method_support_t* m = servers->health_stats[si].unsupported_methods;
      if (!m) continue;
      if (!first_server) bprintf(&um, ",");
      first_server  = 0;
      const char* n = c4_extract_server_name(servers->urls[si]);
      bprintf(&um, "\"%S\":[", n ? n : "");
      int first_m = 1;
      while (m) {
        if (!m->is_supported) {
          if (!first_m) bprintf(&um, ",");
          first_m = 0;
          bprintf(&um, "\"%S\"", m->method_name ? m->method_name : "");
        }
        m = m->next;
      }
      bprintf(&um, "]");
    }
    bprintf(&um, "}");
    tracing_span_tag_json(r->attempt_span, "servers.unsupported_methods", (char*) um.data.data);
    um.data.data = NULL;
    buffer_free(&um);
  }
  // Cache state best-effort (will be updated on completion too)
  tracing_span_tag_str(r->attempt_span, "cache", r->cached ? "hit" : "miss");
}

#ifdef TEST
// Test hook for URL rewriting (file:// mocking)
// ONLY compiled in test builds (-DTEST=1)
// This is a security risk in production!
char* (*c4_test_url_rewriter)(const char* url, const char* payload) = NULL;

// Generate deterministic filename for mock/recorded responses
// Returns: allocated string "test_data_dir/server/test_name/host_hash.json"
// Caller must free the returned string
char* c4_file_mock_get_filename(const char* host, const char* url,
                                const char* payload, const char* test_name) {
  if (!test_name) return NULL;

  // Create hash of host + url + payload
  buffer_t buf = {0};
  bprintf(&buf, "%s:%s:%s",
          host ? host : "",
          url ? url : "",
          payload ? payload : "");

  bytes32_t hash;
  sha256(buf.data, hash);

  // Create filename: TESTDATA_DIR/server/test_name/host_hash.json
  buffer_t filename = {0};
  bprintf(&filename, "%s/server/%s/%s_%x.json",
          TESTDATA_DIR,
          test_name,
          host ? host : "default",
          bytes(hash, 16)); // First 16 bytes of hash

  buffer_free(&buf);
  char* result       = (char*) filename.data.data;
  filename.data.data = NULL; // Prevent buffer_free from freeing it
  return result;
}

// Helper to record responses when http_server.test_dir is set
// Writes responses to TESTDATA_DIR/server/<test_dir>/<host>_<hash>.json
static void c4_record_test_response(const char* url, const char* payload,
                                    bytes_t response, long http_code) {
  if (!http_server.test_dir || !url) return;

  // Extract host from URL (e.g., "http://host:port/path" -> "host")
  char*       host       = NULL;
  const char* host_start = strstr(url, "://");
  if (host_start) {
    host_start += 3; // Skip "://"
    const char* host_end = strchr(host_start, ':');
    if (!host_end) host_end = strchr(host_start, '/');
    if (host_end) {
      size_t len = host_end - host_start;
      host       = strndup(host_start, len);
    }
    else {
      host = strdup(host_start);
    }
  }

  // Use central filename generation function
  char* filename = c4_file_mock_get_filename(host, url, payload, http_server.test_dir);
  if (!filename) {
    free(host);
    return;
  }

  // Ensure directory exists
  char* last_slash = strrchr(filename, '/');
  if (last_slash) {
    *last_slash = '\0';
    // Simple mkdir -p equivalent
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "mkdir -p %s", filename);
    system(tmp);
    *last_slash = '/';
  }

  // Write response to file
  FILE* f = fopen(filename, "w");
  if (f) {
    fprintf(f, "# HTTP %ld\n", http_code);
    fprintf(f, "# URL: %s\n", url);
    fprintf(f, "# Host: %s\n", host ? host : "");
    fprintf(f, "# Payload: %s\n", payload ? payload : "");
    if (response.data && response.len > 0) {
      fwrite(response.data, 1, response.len, f);
    }
    fclose(f);
    log_info("[RECORD] %s -> %s", host ? host : "unknown", filename);
  }
  else {
    log_error("[RECORD] Failed to write %s", filename);
  }

  free(filename);
  free(host);
}

// Parse mock file response (file:// URLs)
// Mock files have format:
//   # HTTP 200
//   # URL: ...
//   # Host: ...
//   # Payload: ...
//   <actual response data>
// Returns: http_code (or 0 if not found), modifies buffer to contain only response data
static long parse_mock_response(buffer_t* buffer) {
  if (!buffer || !buffer->data.data || buffer->data.len == 0) return 0;

  long  http_code   = 200; // Default to 200 if not specified
  char* content     = (char*) buffer->data.data;
  char* content_end = content + buffer->data.len;
  char* line_start  = content;

  // Parse header lines (starting with #)
  while (line_start < content_end && *line_start == '#') {
    char* line_end = strchr(line_start, '\n');
    if (!line_end) line_end = content_end;

    // Check for HTTP status line: "# HTTP 200"
    if (strncmp(line_start, "# HTTP ", 7) == 0) {
      http_code = atol(line_start + 7);
    }

    // Move to next line
    line_start = line_end;
    if (line_start < content_end && *line_start == '\n') line_start++;
  }

  // Calculate actual response data (skip headers)
  size_t header_size = line_start - content;
  if (header_size > 0 && header_size < buffer->data.len) {
    // Move response data to beginning of buffer
    size_t response_len = buffer->data.len - header_size;
    memmove(buffer->data.data, line_start, response_len);
    buffer->data.len                = response_len;
    buffer->data.data[response_len] = '\0'; // Null terminate
  }

  return http_code;
}

#endif

static server_list_t eth_rpc_servers     = {0};
static server_list_t beacon_api_servers  = {0};
static server_list_t prover_servers      = {0};
static server_list_t checkpointz_servers = {0};

static void cache_response(single_request_t* r);
static void trigger_uncached_curl_request(void* data, char* value, size_t value_len);

// Helper function to extract RPC method name from request payload
static char* extract_rpc_method(data_request_t* req) {
  if (!req || req->type != C4_DATA_TYPE_ETH_RPC || !req->payload.data || req->payload.len == 0) return NULL;

  json_t json = json_get(json_parse((char*) req->payload.data), "method");
  if (json.type == JSON_TYPE_STRING) {
    return json_as_string(json, NULL);
  }
  return NULL;
}

// Helper: Extract requested block number for known methods; sets out_has_block when parsed
static void extract_requested_block_for_method(data_request_t* req, const char* rpc_method, uint64_t* out_block, bool* out_has_block) {
  if (!req || !rpc_method || !out_block || !out_has_block) return;
  *out_block     = 0;
  *out_has_block = false;
  if (!req->payload.data || req->payload.len == 0) return;

  json_t root   = json_parse((char*) req->payload.data);
  json_t params = json_get(root, "params");
  if (params.type != JSON_TYPE_ARRAY) return;

  // debug_traceCall / eth_call: block tag at index 1
  if (strcmp(rpc_method, "debug_traceCall") == 0 || strcmp(rpc_method, "eth_call") == 0) {
    json_t tag = json_at(params, 1);
    if (tag.type != JSON_TYPE_NOT_FOUND) {
      *out_block     = json_as_uint64(tag);
      *out_has_block = *out_block > 0;
    }
    return;
  }

  // eth_getProof: block tag at index 2
  if (strcmp(rpc_method, "eth_getProof") == 0) {
    json_t tag = json_at(params, 2);
    if (tag.type != JSON_TYPE_NOT_FOUND) {
      *out_block     = json_as_uint64(tag);
      *out_has_block = *out_block > 0;
    }
    return;
  }

  // eth_getBlockReceipts: block tag at index 0
  if (strcmp(rpc_method, "eth_getBlockReceipts") == 0) {
    json_t tag = json_at(params, 0);
    if (tag.type != JSON_TYPE_NOT_FOUND) {
      *out_block     = json_as_uint64(tag);
      *out_has_block = *out_block > 0;
    }
    return;
  }
}

// Context structure to associate uv_poll_t with CURL easy handle
typedef struct {
  uv_poll_t     poll_handle;
  CURL*         easy_handle;
  curl_socket_t socket;             // Store socket descriptor for cross-platform access
  bool          is_done_processing; // Flag set by handle_curl_events
} curl_poll_context_t;

// Custom close callback for uv_poll_t handles
static void cleanup_easy_handle_and_context(uv_handle_t* handle) {
  if (!handle) return;
  // Use container_of to get the context struct from the poll_handle pointer
  curl_poll_context_t* context = container_of(handle, curl_poll_context_t, poll_handle);
  if (context) {
    // Only cleanup easy handle if CURLMSG_DONE was processed
    if (context->is_done_processing && context->easy_handle) {
      // --- START CHANGE ---
      // Comment out debug print
      // fprintf(stderr, "DEBUG: Cleaning up easy handle %p in uv_close callback\n", context->easy_handle);
      // --- END CHANGE ---
      curl_easy_cleanup(context->easy_handle);
      context->easy_handle = NULL; // Prevent double free if called unexpectedly
    }
    else if (!context->is_done_processing && context->easy_handle) {
      // --- START CHANGE ---
      // Comment out this warning as cleanup now happens reliably in CURLMSG_DONE handler
      // fprintf(stderr, "WARNING: uv_close callback invoked for easy handle %p BEFORE CURLMSG_DONE was processed. Easy handle NOT cleaned up here.\n", context->easy_handle);
      // --- END CHANGE ---
      // This might indicate a potential leak if CURLMSG_DONE never arrives for this handle.
    }
    // Free the context struct itself
    // --- START CHANGE ---
    // Comment out debug print
    // fprintf(stderr, "DEBUG: Freeing poll context %p (handle %p)\n", context, handle);
    // --- END CHANGE ---
    safe_free(context);
  }
  else {
    log_warn("cleanup_easy_handle_and_context called with handle not part of a context?");
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
  if ((in->url == NULL) != (pending->url == NULL)) return false;
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
  pending_request_t* new_request = (pending_request_t*) safe_calloc(1, sizeof(pending_request_t));
  new_request->request           = req;
  new_request->next              = pending_requests;
  pending_requests               = new_request;
}
static void pending_add_to_same_requests(pending_request_t* pending, single_request_t* req) {
  pending_request_t* new_request = (pending_request_t*) safe_calloc(1, sizeof(pending_request_t));
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
      safe_free(current);
      return;
    }
    prev    = current;
    current = current->next;
  }
}
static void call_callback_if_done(request_t* req) {
  for (size_t i = 0; i < req->request_count; i++) {
    if (c4_state_is_pending(req->requests[i].req)) return;                    // we are not done yet if one is still pending
    if (!req->requests[i].end_time) req->requests[i].end_time = current_ms(); // set the end time if it's not set yet
  }

  // now handle metrics
  uint8_t  tmp[1024];
  buffer_t buffer = stack_buffer(tmp);

  for (size_t i = 0; i < req->request_count; i++) {
    single_request_t* r      = req->requests + i;
    char*             method = r->req->url;
    if (r->req->type == C4_DATA_TYPE_ETH_RPC) {
      json_t json = json_get(json_parse((char*) r->req->payload.data), "method");
      if (json.type == JSON_TYPE_STRING)
        method = json_as_string(json, &buffer);
    }
    if (method)
      c4_metrics_add_request(
          r->req->type,
          method, r->req->response.len,
          r->end_time - r->start_time,
          r->req->error == NULL, r->cached);
  }

  // and continue with the callback
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

    single_request_t*  r         = NULL;
    CURLcode           res       = curl_easy_getinfo(easy, CURLINFO_PRIVATE, &r);
    pending_request_t* pending   = pending_find(r);
    long               http_code = 0;
    if (msg->data.result == CURLE_OK) curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &http_code);

#ifdef TEST
    // For file:// URLs (mock responses), parse the HTTP code from the file content
    if (msg->data.result == CURLE_OK && http_code == 0) {
      const char* url = r->url ? r->url : r->req->url;
      if (url && strncmp(url, "file://", 7) == 0) {
        http_code = parse_mock_response(&r->buffer);
        log_info("   [mock ] Parsed mock response from %s: HTTP %l, %d bytes",
                 url, (uint64_t) http_code, (uint32_t) r->buffer.data.len);
      }
    }
#endif

    server_list_t* servers           = c4_get_server_list(r->req->type);
    r->end_time                      = current_ms();
    uint64_t           response_time = r->end_time - r->start_time;
    c4_response_type_t response_type = c4_classify_response(http_code,
                                                            r->url ? r->url : r->req->url,
                                                            r->buffer.data,
                                                            r->req); // Classify the response type

    // Collect libcurl-level metrics
    {
      long   http_version = 0;
      long   num_connects = 0;
      double connect_time = 0.0;
      double app_time     = 0.0;
      curl_easy_getinfo(easy, CURLINFO_HTTP_VERSION, &http_version);
      curl_easy_getinfo(easy, CURLINFO_NUM_CONNECTS, &num_connects);
      curl_easy_getinfo(easy, CURLINFO_CONNECT_TIME, &connect_time);
      curl_easy_getinfo(easy, CURLINFO_APPCONNECT_TIME, &app_time);
      http_server.curl.total_requests++;
      http_server.curl.total_connects += (uint64_t) num_connects;
      // Portable Reuse-Heuristik: wenn keine neuen Connects in diesem Transfer, wurde die Verbindung wiederverwendet
      if (num_connects == 0) http_server.curl.reused_connections_total++;
      if (http_version >= CURL_HTTP_VERSION_2_0)
        http_server.curl.http2_requests_total++;
      else
        http_server.curl.http1_requests_total++;
      if (num_connects > 0 && app_time > 0.0) http_server.curl.tls_handshakes_total++;
      const double w = 0.1;
      if (connect_time > 0.0)
        http_server.curl.avg_connect_time_ms = (1.0 - w) * http_server.curl.avg_connect_time_ms + w * (connect_time * 1000.0);
      if (app_time > 0.0)
        http_server.curl.avg_appconnect_time_ms = (1.0 - w) * http_server.curl.avg_appconnect_time_ms + w * (app_time * 1000.0);
    }

    // Update server health based on response type
    // Method not supported is not a server health issue, so we treat it as successful for health tracking
    bool health_success = (response_type == C4_RESPONSE_SUCCESS ||
                           response_type == C4_RESPONSE_ERROR_USER ||
                           response_type == C4_RESPONSE_ERROR_METHOD_NOT_SUPPORTED);
    // Update via unified end hook (also adjusts AIMD and method stats)
    {
      char* rpc_method = extract_rpc_method(r->req);
      c4_on_request_end(servers, r->req->response_node_index, response_time,
                        health_success, response_type, http_code,
                        rpc_method, NULL);
      if (rpc_method) safe_free(rpc_method);
    }

#ifdef TEST
    // Record response if test_dir is set
    c4_record_test_response(r->url ? r->url : r->req->url,
                            r->req->payload.data ? (char*) r->req->payload.data : NULL,
                            r->buffer.data,
                            http_code);
#endif

    const char* server_name = c4_extract_server_name(servers->urls[r->req->response_node_index]);
    bytes_t     response    = c4_request_fix_response(r->buffer.data, r, servers->client_types[r->req->response_node_index]);

    if (response_type == C4_RESPONSE_SUCCESS && response.data) {
      if (r->attempt_span) {
        tracing_span_tag_str(r->attempt_span, "result", "success");
        tracing_span_tag_i64(r->attempt_span, "http.status", (int64_t) http_code);
        tracing_span_tag_i64(r->attempt_span, "bytes", (int64_t) r->buffer.data.len);
        tracing_span_tag_i64(r->attempt_span, "duration_ms", (int64_t) response_time);
        tracing_span_tag_str(r->attempt_span, "server.name", server_name ? server_name : "");
        tracing_span_tag_str(r->attempt_span, "cache", r->cached ? "hit" : "miss");
        tracing_finish(r->attempt_span);
        r->attempt_span = NULL;
      }
      log_info(GREEN("   [curl ]") " %s -> OK %d bytes, %d ms from " BLUE("%s"),
               c4_req_info(r->req->type, r->req->url, r->req->payload),
               r->buffer.data.len, (int) response_time, server_name);
      r->req->response = response; // set the response
      cache_response(r);           // and write to cache

      r->buffer = (buffer_t) {0}; // reset the buffer, so we don't clean up the data
    }
    else if (response_type == C4_RESPONSE_ERROR_USER && r->req->type == C4_DATA_TYPE_ETH_RPC && response.data) {
      if (r->attempt_span) {
        tracing_span_tag_str(r->attempt_span, "result", "user_error");
        tracing_span_tag_i64(r->attempt_span, "http.status", (int64_t) http_code);
        tracing_span_tag_i64(r->attempt_span, "bytes", (int64_t) r->buffer.data.len);
        tracing_span_tag_i64(r->attempt_span, "duration_ms", (int64_t) response_time);
        tracing_span_tag_str(r->attempt_span, "server.name", server_name ? server_name : "");
        if (response.data) tracing_span_tag_str(r->attempt_span, "error.body", (char*) response.data);
        tracing_finish(r->attempt_span);
        r->attempt_span = NULL;
      }
      log_warn(YELLOW("   [curl ]") " %s -> USER ERROR %d bytes (%s) : from " BLUE("%s"),
               c4_req_info(r->req->type, r->req->url, r->req->payload),
               r->buffer.data.len,
               response.data ? (char*) response.data : "",
               server_name);
      // For JSON-RPC user errors, set the response so application logic can extract detailed error messages
      r->req->response = response;       // set the response with JSON-RPC error details
      r->buffer        = (buffer_t) {0}; // reset the buffer, so we don't clean up the data

      // Mark as non-retryable to avoid unnecessary retries
      log_warn(YELLOW("   [curl ]") " JSON-RPC user error - marking request as non-retryable");
      r->req->node_exclude_mask = (1 << servers->count) - 1; // Set all bits
    }
    else if (response_type == C4_RESPONSE_ERROR_METHOD_NOT_SUPPORTED) {
      if (r->attempt_span) {
        // Treat "method not supported" as an error for tracing purposes
        tracing_span_tag_str(r->attempt_span, "result", "error");
        tracing_span_tag_i64(r->attempt_span, "http.status", (int64_t) http_code);
        tracing_span_tag_i64(r->attempt_span, "bytes", (int64_t) r->buffer.data.len);
        tracing_span_tag_i64(r->attempt_span, "duration_ms", (int64_t) response_time);
        tracing_span_tag_str(r->attempt_span, "server.name", server_name ? server_name : "");
        // Prefer server response text if present (NULL-terminated), else fallback
        const char* errtxt = (r->buffer.data.data && r->buffer.data.len > 0)
                                 ? (char*) r->buffer.data.data
                                 : "method_not_supported";
        tracing_span_tag_str(r->attempt_span, "error", errtxt);
        // we add the list of

        tracing_finish(r->attempt_span);
        r->attempt_span = NULL;
      }
      // Don't set response or mark as completely failed - let retry logic handle it
      if (!r->req->error) {
        r->req->error = strdup((r->buffer.data.data && r->buffer.data.len > 0)
                                   ? (char*) r->buffer.data.data
                                   : "Method not supported");
      }
      log_warn(YELLOW("   [curl ]") " %s -> " BOLD("METHOD NOT SUPPORTED : %s") " from " BLUE("%s"),
               c4_req_info(r->req->type, r->req->url, r->req->payload),
               r->req->error ? r->req->error : "",
               server_name);
    }
    else {
      char* effective_url = NULL;
      curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &effective_url);
      if (!r->req->error) r->req->error = bprintf(NULL, "(%d) %s : %s", (uint32_t) http_code, msg->data.result == CURLE_OK ? "" : curl_easy_strerror(msg->data.result), bprintf(&r->buffer, " ")); // create error message
      if (r->attempt_span) {
        tracing_span_tag_str(r->attempt_span, "result", "error");
        tracing_span_tag_i64(r->attempt_span, "http.status", (int64_t) http_code);
        tracing_span_tag_i64(r->attempt_span, "bytes", (int64_t) r->buffer.data.len);
        tracing_span_tag_i64(r->attempt_span, "duration_ms", (int64_t) response_time);
        tracing_span_tag_str(r->attempt_span, "server.name", server_name ? server_name : "");
        tracing_span_tag_str(r->attempt_span, "error", r->req->error ? r->req->error : "");
        tracing_finish(r->attempt_span);
        r->attempt_span = NULL;
      }
      log_warn(YELLOW("   [curl ]") " %s -> ERROR : " BOLD("%s") " : from " BLUE("%s"),
               c4_req_info(r->req->type, r->req->url, r->req->payload),
               r->req->error,
               server_name);

      // For non-JSON-RPC user errors, mark as non-retryable to avoid unnecessary retries
      if (response_type == C4_RESPONSE_ERROR_USER) {
        log_warn(YELLOW("   [user ]") " User error detected - marking request as non-retryable");
        // Set exclude mask to all servers to prevent retries
        r->req->node_exclude_mask = (1 << servers->count) - 1; // Set all bits
      }
    }

    // Process any waiting requests
    if (pending) {
      pending_request_t* same = pending->same_requests;
      pending_remove(r);
      while (same) {
        pending_request_t* next = same->next;
        if (same->request) // finish the request either by returning the response or by triggering a new request in case this request failed
          trigger_uncached_curl_request(same->request, r->req->response.data ? (char*) r->req->response.data : NULL, r->req->response.len);
        safe_free(same);
        same = next;
      }
    }

    r->curl     = NULL; // setting it to NULL marks it as done
    r->end_time = current_ms();

    // Clean up the easy handle
    curl_multi_remove_handle(multi_handle, easy);
    curl_easy_cleanup(easy); // Cleanup the easy handle immediately

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
  // Retrieve context to get socket descriptor
  curl_poll_context_t* context = (curl_poll_context_t*) handle->data;

  // Check if there was an error
  if (status < 0) {
    log_error("Socket poll error: %s", uv_strerror(status));
    return;
  }

  int flags = 0;
  if (events & UV_READABLE) flags |= CURL_CSELECT_IN;
  if (events & UV_WRITABLE) flags |= CURL_CSELECT_OUT;

  int           running_handles;
  curl_socket_t socket = context->socket; // Use stored socket from context

  // Make sure the socket is valid
  if (socket < 0) {
    return;
  }

  CURLMcode rc = curl_multi_socket_action(multi_handle, socket, flags, &running_handles);
  if (rc != CURLM_OK) {
    log_error("curl_multi_socket_action error: %s", curl_multi_strerror(rc));
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
    log_error("curl_multi_socket_action error in timer: %s", curl_multi_strerror(rc));
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
  curl_poll_context_t* context = (curl_poll_context_t*) socketp;

  // Handle remove case first
  if (what == CURL_POLL_REMOVE) {
    if (context) {
      // Check if the handle is not already being closed
      if (context->poll_handle.data != NULL) {
        // --- START CHANGE ---
        // Comment out debug print
        // fprintf(stderr, "DEBUG: Initiating close for poll handle %p via CURL_POLL_REMOVE for socket %d\n", &context->poll_handle, (int) s);
        // --- END CHANGE ---
        // uv_poll_stop(&context->poll_handle); // REMOVED - uv_close should handle stopping
        context->poll_handle.data = NULL; // Mark as closing/invalid
        uv_close((uv_handle_t*) &context->poll_handle, cleanup_easy_handle_and_context);
        // Clear the socket association in curl *only if we initiated close now*
        curl_multi_assign(multi_handle, s, NULL);
      }
      else {
        // --- START CHANGE ---
        // Comment out debug print
        // fprintf(stderr, "DEBUG: Ignoring CURL_POLL_REMOVE for already closing poll handle associated with socket %d\n", (int) s);
        // --- END CHANGE ---
        // curl_multi_assign is NOT called here
      }
    }
    // curl_multi_assign is NOT called if context was initially NULL
    return 0;
  }

  // If we don't have a context/poll handle yet (socketp was NULL), create one
  if (!context) {
    context = (curl_poll_context_t*) safe_calloc(1, sizeof(curl_poll_context_t));
    if (!context) {
      log_error("Failed to allocate poll context");
      return -1;
    }
    context->easy_handle = easy;
    context->socket      = s; // Store socket descriptor for cross-platform access
    int err              = uv_poll_init_socket(uv_default_loop(), &context->poll_handle, s);
    if (err != 0) {
      log_error("Failed to initialize poll handle: %s", uv_strerror(err));
      safe_free(context);
      return -1;
    }
    context->poll_handle.data = context; // Mark as active
    // Associate the *new* context with the socket in curl
    curl_multi_assign(multi_handle, s, context);
  }

  // Determine which events to monitor
  int events = 0;
  if (what & CURL_POLL_IN) events |= UV_READABLE;
  if (what & CURL_POLL_OUT) events |= UV_WRITABLE;

  // Start or update polling for events
  int err = uv_poll_start(&context->poll_handle, events, poll_cb);
  if (err != 0) {
    log_error("Failed to start polling: %s", uv_strerror(err));
    // If starting polling failed, we should clean up the context/handle
    // Only do this if we *just* created the context in this call (!socketp check equivalent)
    if (!socketp) {                     // Check if context was created in this call
      context->poll_handle.data = NULL; // Mark as invalid before closing
      uv_close((uv_handle_t*) &context->poll_handle, cleanup_easy_handle_and_context);
    }
    // Libcurl docs suggest returning -1 on failure.
    return -1;
  }

  // Note: curl_multi_assign was already called when creating the context
  return 0;
}

server_list_t* c4_get_server_list(data_request_type_t type) {
  switch (type) {
    case C4_DATA_TYPE_ETH_RPC:
      return (server_list_t*) &eth_rpc_servers;
    case C4_DATA_TYPE_BEACON_API:
      return (server_list_t*) &beacon_api_servers;
    case C4_DATA_TYPE_PROVER:
      return (server_list_t*) &prover_servers;
    case C4_DATA_TYPE_CHECKPOINTZ:
      return (server_list_t*) &checkpointz_servers;
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
    log_error("Invalid request or context in c4_add_request_response");
    return;
  }

  http_response_t* res = (http_response_t*) req->ctx;
  data_request_t*  dr  = req->requests->req;
  if (c4_check_retry_request(req)) return; // if there are data_request in the req, we either clean it up or retry in case of an error (if possible.)

  // Check that client is still valid and not being closed
  if (!res->client || res->client->being_closed)
    log_warn("Client is no longer valid or is being closed - discarding response");
  else
    // Client is still valid, deliver the response
    res->cb(req->client, res->data, dr);

  // Clean up resources regardless of client state
  safe_free(res);
  safe_free(req->requests);
  safe_free(req);
}

// Function to determine TTL for different request types
static inline uint32_t get_request_ttl(data_request_t* req) {
  if (req->ttl && req->response.data && req->response.len > 0 && strnstr((char*) req->response.data, "\"error\":", req->response.len))
    return 0;
  return req->ttl;
}

// Function to generate cache key from request
static char* generate_cache_key(data_request_t* req) {
  buffer_t key = {0};
  bprintf(&key, "%d:%s:%s:%s:%l",
          req->type,
          req->url,
          req->method == C4_DATA_METHOD_POST ? (char*) req->payload.data : "",
          req->encoding == C4_DATA_ENCODING_JSON ? "json" : "ssz",
          req->chain_id);
  bytes32_t hash;
  sha256(key.data, hash);
  buffer_reset(&key);
  bprintf(&key, "%x", bytes(hash, 32));
  return (char*) key.data.data;
}

// Function to handle successful response and cache it
static void cache_response(single_request_t* r) {
  uint32_t ttl = get_request_ttl(r->req);
  if (ttl > 0 && r->req->response.data && r->req->response.len > 0 && memcache_client) { // Added check for valid response data
    char* key = generate_cache_key(r->req);
    if (key) { // Check if key generation succeeded
               // Use r->req->response directly instead of r->buffer
      memcache_set(memcache_client, key, strlen(key), (char*) r->req->response.data, r->req->response.len, ttl);
      safe_free(key);
    }
  }
  // Note: r->buffer should already be empty or transferred to r->req->response in handle_curl_events
}

// Helper function to configure SSL settings for an easy handle
static void configure_ssl_settings(CURL* easy) {
  if (!easy) {
    log_error("configure_ssl_settings: NULL easy handle passed");
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
  pending_request_t* pending = value == NULL ? pending_find_matching(r) : NULL; // is there a pending request asking for the same result

  // If we were waiting on memcache, close that span here (hit or miss)
  if (r->cache_span) {
    uint64_t dur_ms = current_unix_ms() - r->cache_start_ms;
    tracing_span_tag_i64(r->cache_span, "duration_ms", (int64_t) dur_ms);
    tracing_span_tag_str(r->cache_span, "cache", value ? "hit" : "miss");
    if (value) tracing_span_tag_i64(r->cache_span, "bytes", (int64_t) value_len);
    tracing_finish(r->cache_span);
    r->cache_span = NULL;
  }
  // If this request was waiting as a pending follower and now receives a value, finish the wait span
  if (r->wait_span) {
    uint64_t dur_ms = current_unix_ms() - r->wait_start_ms;
    tracing_span_tag_i64(r->wait_span, "duration_ms", (int64_t) dur_ms);
    tracing_span_tag_str(r->wait_span, "result", value ? "joined" : "joined_empty");
    if (value) tracing_span_tag_i64(r->wait_span, "bytes", (int64_t) value_len);
    tracing_finish(r->wait_span);
    r->wait_span = NULL;
  }

  if (r->req->type == C4_DATA_TYPE_INTERN && !value) { // this is an internal request, so we need to handle it differently
    c4_handle_internal_request(r);
    return;
  }

  if (pending) { // there is a pending request asking for the same result
    pending_add_to_same_requests(pending, r);
    log_info(GRAY("   [join ]") " %s", c4_req_info(r->req->type, r->req->url, r->req->payload));
    // Start a wait span representing the pending-follow duration
    if (tracing_is_enabled() && r->parent && r->parent->trace_root) {
      r->wait_start_ms = current_unix_ms();
      r->wait_span     = tracing_start_child(r->parent->trace_root, "pending join");
    }
    // callback will be called when the pending-request is done
  }
  else if (value) { // there is a cached response
    // Cache hit - create response from cached data
    log_info(GRAY("   [cache]") " %s", c4_req_info(r->req->type, r->req->url, r->req->payload));
    r->req->response = bytes_dup(bytes(value, value_len));
    r->curl          = NULL; // Mark as done
    r->cached        = true;
    r->end_time      = current_ms();
    call_callback_if_done(r->parent);
  }
  else {
    // Cache miss - proceed with normal request handling
    server_list_t* servers = c4_get_server_list(r->req->type);

    int selected_index;

    // Check if this is a retry (exclude_mask > 0) with valid pre-selected server index
    if (r->req->node_exclude_mask > 0 &&
        r->req->response_node_index < servers->count &&
        !(r->req->node_exclude_mask & (1 << r->req->response_node_index))) {
      // Use pre-selected index from retry logic
      selected_index = r->req->response_node_index;
      log_warn("   [retry] Using pre-selected server %s", c4_extract_server_name(servers->urls[selected_index]));
    }
    else {
      // Use intelligent server selection for initial requests
      // For RPC requests, use method-aware selection
      char* rpc_method = extract_rpc_method(r->req);
      if (rpc_method) {
        // Extract requested block number for known methods (if present)
        uint64_t requested_block = 0;
        bool     has_block       = false;
        extract_requested_block_for_method(r->req, rpc_method, &requested_block, &has_block);
        selected_index = c4_select_best_server_for_method(servers, r->req->node_exclude_mask, r->req->preferred_client_type, rpc_method, requested_block, has_block);
        safe_free(rpc_method);
      }
      else
        selected_index = c4_select_best_server(servers, r->req->node_exclude_mask, r->req->preferred_client_type);

      if (selected_index == -1) {
        // This should be very rare after emergency reset logic in c4_select_best_server
        log_error(":: CRITICAL ERROR: No available servers even after emergency reset attempts");
        r->req->error = bprintf(NULL, "All servers exhausted - check network connectivity");
        r->end_time   = current_ms();
        call_callback_if_done(r->parent);
        return;
      }
      // Update the request with the selected server index
      r->req->response_node_index = selected_index;
    }
    char* base_url = servers && servers->count > selected_index ? servers->urls[selected_index] : NULL;
    char* req_url  = c4_request_fix_url(r->req->url, r, servers->client_types[selected_index]);

    // Safeguard against NULL URLs
    if (!req_url) req_url = "";
    if (!base_url) base_url = "";

    if (strlen(base_url) == 0 && strlen(req_url) > 0)
      r->url = strdup(req_url);
    else if (strlen(req_url) == 0 && strlen(base_url) > 0)
      r->url = strdup(base_url);
    else if (strlen(req_url) > 0 && strlen(base_url) > 0)
      r->url = bprintf(NULL, "%s%s%s", base_url, base_url[strlen(base_url) - 1] == '/' ? "" : "/", req_url);
    else {
      log_error(":: ERROR: Empty URL");
      r->req->error = bprintf(NULL, "Empty URL");
      r->end_time   = current_ms();
      call_callback_if_done(r->parent);
      return;
    }

    // Tracing: create an attempt span and annotate server selection and weights/unsupported
    c4_tracing_annotate_attempt(r, servers, selected_index, base_url);

    pending_add(r);
    CURL* easy = curl_easy_init();
    r->curl    = easy;

#ifdef TEST
    // Apply test URL rewriter if set (for file:// based mocking)
    // ONLY in test builds!
    if (c4_test_url_rewriter) {
      char* rewritten = c4_test_url_rewriter(r->url,
                                             r->req->payload.data ? (char*) r->req->payload.data : NULL);
      if (rewritten && rewritten != r->url) {
        free(r->url);
        r->url = rewritten;
      }
    }
#endif

    curl_easy_setopt(easy, CURLOPT_URL, r->url);
    if (r->req->payload.len && r->req->payload.data) {
      curl_easy_setopt(easy, CURLOPT_POSTFIELDS, r->req->payload.data);
      curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, (long) r->req->payload.len);
    }

    // Set up headers
    r->headers = NULL; // Initialize headers
    if (r->req->payload.len && r->req->payload.data)
      r->headers = curl_slist_append(r->headers, r->req->encoding == C4_DATA_ENCODING_JSON ? "Content-Type: application/json" : "Content-Type: application/octet-stream");
    r->headers = curl_slist_append(r->headers, c4_request_fix_encoding(r->req->encoding, r, servers->client_types[selected_index]) == C4_DATA_ENCODING_JSON ? "Accept: application/json" : "Accept: application/octet-stream");
    r->headers = curl_slist_append(r->headers, "charsets: utf-8");
    r->headers = curl_slist_append(r->headers, "User-Agent: c4 curl ");
    // Inject b3 tracing headers
    if (r->attempt_span) {
      tracing_inject_b3_headers(r->attempt_span, &r->headers);
    }
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, r->headers);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, curl_append);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &r->buffer);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, (uint64_t) 120);
    curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, CURL_METHODS[r->req->method]);
    curl_easy_setopt(easy, CURLOPT_PRIVATE, r);

    // Preferred HTTP version and connection reuse settings
    curl_easy_setopt(easy, CURLOPT_HTTP_VERSION, http_server.curl.http2_enabled ? CURL_HTTP_VERSION_2TLS : CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(easy, CURLOPT_PIPEWAIT, 1L);
    curl_easy_setopt(easy, CURLOPT_DNS_CACHE_TIMEOUT, 600L);
    if (http_server.curl.tcp_keepalive_enabled) {
      curl_easy_setopt(easy, CURLOPT_TCP_KEEPALIVE, 1L);
      curl_easy_setopt(easy, CURLOPT_TCP_KEEPIDLE, (long) http_server.curl.tcp_keepidle_s);
      curl_easy_setopt(easy, CURLOPT_TCP_KEEPINTVL, (long) http_server.curl.tcp_keepintvl_s);
    }
#ifdef CURLOPT_UPKEEP_INTERVAL_MS
    if (http_server.curl.upkeep_interval_ms > 0)
      curl_easy_setopt(easy, CURLOPT_UPKEEP_INTERVAL_MS, (long) http_server.curl.upkeep_interval_ms);
#endif
#ifdef CURLOPT_MAXAGE_CONN
    curl_easy_setopt(easy, CURLOPT_MAXAGE_CONN, 300L);
#endif
    if (g_curl_share) curl_easy_setopt(easy, CURLOPT_SHARE, g_curl_share);

    // Configure SSL settings for this easy handle
    configure_ssl_settings(easy);

    // Mark request start for concurrency tracking (best-effort)
    c4_on_request_start(servers, selected_index, /*allow_overflow=*/true);
    curl_multi_add_handle(multi_handle, easy);
    //    fprintf(stderr, "send: [%p] %s  %s\n", easy, r->url, r->req->payload.data ? (char*) r->req->payload.data : "");
    // callback will be called when the request by handle_curl_events when all are done.
  }
}

static void trigger_cached_curl_requests(request_t* req) {
  uint64_t start_time = current_ms();
  for (size_t i = 0; i < req->request_count; i++) {
    single_request_t* r       = req->requests + i;
    data_request_t*   pending = r->req;
    r->start_time             = start_time;
    r->parent                 = req;         // Set the parent pointer
    if (!pending->ttl || !memcache_client) { // not cached, so we trigger a uncached request
      trigger_uncached_curl_request(r, NULL, 0);
      continue;
    }
    // Check cache first
    char* key = generate_cache_key(pending);
    // Start memcache "get" span
    if (tracing_is_enabled() && req->trace_root) {
      r->cache_start_ms = current_unix_ms();
      r->cache_span     = tracing_start_child(req->trace_root, "memcache get");
      if (r->cache_span) {
        tracing_span_tag_str(r->cache_span, "cache.layer", "memcached");
        tracing_span_tag_i64(r->cache_span, "ttl", (int64_t) pending->ttl);
      }
    }
    int ret = memcache_get(memcache_client, key, strlen(key), r, trigger_uncached_curl_request);
    safe_free(key);
    if (ret) {
      log_error("CACHE-Error : %d %s %s", ret, r->req->url, r->req->payload.data ? (char*) r->req->payload.data : "");
      trigger_uncached_curl_request(r, NULL, 0);
    }
  }
}

void c4_add_request(client_t* client, data_request_t* req, void* data, http_request_cb cb) {
  // Check if client is valid and not being closed
  if (!client || client->being_closed) {
    log_error("Attempted to add request to invalid or closing client");
    // Clean up resources since we won't be processing this request
    if (req) {
      safe_free(req->url);
      safe_free(req);
    }
    return;
  }

  http_response_t* res = (http_response_t*) safe_calloc(1, sizeof(http_response_t));
  request_t*       r   = (request_t*) safe_calloc(1, sizeof(request_t));
  r->client            = client;
  r->cb                = c4_add_request_response;
  r->requests          = (single_request_t*) safe_calloc(1, sizeof(single_request_t));
  r->requests->req     = req;
  r->request_count     = 1;
  r->ctx               = res;
  res->cb              = cb;
  res->data            = data;
  res->req             = r;
  res->client          = client;

  trigger_cached_curl_requests(r);
}

void c4_start_curl_requests(request_t* req, c4_state_t* state) {
  int len = 0, i = 0;

  // Count pending requests (server availability will be checked in c4_select_best_server)
  for (data_request_t* r = state->requests; r; r = r->next) {
    if (c4_state_is_pending(r)) len++;
  }

  if (len == 0) {
    // No pending requests, go back to the callback function
    req->cb(req);
    return;
  }

  req->requests      = (single_request_t*) safe_calloc(len, sizeof(single_request_t));
  req->request_count = len;

  for (data_request_t* r = state->requests; r; r = r->next) {
    if (c4_state_is_pending(r)) req->requests[i++].req = r;
  }

  trigger_cached_curl_requests(req);
}

static void free_single_request(single_request_t* r) {
  buffer_free(&r->buffer);
  safe_free(r->url);
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
    server_list_t*    servers = c4_get_server_list(pending->type);

    if (pending->error && servers) {
      // Check if too many servers are unhealthy (might indicate user error)
      if (c4_should_reset_health_stats(servers))
        c4_reset_server_health_stats(servers);

      // Mark the current server as failed in the exclude mask
      pending->node_exclude_mask |= (1 << pending->response_node_index);

      // Try to find another available server (c4_select_best_server handles emergency reset)
      int new_idx = c4_select_best_server(servers, pending->node_exclude_mask, pending->preferred_client_type);
      // Tracing: emit retry scheduling event (one-off child span)
      /*
      if (tracing_is_enabled() && req->trace_root) {
        trace_span_t* ev = tracing_start_child(req->trace_root, "retry");
        if (ev) {
          tracing_span_tag_str(ev, "retry.scheduled", "true");
          tracing_span_tag_i64(ev, "previous_server_index", (int64_t) pending->response_node_index);
          tracing_span_tag_i64(ev, "new_server_index", (int64_t) new_idx);
          tracing_span_tag_i64(ev, "exclude.mask", (int64_t) pending->node_exclude_mask);
          if (pending->error) tracing_span_tag_str(ev, "reason", pending->error);
          tracing_finish(ev);
        }
      }
      */
      if (new_idx != -1) {
        log_warn("   [retry] %s -> Using pre-selected server " BRIGHT_BLUE("%s"), c4_req_info(pending->type, pending->url, pending->payload), c4_extract_server_name(servers->urls[new_idx]));

        safe_free(pending->error);
        pending->response_node_index = new_idx;
        pending->error               = NULL;
        r->start_time                = current_ms();
        retry_requests++;
      }
      else {
        // This should be very rare due to emergency reset in c4_select_best_server
        log_error(":: No more servers available for retry after emergency measures");
      }
    }
  }

  if (retry_requests == 0) {
    for (int i = 0; i < req->request_count; i++) free_single_request(req->requests + i);
    safe_free(req->requests);
    req->request_count = 0;
    req->requests      = NULL;
    return false;
  }
  else {
    //    fprintf(stderr, ":: Retrying %d requests with different servers\n", retry_requests);
    single_request_t* pendings = (single_request_t*) safe_calloc(retry_requests, sizeof(single_request_t));
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

  // Use the new centralized configuration parser
  c4_parse_server_config(list, servers);
}

void c4_init_curl(uv_timer_t* timer) {
  // Initialize global curl state with SSL support
  curl_global_init(CURL_GLOBAL_SSL | CURL_GLOBAL_DEFAULT);

  // Initialize shared object for DNS and SSL session sharing
  g_curl_share = curl_share_init();
  if (g_curl_share) {
    curl_share_setopt(g_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
    curl_share_setopt(g_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
  }

  // Initialize multi handle
  multi_handle = curl_multi_init();
  curl_multi_setopt(multi_handle, CURLMOPT_SOCKETFUNCTION, socket_callback);
  curl_multi_setopt(multi_handle, CURLMOPT_TIMERFUNCTION, timer_callback);
  curl_multi_setopt(multi_handle, CURLMOPT_TIMERDATA, timer);

  // Configure connection pool limits and HTTP/2 multiplexing
  curl_multi_setopt(multi_handle, CURLMOPT_MAX_HOST_CONNECTIONS, (long) http_server.curl.pool_max_host);
  curl_multi_setopt(multi_handle, CURLMOPT_MAX_TOTAL_CONNECTIONS, (long) http_server.curl.pool_max_total);
#ifdef CURLPIPE_MULTIPLEX
  curl_multi_setopt(multi_handle, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
#endif
  curl_multi_setopt(multi_handle, CURLMOPT_MAXCONNECTS, (long) http_server.curl.pool_maxconnects);

  if (http_server.memcached_host && *http_server.memcached_host) {
    // Initialize memcached client
    memcache_client = memcache_new(http_server.memcached_pool, http_server.memcached_host, http_server.memcached_port);
    if (!memcache_client) {
      log_error("Failed to create memcached client");
      return;
    }
  }

  init_serverlist(&eth_rpc_servers, http_server.rpc_nodes);
  init_serverlist(&beacon_api_servers, http_server.beacon_nodes);
  init_serverlist(&prover_servers, http_server.prover_nodes);
  init_serverlist(&checkpointz_servers, http_server.checkpointz_nodes);
  // Auto-detect client types for servers without explicit configuration
  c4_detect_server_client_types(&eth_rpc_servers, C4_DATA_TYPE_ETH_RPC);
  c4_detect_server_client_types(&beacon_api_servers, C4_DATA_TYPE_BEACON_API);
  // Start RPC head polling (optional, based on ENV)
  c4_start_rpc_head_poller(&eth_rpc_servers);
}

static void free_server_list(server_list_t* list) {
  if (!list) return;
  if (list->health_stats) {
    for (size_t i = 0; i < list->count; i++)
      c4_cleanup_method_support(&list->health_stats[i]);
    safe_free(list->health_stats);
    list->health_stats = NULL;
  }
  if (list->client_types) {
    safe_free(list->client_types);
    list->client_types = NULL;
  }
  if (list->urls) {
    for (size_t i = 0; i < list->count; i++)
      safe_free(list->urls[i]);
    safe_free(list->urls);
    list->urls = NULL;
  }
  list->count      = 0;
  list->next_index = 0;
}

void c4_cleanup_curl() {
  // Close all handles and let the cleanup function free the resources
  curl_multi_setopt(multi_handle, CURLMOPT_SOCKETFUNCTION, NULL);
  curl_multi_cleanup(multi_handle);
  curl_global_cleanup();
  if (g_curl_share) {
    curl_share_cleanup(g_curl_share);
    g_curl_share = NULL;
  }
  if (memcache_client) {
    memcache_free(&memcache_client);
  }

  free_server_list(&eth_rpc_servers);
  free_server_list(&beacon_api_servers);
  free_server_list(&prover_servers);
  free_server_list(&checkpointz_servers);
}