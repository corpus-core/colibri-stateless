/*
 * Copyright (c) 2025,2026 corpus.core
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include "colibri_common.h"
#include "version.h"
#include <stdlib.h>
#include <string.h>

/* ── string converters ── */

static const char* status_to_string(c4_status_t status) {
  switch (status) {
    case C4_SUCCESS: return "success";
    case C4_ERROR: return "error";
    case C4_PENDING: return "pending";
  }
  return "error";
}

static const char* encoding_to_string(data_request_encoding_t encoding) {
  switch (encoding) {
    case C4_DATA_ENCODING_SSZ: return "ssz";
    case C4_DATA_ENCODING_JSON: return "json";
  }
  return "json";
}

static const char* method_to_string(data_request_method_t method) {
  switch (method) {
    case C4_DATA_METHOD_GET: return "get";
    case C4_DATA_METHOD_POST: return "post";
    case C4_DATA_METHOD_PUT: return "put";
    case C4_DATA_METHOD_DELETE: return "delete";
  }
  return "get";
}

static const char* data_request_type_to_string(data_request_type_t type) {
  switch (type) {
    case C4_DATA_TYPE_BEACON_API: return "beacon_api";
    case C4_DATA_TYPE_ETH_RPC: return "eth_rpc";
    case C4_DATA_TYPE_REST_API: return "rest_api";
    case C4_DATA_TYPE_INTERN: return "intern";
    case C4_DATA_TYPE_PROVER: return "prover";
    case C4_DATA_TYPE_CHECKPOINTZ: return "checkpointz";
  }
  return "eth_rpc";
}

/* ── JSON helpers ── */

void c4i_add_data_request(buffer_t* result, data_request_t* req, bool req_ptr_as_string) {
  if (req_ptr_as_string)
    bprintf(result, "{\"req_ptr\": \"%l\",", (uint64_t) (uintptr_t) req);
  else
    bprintf(result, "{\"req_ptr\": %l,", (uint64_t) (uintptr_t) req);
  bprintf(result, "\"chain_id\": %d,", (uint32_t) req->chain_id);
  bprintf(result, "\"encoding\": \"%s\",", encoding_to_string(req->encoding));
  bprintf(result, "\"exclude_mask\": \"%d\",", (uint32_t) req->node_exclude_mask);
  bprintf(result, "\"method\": \"%s\",", method_to_string(req->method));
  bprintf(result, "\"url\": \"%s\",", req->url);
  if (req->payload.data)
    bprintf(result, "\"payload\": %j,", (json_t){.len = req->payload.len, .start = (char*) req->payload.data, .type = JSON_TYPE_OBJECT});
  bprintf(result, "\"type\": \"%s\"}", data_request_type_to_string(req->type));
}

static void append_pending_requests(buffer_t* buf, c4_state_t* state, bool req_ptr_as_string) {
  bprintf(buf, "\"requests\": [");
  data_request_t* req = c4_state_get_pending_request(state);
  while (req) {
    if (!req->response.data && !req->error) {
      if (buf->data.data[buf->data.len - 1] != '[') bprintf(buf, ",");
      c4i_add_data_request(buf, req, req_ptr_as_string);
    }
    req = req->next;
  }
  bprintf(buf, "]");
}

static char* build_error_or_pending(buffer_t* buf, c4_status_t status, c4_state_t* state, bool req_ptr_as_string) {
  switch (status) {
    case C4_ERROR:
      bprintf(buf, "\"error\": \"%S\"", state->error ? state->error : "unknown error");
      break;
    case C4_PENDING:
      append_pending_requests(buf, state, req_ptr_as_string);
      break;
    default:
      break;
  }
  return bprintf(buf, "}");
}

char* c4i_build_prover_json_status(c4_status_t status, c4_state_t* state,
                                   void* proof_ptr, uint32_t proof_len,
                                   bool req_ptr_as_string) {
  buffer_t buf = {0};
  bprintf(&buf, "{\"status\": \"%s\",", status_to_string(status));
  if (status == C4_SUCCESS) 
    return bprintf(&buf, "\"result\": \"0x%lx\", \"result_len\": %d}", (uint64_t) (uintptr_t) proof_ptr, proof_len);
  return build_error_or_pending(&buf, status, state, req_ptr_as_string);
}

char* c4i_build_verifier_json_status(c4_status_t status, c4_state_t* state,
                                     ssz_ob_t result,
                                     bool req_ptr_as_string) {
  buffer_t buf = {0};
  bprintf(&buf, "{\"status\": \"%s\",", status_to_string(status));
  if (status == C4_SUCCESS) 
    return bprintf(&buf, "\"result\": %Z}", result);
  return build_error_or_pending(&buf, status, state, req_ptr_as_string);
}

/* ── rpc_ctx lifecycle ── */

