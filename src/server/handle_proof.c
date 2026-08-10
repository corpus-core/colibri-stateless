/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "beacon.h"
#include "logger.h"
#include "server.h"
#include "util/compat.h"
#include "verify.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include "../util/win_compat.h"
#endif

#define HANDLE_PROOF_PROXY_MAX_URLS   32
#define HANDLE_PROOF_PROXY_MAX_URL_LEN 2048

static bool proxy_pattern_matches_host(const char* pattern, const char* host) {
  if (!pattern || !host || !*pattern) return false;
  if (pattern[0] == '*' && pattern[1] == '.') {
    const char* base = pattern + 2;
    if (!*base) return false;
    size_t bl = strlen(base);
    size_t hl = strlen(host);
    if (hl < bl + 2) return false;
    if (strcasecmp(host + hl - bl, base) != 0) return false;
    if (host[hl - bl - 1] != '.') return false;
    return true;
  }
  return strcasecmp(host, pattern) == 0;
}

static bool proxy_host_allowed_by_config(const char* host) {
  const char* cfg = http_server.proxy_allowed_domains;
  if (!cfg || !*cfg) return false;
  char* copy = strdup(cfg);
  if (!copy) return false;
  bool  ok    = false;
  char* save  = NULL;
  for (char* tok = c4_strtok_r(copy, ",", &save); tok && !ok; tok = c4_strtok_r(NULL, ",", &save)) {
    while (*tok && isspace((unsigned char) *tok)) tok++;
    char* end = tok + strlen(tok);
    while (end > tok && isspace((unsigned char) end[-1])) *--end = '\0';
    if (!*tok) continue;
    if (proxy_pattern_matches_host(tok, host)) ok = true;
  }
  free(copy);
  return ok;
}

/** Extract host from `https://` URL (no userinfo). Returns false if invalid. */
static bool proxy_extract_https_host(const char* url, char* out, size_t cap) {
  const char* p = url;
  while (*p && isspace((unsigned char) *p)) p++;
  if (strncasecmp(p, "https://", 8) != 0) return false;
  p += 8;
  for (const char* q = p; *q && *q != '/' && *q != '?' && *q != '#'; q++) {
    if (*q == '@') return false;
  }
  const char *host_start, *host_end;
  if (*p == '[') {
    host_start = p + 1;
    host_end   = strchr(host_start, ']');
    if (!host_end) return false;
    p = host_end + 1;
    if (*p == ':') {
      while (*p && *p != '/' && *p != '?' && *p != '#') p++;
    }
  }
  else {
    host_start = p;
    host_end   = strpbrk(p, "/?#");
    if (!host_end) host_end = p + strlen(p);
    const char* colon = NULL;
    for (const char* q = host_start; q < host_end; q++) {
      if (*q == ':') colon = q;
    }
    if (colon) {
      bool all_digit = true;
      for (const char* q = colon + 1; q < host_end; q++) {
        if (!isdigit((unsigned char) *q)) {
          all_digit = false;
          break;
        }
      }
      if (all_digit) host_end = colon;
    }
  }
  size_t n = (size_t) (host_end - host_start);
  if (n == 0 || n >= cap) return false;
  memcpy(out, host_start, n);
  out[n] = '\0';
  return true;
}

static bool proxy_url_acceptable(const char* url, char* err, size_t err_len) {
  if (strlen(url) > HANDLE_PROOF_PROXY_MAX_URL_LEN) {
    snprintf(err, err_len, "URL too long");
    return false;
  }
  char host[256];
  if (!proxy_extract_https_host(url, host, sizeof host)) {
    snprintf(err, err_len, "URL must use https:// without credentials");
    return false;
  }
  if (!proxy_host_allowed_by_config(host)) {
    snprintf(err, err_len, "host '%s' is not in proxy_allowed_domains", host);
    return false;
  }
  return true;
}

