/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "beacon.h"
#include "logger.h"
#include "server.h"

typedef struct {
  uv_work_t     req;
  request_t*    req_obj;
  prover_ctx_t* ctx;
} proof_work_t;

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

    uv_queue_work(uv_default_loop(), &work->req,
                  c4_prover_execute_worker,
                  c4_prover_execute_after);
    return true;
  }
  return false;
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
  switch (c4_prover_execute(ctx)) {
    case C4_SUCCESS:
      respond(req, ctx->proof, 200, "application/octet-stream");
      prover_request_free(req);
      return;

    case C4_ERROR: {
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
  prover_flags_t flags            = C4_PROVER_FLAG_UV_SERVER_CTX | C4_PROVER_FLAG_INCLUDE_SYNC | (http_server.period_store ? C4_PROVER_FLAG_CHAIN_STORE : 0);
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
  if (client_state.type == JSON_TYPE_STRING) ctx->client_state = json_as_bytes(client_state, &client_state_buf);
  if (!bytes_all_zero(bytes(http_server.witness_key, 32))) ctx->witness_key = bytes(http_server.witness_key, 32);

  safe_free(method_str);
  safe_free(params_str);
  req->cb(req);
  return true;
}