c4_rpc_ctx_t* c4_rpc_ctx_create(const char* method, const char* params, chain_id_t chain_id,
                                prover_flags_t prover_flags, verify_flags_t verify_flags,
                                bool use_remote_prover) {
  c4_rpc_ctx_t* ctx = safe_calloc(1, sizeof(c4_rpc_ctx_t));
  if (!method || strlen(method) == 0) {
    ctx->error = strdup("method cannot be NULL or empty");
    ctx->phase = RPC_PHASE_DONE;
    return ctx;
  }
  ctx->method             = strdup(method);
  ctx->params             = strdup(params ? params : "[]");
  ctx->chain_id           = chain_id;
  ctx->prover_flags       = prover_flags;
  ctx->verify_flags       = verify_flags;
  ctx->use_remote_prover  = use_remote_prover;
  ctx->phase              = RPC_PHASE_INIT;
  ctx->method_type        = METHOD_UNDEFINED;
  return ctx;
}

static c4_status_t rpc_start_verifier(c4_rpc_ctx_t* ctx, bytes_t proof) {
  char*  method_dup = strdup(ctx->method);
  char*  params_dup = strdup(ctx->params);
  json_t params_json = json_parse(params_dup);

  c4_status_t status = c4_verify_init(&ctx->verifier, proof, method_dup, params_json,
                                      ctx->chain_id, ctx->verify_flags);
  if (status == C4_ERROR) {
    if (!ctx->verifier.method) free(method_dup);
    if (!ctx->verifier.args.start) free(params_dup);
    ctx->phase = RPC_PHASE_DONE;
    ctx->error = strdup(ctx->verifier.state.error ? ctx->verifier.state.error : "verifier init failed");
    return C4_ERROR;
  }
  ctx->phase = RPC_PHASE_VERIFYING;
  return c4_verify(&ctx->verifier);
}

static c4_status_t rpc_handle_proving(c4_rpc_ctx_t* ctx) {
  c4_status_t status = c4_prover_execute(ctx->prover);
  if (status == C4_SUCCESS) {
    ctx->proof       = bytes_dup(ctx->prover->proof);
    ctx->proof_owned = true;
    return rpc_start_verifier(ctx, ctx->proof);
  }
  return status;
}

static c4_status_t rpc_handle_remote_proof(c4_rpc_ctx_t* ctx) {
  if (!ctx->rpc_request) {
    ctx->rpc_request = safe_calloc(1, sizeof(data_request_t));
    ctx->rpc_request->type     = C4_DATA_TYPE_PROVER;
    ctx->rpc_request->chain_id = ctx->chain_id;
    ctx->rpc_request->method   = C4_DATA_METHOD_POST;
    ctx->rpc_request->encoding = C4_DATA_ENCODING_SSZ;
    ctx->rpc_request->url      = strdup("");

    buffer_t payload = {0};
    bprintf(&payload, "{\"method\": \"%s\", \"params\": %s, \"version\": %d}",
            ctx->method, ctx->params, (uint32_t) c4_current_version_number());
    ctx->rpc_request->payload = bytes_dup(payload.data);
    buffer_free(&payload);

    return C4_PENDING;
  }

  if (ctx->rpc_request->error) {
    ctx->error = bprintf(NULL, "Remote prover failed: %s", ctx->rpc_request->error);
    ctx->phase = RPC_PHASE_DONE;
    return C4_ERROR;
  }

  if (ctx->rpc_request->response.data) {
    ctx->proof       = bytes_dup(ctx->rpc_request->response);
    ctx->proof_owned = true;
    return rpc_start_verifier(ctx, ctx->proof);
  }

  return C4_PENDING;
}

static c4_status_t rpc_handle_unproofable(c4_rpc_ctx_t* ctx) {
  if (!ctx->rpc_request) {
    ctx->rpc_request = safe_calloc(1, sizeof(data_request_t));
    ctx->rpc_request->type     = C4_DATA_TYPE_ETH_RPC;
    ctx->rpc_request->chain_id = ctx->chain_id;
    ctx->rpc_request->method   = C4_DATA_METHOD_POST;
    ctx->rpc_request->encoding = C4_DATA_ENCODING_JSON;

    buffer_t payload = {0};
    bprintf(&payload, "{\"method\": \"%s\", \"params\": %s}", ctx->method, ctx->params);
    ctx->rpc_request->payload =payload.data;

    return C4_PENDING;
  }

  if (ctx->rpc_request->error) {
    ctx->error = bprintf(NULL, "RPC request failed: %s", ctx->rpc_request->error);
    ctx->phase = RPC_PHASE_DONE;
    return C4_ERROR;
  }

  if (ctx->rpc_request->response.data) {
    ctx->phase = RPC_PHASE_DONE;
    return C4_SUCCESS;
  }

  return C4_PENDING;
}