/** Validate every URL in a comma-separated list, then build a `server_list_t`. */
static bool handle_proof_parse_proxy_csv(char* csv, server_list_t** out_list, char* err, size_t err_len) {
  *out_list = NULL;
  if (!csv || !*csv) return true;

  char*  copy  = strdup(csv);
  char*  save  = NULL;
  size_t count = 0;
  for (char* tok = c4_strtok_r(copy, ",", &save); tok; tok = c4_strtok_r(NULL, ",", &save)) {
    while (*tok && isspace((unsigned char) *tok)) tok++;
    char* end = tok + strlen(tok);
    while (end > tok && isspace((unsigned char) end[-1])) end--;
    *end = '\0';
    if (!*tok) continue;
    if (count >= (size_t) HANDLE_PROOF_PROXY_MAX_URLS) {
      snprintf(err, err_len, "at most %d URLs allowed", HANDLE_PROOF_PROXY_MAX_URLS);
      free(copy);
      return false;
    }
    if (!proxy_url_acceptable(tok, err, err_len)) {
      free(copy);
      return false;
    }
    count++;
  }
  free(copy);
  if (count == 0) return true;

  server_list_t* list = (server_list_t*) safe_calloc(1, sizeof(server_list_t));
  c4_parse_server_config(list, csv);
  if (list->count == 0) {
    c4_free_server_list(list);
    safe_free(list);
    snprintf(err, err_len, "no valid URLs after parsing");
    return false;
  }
  *out_list = list;
  return true;
}

typedef struct {
  uv_work_t     req;
  request_t*    req_obj;
  prover_ctx_t* ctx;
  // tracing for worker execution
  trace_span_t* span;
  uint64_t      start_ms;
} proof_work_t;

#ifdef PROVER_TRACE
// Flush and free collected prover spans, attaching them under 'parent'
static void c4_tracing_flush_prover_spans(trace_span_t* parent, prover_ctx_t* ctx) {
  if (!parent || !ctx) return;
  // Close open span at boundary
  if (ctx->trace_open) {
    ctx->trace_open->duration_ms = current_unix_ms() - ctx->trace_open->start_ms;
    ctx->trace_open->next        = ctx->trace_spans;
    ctx->trace_spans             = ctx->trace_open;
    ctx->trace_open              = NULL;
  }
  for (prover_trace_span_t* s = ctx->trace_spans; s;) {
    trace_span_t* child = tracing_start_child_at(parent, s->name ? s->name : "prover", s->start_ms);
    if (child) {
      for (prover_trace_kv_t* kv = s->tags; kv; kv = kv->next) {
        if (kv->key && kv->value) tracing_span_tag_str(child, kv->key, kv->value);
      }
      tracing_finish_at(child, s->start_ms + s->duration_ms);
    }
    // free collected span
    prover_trace_span_t* next = s->next;
    while (s->tags) {
      prover_trace_kv_t* tnext = s->tags->next;
      if (s->tags->key) free(s->tags->key);
      if (s->tags->value) free(s->tags->value);
      free(s->tags);
      s->tags = tnext;
    }
    if (s->name) free(s->name);
    free(s);
    s = next;
  }
  ctx->trace_spans = NULL;
}
#endif

/**
 * Sends the prover response to the client or to a parent callback.
 *
 * Two modes:
 * 1. DIRECT (parent_cb == NULL): Sends HTTP response directly to client, including
 *    a `Compute-Units` header so an upstream load balancer can forward the value
 *    to API-key based billing without the server itself knowing about API keys.
 * 2. CALLBACK (parent_cb != NULL): Calls parent_cb with result. `compute_units` is
 *    intentionally not propagated to the parent because internal verifier sub-requests
 *    are not subject to billing.
 *
 * When in callback mode:
 * - Used when the prover is called as a sub-request from the verifier
 * - parent_ctx points to verify_request_t
 * - parent_cb is prover_callback (in handle_verify.c)
 * - Cleanup is handled by c4_prover_handle_request() after this returns
 *
 * @param req Request with context and callback info
 * @param result Result bytes (proof or error message)
 * @param status HTTP status code
 * @param content_type Content-Type for direct HTTP response
 * @param compute_units accumulated compute units for billing; emitted as `Compute-Units` header in DIRECT mode
 */
