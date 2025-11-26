/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "beacon.h"
#include "logger.h"
#include "server.h"
#include "verify.h"

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
 * 1. DIRECT (parent_cb == NULL): Sends HTTP response directly to client
 * 2. CALLBACK (parent_cb != NULL): Calls parent_cb with result
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
 */
static void respond(request_t* req, bytes_t result, int status, char* content_type) {
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
  else
    c4_http_respond(req->client, status, content_type, result);
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
  // measure and trace c4_prover_execute invocation on main thread

  switch (prover_execute(req, ctx)) {
    case C4_SUCCESS:
      log_info(MAGENTA("::[ OK ]") "%s " GRAY(" (%d bytes in %l ms) :: #%lx"),
               c4_req_info(C4_DATA_TYPE_INTERN, req->client->request.path, bytes(req->client->request.payload, req->client->request.payload_len)),
               ctx->proof.len, (uint64_t) (current_ms() - req->start_time), (uint64_t) (uintptr_t) req->client);
      respond(req, ctx->proof, 200, "application/octet-stream");
      prover_request_free(req);
      return;

    case C4_ERROR: {
      log_info(RED("::[ERR ]") "%s " YELLOW("%s") GRAY(" :: #%lx"),
               c4_req_info(C4_DATA_TYPE_INTERN, req->client->request.path, bytes(req->client->request.payload, req->client->request.payload_len)),
               ctx->state.error ? ctx->state.error : "",
               (uint64_t) (uintptr_t) req->client);

      buffer_t buf = {0};
      bprintf(&buf, "{\"error\":\"%s\"}", ctx->state.error);
      respond(req, buf.data, 500, "application/json");
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
        respond(req, bytes((uint8_t*) error, strlen(error)), 500, "application/json");
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
bool c4_handle_proof_request(client_t* client) {
  if (client->request.method != C4_DATA_METHOD_POST /*|| strncmp(client->request.path, "/proof/", 7) != 0*/)
    return false;

  json_t rpc_req = json_parse((char*) client->request.payload);
  if (rpc_req.type != JSON_TYPE_OBJECT) {
    c4_write_error_response(client, 400, "Invalid request");
    return true;
  }
  json_t method       = json_get(rpc_req, "method");
  json_t params       = json_get(rpc_req, "params");
  json_t client_state = json_get(rpc_req, "c4");
  json_t include_code = json_get(rpc_req, "include_code");
  if (method.type != JSON_TYPE_STRING || params.type != JSON_TYPE_ARRAY) {
    c4_write_error_response(client, 400, "Invalid request");
    return true;
  }

  prover_flags_t flags            = C4_PROVER_FLAG_UV_SERVER_CTX | http_server.prover_flags;
  buffer_t       client_state_buf = {0};
  char*          method_str       = bprintf(NULL, "%j", method);
  char*          params_str       = bprintf(NULL, "%J", params);
  request_t*     req              = (request_t*) safe_calloc(1, sizeof(request_t));
  prover_ctx_t*  ctx              = c4_prover_create(method_str, params_str, (chain_id_t) http_server.chain_id, flags);
  req->start_time                 = current_ms();
  req->client                     = client;
  req->cb                         = c4_prover_handle_request;
  req->ctx                        = ctx;
  if (include_code.type == JSON_TYPE_BOOLEAN && include_code.start[0] == 't') ctx->flags |= C4_PROVER_FLAG_INCLUDE_CODE;
  if (client_state.type == JSON_TYPE_STRING && client_state.len > 4) ctx->client_state = json_as_bytes(client_state, &client_state_buf);
  if (ctx->client_state.len > 4) ctx->flags |= C4_PROVER_FLAG_INCLUDE_SYNC;
  if (!bytes_all_zero(bytes(http_server.witness_key, 32))) ctx->witness_key = bytes(http_server.witness_key, 32);

  // Tracing: start root span
  if (tracing_is_enabled() && client->trace_level != TRACE_LEVEL_NONE) {
    char  name_tmp[256];
    char* desc = c4_req_info(C4_DATA_TYPE_INTERN, "", bytes(client->request.payload, client->request.payload_len));
    sbprintf(name_tmp, "proof/%s", method_str ? method_str : "unknown");
    bool force_debug = (client->trace_level == TRACE_LEVEL_DEBUG && tracing_debug_quota_try_consume());
    if (client->b3_trace_id) {
      int sampled =
          force_debug ? 1 : (client->b3_sampled == 0 ? 0 : 1);
      req->trace_root = tracing_start_root_with_b3(name_tmp, client->b3_trace_id,
                                                   client->b3_span_id ? client->b3_span_id : client->b3_parent_span_id,
                                                   sampled);
    }
    else {
      req->trace_root = force_debug ? tracing_start_root_forced(name_tmp) : tracing_start_root(name_tmp);
    }
    if (req->trace_root) {
      tracing_span_tag_str(req->trace_root, "method", method_str ? method_str : "");
      tracing_span_tag_json(req->trace_root, "params", params_str);
      tracing_span_tag_i64(req->trace_root, "chain_id", (int64_t) http_server.chain_id);
      tracing_span_tag_i64(req->trace_root, "flags", (int64_t) ctx->flags);
      tracing_span_tag_i64(req->trace_root, "request.size", (int64_t) client->request.payload_len);
      tracing_span_tag_str(req->trace_root, "trace.level", client->trace_level == TRACE_LEVEL_DEBUG ? "debug" : "min");
      if (include_code.type == JSON_TYPE_BOOLEAN)
        tracing_span_tag_str(req->trace_root, "include_code", include_code.start[0] == 't' ? "true" : "false");
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
  return true;
}