c4_status_t c4_rpc_execute(c4_rpc_ctx_t* ctx) {
  if (!ctx) return C4_ERROR;
  switch (ctx->phase) {
    case RPC_PHASE_INIT: {
      ctx->method_type = c4_get_method_type(ctx->chain_id, ctx->method,
                                            json_parse(ctx->params), ctx->verify_flags);
      switch (ctx->method_type) {
        case METHOD_PROOFABLE:
          if (ctx->use_remote_prover) {
            ctx->phase = RPC_PHASE_RPC;
            return rpc_handle_remote_proof(ctx);
          }
          ctx->prover = c4_prover_create(ctx->method, ctx->params, ctx->chain_id, ctx->prover_flags);
          ctx->phase  = RPC_PHASE_PROVING;
          return rpc_handle_proving(ctx);

        case METHOD_LOCAL:
          return rpc_start_verifier(ctx, NULL_BYTES);

        case METHOD_UNPROOFABLE:
          ctx->phase = RPC_PHASE_RPC;
          return rpc_handle_unproofable(ctx);

        case METHOD_UNDEFINED:
        case METHOD_NOT_SUPPORTED:
          ctx->error = bprintf(NULL, "Method %s is not supported", ctx->method);
          ctx->phase = RPC_PHASE_DONE;
          return C4_ERROR;
      }
      break;
    }

    case RPC_PHASE_PROVING:
      return rpc_handle_proving(ctx);

    case RPC_PHASE_VERIFYING: {
      c4_status_t status = c4_verify(&ctx->verifier);
      if (status == C4_SUCCESS || status == C4_ERROR)
        ctx->phase = RPC_PHASE_DONE;
      return status;
    }

    case RPC_PHASE_RPC:
      if (ctx->method_type == METHOD_PROOFABLE)
        return rpc_handle_remote_proof(ctx);
      return rpc_handle_unproofable(ctx);

    case RPC_PHASE_DONE:
      if (ctx->error) return C4_ERROR;
      return C4_SUCCESS;
  }

  return C4_ERROR;
}

c4_state_t* c4_rpc_get_state(c4_rpc_ctx_t* ctx) {
  switch (ctx->phase) {
    case RPC_PHASE_PROVING:
      return ctx->prover ? &ctx->prover->state : NULL;
    case RPC_PHASE_VERIFYING:
      return &ctx->verifier.state;
    default:
      return NULL;
  }
}

void c4_rpc_ctx_free(c4_rpc_ctx_t* ctx) {
  if (!ctx) return;
  if (ctx->method) free(ctx->method);
  if (ctx->params) free(ctx->params);
  if (ctx->prover) c4_prover_free(ctx->prover);
  if (ctx->verifier.method) free((char*) ctx->verifier.method);
  if (ctx->verifier.args.start) free((char*) ctx->verifier.args.start);
  c4_verify_free_data(&ctx->verifier);
  if (ctx->proof_owned && ctx->proof.data) free(ctx->proof.data);
  if (ctx->rpc_request)
    c4_request_free(ctx->rpc_request);
  if (ctx->error) free(ctx->error);
  free(ctx);
}

/* ── Unified JSON status for rpc_ctx ── */

char* c4_rpc_build_json_status(c4_rpc_ctx_t* ctx, bool req_ptr_as_string) {
  c4_status_t status = c4_rpc_execute(ctx);
  buffer_t    buf    = {0};

  bprintf(&buf, "{\"status\": \"%s\",", status_to_string(status));

  switch (status) {
    case C4_SUCCESS:
      if (ctx->phase == RPC_PHASE_DONE && ctx->method_type == METHOD_UNPROOFABLE) 
        bprintf(&buf, "\"result\": %r", ctx->rpc_request->response);
      else 
        bprintf(&buf, "\"result\": %Z", ctx->verifier.data);
      break;

    case C4_ERROR:
      if (ctx->error)
        bprintf(&buf, "\"error\": \"%S\"", ctx->error);
      else if (ctx->phase == RPC_PHASE_VERIFYING || ctx->phase == RPC_PHASE_DONE)
        bprintf(&buf, "\"error\": \"%S\"", ctx->verifier.state.error);
      else if (ctx->prover && ctx->prover->state.error)
        bprintf(&buf, "\"error\": \"%S\"", ctx->prover->state.error);
      else
        bprintf(&buf, "\"error\": \"unknown error\"");
      break;

    case C4_PENDING:
      if (ctx->phase == RPC_PHASE_RPC && ctx->rpc_request) {
        bprintf(&buf, "\"requests\": [");
        if (!ctx->rpc_request->response.data && !ctx->rpc_request->error)
          c4i_add_data_request(&buf, ctx->rpc_request, req_ptr_as_string);
        bprintf(&buf, "]");
      }
      else {
        c4_state_t* state = c4_rpc_get_state(ctx);
        if (state)
          append_pending_requests(&buf, state, req_ptr_as_string);
        else
          bprintf(&buf, "\"requests\": []");
      }
      break;
  }

  return bprintf(&buf, "}");
}