static void respond(request_t* req, bytes_t result, int status, char* content_type, uint64_t compute_units) {
  if (req->parent_cb && req->parent_ctx) {
    // CALLBACK MODE: Call parent_cb instead of responding directly
    data_request_t* data = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
    if (status == 200)
      data->response = bytes_dup(result);
    else {
      data->error = safe_malloc(result.len + 1);
      memcpy(data->error, result.data, result.len);
      data->error[result.len] = '\0';
    }
    req->parent_cb(req->client, req->parent_ctx, data);
  }
  else {
    buffer_t hdr = {0};
    bprintf(&hdr, "Compute-Units: %l\r\n", compute_units);
    // Cache-Control: only cache successful proofs; the value is server-controlled (derived from
    // the block identifier), never from client input, so no header-injection risk.
    if (status == 200 && req->cache_control && *req->cache_control)
      bprintf(&hdr, "Cache-Control: %s\r\n", req->cache_control);
    else if (status != 200)
      bprintf(&hdr, "Cache-Control: no-store\r\n");
    c4_http_respond_ex(req->client, status, content_type, result, hdr.data);
    buffer_free(&hdr);
  }
}

// --- executed in worker-thread ---
static void c4_prover_execute_worker(uv_work_t* req) {
  proof_work_t* work = (proof_work_t*) req->data;
  c4_prover_execute(work->ctx);
  work->ctx->flags &= ~C4_PROVER_FLAG_UV_WORKER_REQUIRED;
}

static void c4_prover_execute_after(uv_work_t* req, int status) {
  proof_work_t* work = (proof_work_t*) req->data;
  // finish worker tracing span
  if (work->span) {
    // attach any prover-internal spans to the worker span before finishing it
#ifdef PROVER_TRACE
    c4_tracing_flush_prover_spans(work->span, work->ctx);
#endif
    uint64_t dur_ms = current_unix_ms() - work->start_ms;
    tracing_span_tag_i64(work->span, "duration_ms", (int64_t) dur_ms);
    tracing_span_tag_str(work->span, "thread", "worker");
    tracing_finish(work->span);
    work->span = NULL;
  }
  c4_prover_handle_request(work->req_obj);
  safe_free(req);
}

static void prover_request_free(request_t* req) {
  prover_ctx_t* ctx = (prover_ctx_t*) req->ctx;
  if (req->start_time)
    c4_metrics_add_request(C4_DATA_TYPE_INTERN, ctx->method, ctx->state.error ? strlen(ctx->state.error) : ctx->proof.len, current_ms() - req->start_time, ctx->state.error == NULL, false);
  c4_prover_free((prover_ctx_t*) req->ctx);
  // NOTE: We do NOT free req->requests here - that's handled elsewhere
  if (req->proxy_rpc_servers) {
    c4_free_server_list(req->proxy_rpc_servers);
    safe_free(req->proxy_rpc_servers);
  }
  if (req->proxy_beacon_servers) {
    c4_free_server_list(req->proxy_beacon_servers);
    safe_free(req->proxy_beacon_servers);
  }
  if (req->cache_control) safe_free(req->cache_control);
  safe_free(req);
}
static bool c4_check_worker_request(request_t* req) {
  prover_ctx_t* ctx = (prover_ctx_t*) req->ctx;
  if (ctx->flags & C4_PROVER_FLAG_UV_WORKER_REQUIRED && c4_prover_status(ctx) == C4_PENDING && c4_state_get_pending_request(&ctx->state) == NULL) {
    // no data are required and no pending request, so we can execute the prover in the worker thread
    proof_work_t* work = (proof_work_t*) safe_calloc(1, sizeof(proof_work_t));
    work->req_obj      = req;
    work->ctx          = ctx;
    work->req.data     = work;
    // tracing: worker span for encoding/proof building
    if (tracing_is_enabled() && req->trace_root) {
      work->start_ms = current_unix_ms();
      work->span     = tracing_start_child(req->trace_root, "worker: build proof");
    }

    uv_queue_work(uv_default_loop(), &work->req,
                  c4_prover_execute_worker,
                  c4_prover_execute_after);
    return true;
  }
  return false;
}

static c4_status_t prover_execute(request_t* req, prover_ctx_t* ctx) {
  trace_span_t* exec_span = NULL;
  uint64_t      exec_ms   = 0;
  if (tracing_is_enabled() && req->trace_root) {
    char span_name[100];
    sbprintf(span_name, "prover_execute | # %d", req->prover_step);
    exec_span = tracing_start_child(req->trace_root, span_name);
    if (exec_span) {
      tracing_span_tag_str(exec_span, "thread", "main");
      tracing_span_tag_i64(exec_span, "step", (int64_t) req->prover_step);
    }
    exec_ms = current_unix_ms();
  }
  c4_status_t exec_res = c4_prover_execute(ctx);
  if (exec_span) {
    tracing_span_tag_i64(exec_span, "duration_ms", (int64_t) (current_unix_ms() - exec_ms));
    // add diagnostics: number of data requests in ctx and cache entries (if enabled)
    int req_count = 0;
    for (data_request_t* d = ctx->state.requests; d; d = d->next) req_count++;
    tracing_span_tag_i64(exec_span, "state.requests", (int64_t) req_count);
#ifdef PROVER_CACHE
    int cache_count = 0;
    for (cache_entry_t* ce = ctx->cache; ce; ce = ce->next) cache_count++;
    tracing_span_tag_i64(exec_span, "cache.entries", (int64_t) cache_count);
#endif
#ifdef PROVER_TRACE
    // Flush prover-internal finished spans as children of exec_span
    c4_tracing_flush_prover_spans(exec_span, ctx);
#endif
    switch (exec_res) {
      case C4_SUCCESS: {
        if (exec_span) {
          tracing_span_tag_str(exec_span, "result", "success");
          tracing_finish(exec_span);
        }
        if (req->trace_root) {
          tracing_span_tag_str(req->trace_root, "status", "ok");
          tracing_span_tag_i64(req->trace_root, "proof.size", (int64_t) ctx->proof.len);
          ssz_ob_t proof       = (ssz_ob_t) {.def = c4_get_request_type(c4_get_chain_type_from_req(ctx->proof)), .bytes = ctx->proof};
          ssz_ob_t proof_proof = ssz_get(&proof, "proof");
          ssz_ob_t proof_sync  = ssz_get(&proof, "sync_data");
          tracing_span_tag_str(req->trace_root, "proof_type", proof_proof.def ? proof_proof.def->name : "none");
          tracing_span_tag_i64(req->trace_root, "proof.proof_size", (int64_t) proof_proof.bytes.len);
          tracing_span_tag_i64(req->trace_root, "proof.sync_size", (int64_t) (proof_sync.bytes.len > 0 ? proof_sync.bytes.len : 0));
          tracing_finish(req->trace_root);
          req->trace_root = NULL;
        }
        break;
      }
      case C4_ERROR: {
        if (exec_span) {
          tracing_span_tag_str(exec_span, "result", "error");
          if (ctx->state.error) tracing_span_tag_str(exec_span, "error", ctx->state.error);
          tracing_finish(exec_span);
        }
        if (req->trace_root) {
          tracing_span_tag_str(req->trace_root, "status", "error");
          if (ctx->state.error) tracing_span_tag_str(req->trace_root, "error", ctx->state.error);
          tracing_finish(req->trace_root);
          req->trace_root = NULL;
        }
        break;
      }
      case C4_PENDING: {
        if (exec_span) {
          tracing_span_tag_str(exec_span, "result", "pending");
          tracing_finish(exec_span);
          exec_span = NULL;
        }

        break;
      }
      default:
        break;
    }
  }
  req->prover_step++;
  return exec_res;
}

/**
 * Handler for prover requests.
 * Used in two modes:
 *
 * 1. DIRECT: Called from c4_handle_proof_request() for /proof endpoint
 *    - Sends proof directly as HTTP response via respond()
 *    - prover_request_free() is called to cleanup
 *
 * 2. CALLBACK: Called as sub-request from verifier (handle_verify.c)
 *    - parent_ctx and parent_cb are set
 *    - respond() calls parent_cb instead of sending HTTP response
 *    - prover_request_free() is still called to cleanup
 *
 * @param req Request with prover_ctx_t* as ctx
 */
void c4_prover_handle_request(request_t* req) {
  if (c4_check_retry_request(req) || c4_check_worker_request(req)) return;

  prover_ctx_t* ctx = (prover_ctx_t*) req->ctx;
  const char*   req_path =
      (req && req->client && req->client->request.path) ? req->client->request.path : "";
  bytes_t req_payload =
      (req && req->client && req->client->request.payload && req->client->request.payload_len)
          ? bytes(req->client->request.payload, (uint32_t) req->client->request.payload_len)
          : (bytes_t) {0};
  uint64_t client_ptr = (uint64_t) (uintptr_t) (req ? req->client : NULL);
  // measure and trace c4_prover_execute invocation on main thread

  switch (prover_execute(req, ctx)) {
    case C4_SUCCESS:
      log_info(MAGENTA("::[ OK ]") "%s " GRAY(" (%d bytes in %l ms) :: #%lx"),
               c4_req_info(C4_DATA_TYPE_INTERN, req_path, req_payload),
               ctx->proof.len, (uint64_t) (current_ms() - req->start_time), client_ptr);
      respond(req, ctx->proof, 200, "application/octet-stream", ctx->compute_units);
      prover_request_free(req);
      return;

    case C4_ERROR: {
      log_info(RED("::[ERR ]") "%s " YELLOW("%s") GRAY(" :: #%lx"),
               c4_req_info(C4_DATA_TYPE_INTERN, req_path, req_payload),
               ctx->state.error ? ctx->state.error : "",
               client_ptr);

      buffer_t buf = {0};
      bprintf(&buf, "{\"error\":\"%s\"}", ctx->state.error);
      respond(req, buf.data, 500, "application/json", ctx->compute_units);
      buffer_free(&buf);
      prover_request_free(req);
      return;
    }

    case C4_PENDING:
      if (c4_state_get_pending_request(&ctx->state)) // there are pending requests, let's take care of them first
        c4_start_curl_requests(req, &ctx->state);
      else if (ctx->flags & C4_PROVER_FLAG_UV_WORKER_REQUIRED) // worker is required, retry and handle it in the beginning of the next loop
        c4_prover_handle_request(req);
      else {
        // stop here, we don't have anything to do
        char* error = "{\"error\":\"Internal prover error: no prover available\"}";
        respond(req, bytes((uint8_t*) error, strlen(error)), 500, "application/json", ctx->compute_units);
        if (req->trace_root) {
          tracing_span_tag_str(req->trace_root, "status", "error");
          tracing_span_tag_str(req->trace_root, "error", "Internal prover error: no prover available");
          tracing_finish(req->trace_root);
          req->trace_root = NULL;
        }
        prover_request_free(req);
      }
  }
}

void c4_proof_request_dispatch(client_t* client, char* method_str, char* params_str,
                               uint32_t version, prover_flags_t extra_flags,
                               bytes_t client_state, bytes_t witness_key,
                               server_list_t* proxy_rpc, server_list_t* proxy_beacon,
                               const char* cache_control) {
  prover_flags_t flags = C4_PROVER_FLAG_UV_SERVER_CTX | http_server.prover_flags | extra_flags;
  if (proxy_rpc || proxy_beacon) flags |= C4_PROVER_FLAG_PROXY;

  request_t*    req = (request_t*) safe_calloc(1, sizeof(request_t));
  prover_ctx_t* ctx = c4_prover_create(method_str, params_str, (chain_id_t) http_server.chain_id, flags);
  req->start_time           = current_ms();
  req->client               = client;
  req->cb                   = c4_prover_handle_request;
  req->ctx                  = ctx;
  req->proxy_rpc_servers    = proxy_rpc;
  req->proxy_beacon_servers = proxy_beacon;
  if (cache_control && *cache_control) req->cache_control = strdup(cache_control);
  ctx->version = version;
  if (client_state.data && client_state.len) {
    ctx->client_state = bytes_dup(client_state);
    if (ctx->client_state.len > 4) ctx->flags |= C4_PROVER_FLAG_INCLUDE_SYNC;
  }
  if (witness_key.data && witness_key.len)
    ctx->witness_key = bytes_dup(witness_key);

  // Tracing: start root span
  if (tracing_is_enabled() && client->trace_level != TRACE_LEVEL_NONE) {
    char name_tmp[256];
    sbprintf(name_tmp, "proof/%s", method_str ? method_str : "unknown");
    bool force_debug = (client->trace_level == TRACE_LEVEL_DEBUG && tracing_debug_quota_try_consume());
    if (client->b3_trace_id) {
      int sampled = force_debug ? 1 : (client->b3_sampled == 0 ? 0 : 1);
      req->trace_root = tracing_start_root_with_b3(name_tmp, client->b3_trace_id,
                                                   client->b3_span_id ? client->b3_span_id : client->b3_parent_span_id,
                                                   sampled);
    }
    else
      req->trace_root = force_debug ? tracing_start_root_forced(name_tmp) : tracing_start_root(name_tmp);
    if (req->trace_root) {
      tracing_span_tag_str(req->trace_root, "method", method_str ? method_str : "");
      tracing_span_tag_json(req->trace_root, "params", params_str);
      tracing_span_tag_i64(req->trace_root, "chain_id", (int64_t) http_server.chain_id);
      tracing_span_tag_i64(req->trace_root, "flags", (int64_t) ctx->flags);
      tracing_span_tag_i64(req->trace_root, "request.size", (int64_t) client->request.payload_len);
      tracing_span_tag_str(req->trace_root, "trace.level", client->trace_level == TRACE_LEVEL_DEBUG ? "debug" : "min");
      tracing_span_tag_str(req->trace_root, "include_code", (ctx->flags & C4_PROVER_FLAG_INCLUDE_CODE) ? "true" : "false");
      if (ctx->client_state.len) {
        char cs_str[70];
        sbprintf(cs_str, "%x", ctx->client_state);
        tracing_span_tag_str(req->trace_root, "client_state", cs_str);
      }
    }
  }

  safe_free(method_str);
  safe_free(params_str);
  req->cb(req);
}

bool c4_handle_proof_request(client_t* client) {
  if (client->request.method != C4_DATA_METHOD_POST /*|| strncmp(client->request.path, "/proof/", 7) != 0*/)
    return false;

  if (strlen(client->request.path) > 1 && strncmp(client->request.path, "/proof", 6) != 0)
    return false;

  json_t rpc_req = json_parse((char*) client->request.payload);
  if (rpc_req.type != JSON_TYPE_OBJECT) {
    c4_write_error_response(client, 400, "Invalid request");
    return true;
  }
  json_t method       = json_get(rpc_req, "method");
  json_t params       = json_get(rpc_req, "params");
  json_t version      = json_get(rpc_req, "version");
  json_t client_state = json_get(rpc_req, "c4");
  json_t include_code = json_get(rpc_req, "include_code");
  json_t zk_proof     = json_get(rpc_req, "zk_proof");
  json_t logs_compl   = json_get(rpc_req, "logs_completeness");
  json_t signers      = json_get(rpc_req, "signers");
  if (method.type != JSON_TYPE_STRING || params.type != JSON_TYPE_ARRAY) {
    c4_write_error_response(client, 400, "Invalid request");
    return true;
  }

  json_t rpc_proxy_j    = json_get(rpc_req, "rpc");
  json_t beacon_proxy_j = json_get(rpc_req, "beacon");
  if (rpc_proxy_j.type != JSON_TYPE_NOT_FOUND && rpc_proxy_j.type != JSON_TYPE_STRING) {
    c4_write_error_response(client, 400, "Invalid rpc field (expected comma-separated URL string)");
    return true;
  }
  if (beacon_proxy_j.type != JSON_TYPE_NOT_FOUND && beacon_proxy_j.type != JSON_TYPE_STRING) {
    c4_write_error_response(client, 400, "Invalid beacon field (expected comma-separated URL string)");
    return true;
  }

  // Parse proxy CSVs up front (before allocating the prover ctx) so cleanup on error is trivial.
  bool wants_rpc_proxy    = rpc_proxy_j.type == JSON_TYPE_STRING && rpc_proxy_j.len > 2;
  bool wants_beacon_proxy = beacon_proxy_j.type == JSON_TYPE_STRING && beacon_proxy_j.len > 2;
  if ((wants_rpc_proxy || wants_beacon_proxy) && !http_server.proxy_enabled) {
    c4_write_error_response(client, 403, "Client rpc/beacon URL lists are disabled on this server");
    return true;
  }

  server_list_t* proxy_rpc    = NULL;
  server_list_t* proxy_beacon = NULL;
  char           proxy_err[256];
  char*          rpc_csv    = wants_rpc_proxy ? json_new_string(rpc_proxy_j) : NULL;
  char*          beacon_csv = wants_beacon_proxy ? json_new_string(beacon_proxy_j) : NULL;

  if (rpc_csv && !handle_proof_parse_proxy_csv(rpc_csv, &proxy_rpc, proxy_err, sizeof proxy_err)) {
    safe_free(rpc_csv);
    safe_free(beacon_csv);
    c4_write_error_response(client, 400, proxy_err);
    return true;
  }
  if (beacon_csv && !handle_proof_parse_proxy_csv(beacon_csv, &proxy_beacon, proxy_err, sizeof proxy_err)) {
    safe_free(rpc_csv);
    safe_free(beacon_csv);
    if (proxy_rpc) {
      c4_free_server_list(proxy_rpc);
      safe_free(proxy_rpc);
    }
    c4_write_error_response(client, 400, proxy_err);
    return true;
  }
  safe_free(rpc_csv);
  safe_free(beacon_csv);
  /* Per-request proxy lists keep BEACON_CLIENT_UNKNOWN: c4_detect_server_client_types uses sync curl and would block
   * libuv. Planned: per-host cache + async detection (track via GitHub issue on corpus-core/colibri-stateless). */

  prover_flags_t extra_flags = 0;
  if (include_code.type == JSON_TYPE_BOOLEAN && include_code.start[0] == 't') extra_flags |= C4_PROVER_FLAG_INCLUDE_CODE;
  if (zk_proof.type == JSON_TYPE_BOOLEAN && zk_proof.start[0] == 't') extra_flags |= C4_PROVER_FLAG_ZK_PROOF;
  if (logs_compl.type == JSON_TYPE_BOOLEAN && logs_compl.start[0] == 't') extra_flags |= C4_PROVER_FLAG_LOGS_COMPLETENESS;

  buffer_t client_state_buf = {0};
  bytes_t  cs               = NULL_BYTES;
  if (client_state.type == JSON_TYPE_STRING && client_state.len > 4) cs = json_as_bytes(client_state, &client_state_buf);

  buffer_t witness_buf = {0};
  bytes_t  wk          = NULL_BYTES;
  if (signers.type == JSON_TYPE_STRING && signers.len > 43 && signers.start[1] == '0' && signers.start[2] == 'x')
    wk = json_as_bytes(signers, &witness_buf);
  else if (!bytes_all_zero(bytes(http_server.witness_key, 32)))
    wk = bytes(http_server.witness_key, 32);

  uint32_t version_num = version.type == JSON_TYPE_NUMBER ? (uint32_t) json_as_uint32(version) : 0;

  c4_proof_request_dispatch(client, bprintf(NULL, "%j", method), bprintf(NULL, "%J", params),
                            version_num, extra_flags, cs, wk, proxy_rpc, proxy_beacon, NULL);

  buffer_free(&client_state_buf);
  buffer_free(&witness_buf);
  return true;
}
