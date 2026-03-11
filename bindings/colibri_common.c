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
#include "plugin.h"
#include "version.h"
#ifdef CHAIN_ETH
#include "sync_committee.h"
#endif
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
    bprintf(result, "\"payload\": %j,", (json_t) {.len = req->payload.len, .start = (char*) req->payload.data, .type = JSON_TYPE_OBJECT});
  bprintf(result, "\"type\": \"%s\"}", data_request_type_to_string(req->type));
}

static void append_pending_requests(buffer_t* buf, c4_state_t* state, bool req_ptr_as_string) {
  bprintf(buf, "\"requests\": [");
  for (data_request_t* req = c4_state_get_pending_request(state); req; req = req->next) {
    if (!req->response.data && !req->error) {
      if (buf->data.data[buf->data.len - 1] != '[') bprintf(buf, ",");
      c4i_add_data_request(buf, req, req_ptr_as_string);
    }
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
  if (status == C4_SUCCESS) {
    if (req_ptr_as_string)
      return bprintf(&buf, "\"result\": \"0x%lx\", \"result_len\": %d}", (uint64_t) (uintptr_t) proof_ptr, proof_len);
    else
      return bprintf(&buf, "\"result\": %l, \"result_len\": %d}", (uint64_t) (uintptr_t) proof_ptr, proof_len);
  }
  return build_error_or_pending(&buf, status, state, req_ptr_as_string);
}

char* c4i_build_verifier_json_status(c4_status_t status, c4_state_t* state,
                                     ssz_ob_t result,
                                     bool     req_ptr_as_string) {
  buffer_t buf = {0};
  bprintf(&buf, "{\"status\": \"%s\",", status_to_string(status));
  return status == C4_SUCCESS
             ? bprintf(&buf, "\"result\": %Z}", result)
             : build_error_or_pending(&buf, status, state, req_ptr_as_string);
}

/* ── Standalone checkpoint setter ── */

void c4_set_checkpoint(chain_id_t chain_id, const char* checkpoint_hex) {
#ifdef CHAIN_ETH
  if (checkpoint_hex && strlen(checkpoint_hex) == 66) {
    bytes32_t checkpoint;
    if (hex_to_bytes(checkpoint_hex + 2, 64, bytes(checkpoint, 32)) == 32)
      c4_eth_set_trusted_checkpoint(chain_id, checkpoint);
  }
#else
  (void) chain_id;
  (void) checkpoint_hex;
#endif
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
  ctx->method            = strdup(method);
  ctx->params            = strdup(params ? params : "[]");
  ctx->chain_id          = chain_id;
  ctx->prover_flags      = prover_flags;
  ctx->verify_flags      = verify_flags;
  ctx->use_remote_prover = use_remote_prover;
  ctx->phase             = RPC_PHASE_INIT;
  ctx->method_type       = METHOD_UNDEFINED;
  return ctx;
}

void c4_rpc_ctx_set_witness_keys(c4_rpc_ctx_t* ctx, const char* keys_hex) {
  if (!ctx) return;
  if (ctx->witness_keys.data) {
    free(ctx->witness_keys.data);
    ctx->witness_keys = NULL_BYTES;
  }
  if (keys_hex && strlen(keys_hex) > 4 && keys_hex[0] == '0' && keys_hex[1] == 'x') {
    uint32_t hex_len = strlen(keys_hex) - 2;
    if (hex_len % 2 != 0) return;
    ctx->witness_keys = bytes(safe_malloc(hex_len / 2), hex_len / 2);
    if (hex_to_bytes(keys_hex + 2, -1, ctx->witness_keys) < 0) {
      free(ctx->witness_keys.data);
      ctx->witness_keys = NULL_BYTES;
    }
  }
}

static c4_status_t rpc_start_verifier(c4_rpc_ctx_t* ctx, bytes_t proof) {
  c4_status_t status = c4_verify_init(&ctx->verifier, proof, ctx->method, json_parse(ctx->params),
                                      ctx->chain_id, ctx->verify_flags);
  if (status == C4_ERROR) {
    ctx->phase = RPC_PHASE_DONE;
    ctx->error = strdup(ctx->verifier.state.error ? ctx->verifier.state.error : "verifier init failed");
    return C4_ERROR;
  }
  if (ctx->witness_keys.data && ctx->witness_keys.len)
    ctx->verifier.witness_keys = bytes_dup(ctx->witness_keys);

  ctx->phase = RPC_PHASE_VERIFYING;
  return c4_verify(&ctx->verifier);
}

static c4_status_t rpc_handle_proving(c4_rpc_ctx_t* ctx) {
  c4_status_t status = c4_prover_execute(ctx->prover);
  if (status == C4_SUCCESS) {
    ctx->proof       = ctx->prover->proof;
    ctx->proof_owned = false;
    //    bytes_write(ctx->proof, fopen("last_proof.ssz", "wb"), true);
    return rpc_start_verifier(ctx, ctx->proof);
  }
  return status;
}

static void cleanup_remote_prover_params(buffer_t* buf, const char* method, const char* params) {
  json_t arr = json_parse(params);
  if (strcmp(method, "eth_verifyLogs") == 0 && arr.type == JSON_TYPE_ARRAY) {
    bprintf(buf, "[");
    for (int i = 0; i < json_len(arr); i++) {
      if (i > 0) bprintf(buf, ",");
      json_t item     = json_at(arr, i);
      json_t tx_index = json_get(item, "transactionIndex");
      json_t block_nr = json_get(item, "blockNumber");
      bprintf(buf, "{\"transactionIndex\":%j,\"blockNumber\":%j}", tx_index, block_nr);
    }
    bprintf(buf, "]");
  }
  /*
    else if (strcmp(method, "colibri_simulateTransaction") == 0 && arr.type == JSON_TYPE_ARRAY) {
      int len = json_len(arr);
      if (len > 2) len = 2;
      bprintf(buf, "[");
      for (int i = 0; i < len; i++) {
        if (i > 0) bprintf(buf, ",");
        bprintf(buf, "%j", json_at(arr, i));
      }
      bprintf(buf, "]");
    }
    */
  else
    bprintf(buf, "%s", params);
}

static c4_status_t rpc_handle_remote_proof(c4_rpc_ctx_t* ctx) {
  if (!ctx->rpc_state.requests) {
    ctx->rpc_state.requests           = safe_calloc(1, sizeof(data_request_t));
    ctx->rpc_state.requests->type     = C4_DATA_TYPE_PROVER;
    ctx->rpc_state.requests->chain_id = ctx->chain_id;
    ctx->rpc_state.requests->method   = C4_DATA_METHOD_POST;
    ctx->rpc_state.requests->encoding = C4_DATA_ENCODING_SSZ;

    buffer_t payload = {0};
    bprintf(&payload, "{\"method\": \"%s\", \"params\": ", ctx->method);
    cleanup_remote_prover_params(&payload, ctx->method, ctx->params);
    bprintf(&payload, ", \"version\": %d", (uint32_t) c4_current_version_number());

    storage_plugin_t storage = {0};
    c4_get_storage_config(&storage);
    buffer_t state_buf = {0};
    char     name[100];
    sbprintf(name, "states_%l", (uint64_t) ctx->chain_id);
    if (storage.get && storage.get(name, &state_buf) && state_buf.data.data && state_buf.data.len)
      bprintf(&payload, ", \"c4\": \"0x%x\"", state_buf.data);
    buffer_free(&state_buf);

    if (ctx->prover_flags & C4_PROVER_FLAG_ZK_PROOF)
      bprintf(&payload, ", \"zk_proof\": true");
    if (ctx->prover_flags & C4_PROVER_FLAG_INCLUDE_CODE)
      bprintf(&payload, ", \"include_code\": true");
    if (ctx->witness_keys.data && ctx->witness_keys.len)
      bprintf(&payload, ", \"signers\": \"0x%x\"", ctx->witness_keys);

    bprintf(&payload, "}");
    ctx->rpc_state.requests->payload = payload.data;

    return C4_PENDING;
  }

  if (ctx->rpc_state.requests->error) {
    c4_request_free(ctx->rpc_state.requests);
    ctx->rpc_state.requests = NULL;
    ctx->prover             = c4_prover_create(ctx->method, ctx->params, ctx->chain_id, ctx->prover_flags);
    ctx->phase              = RPC_PHASE_PROVING;
    return rpc_handle_proving(ctx);
  }

  if (ctx->rpc_state.requests->response.data) {
    ctx->proof       = ctx->rpc_state.requests->response;
    ctx->proof_owned = false;
    return rpc_start_verifier(ctx, ctx->proof);
  }

  return C4_PENDING;
}

static c4_status_t rpc_handle_unproofable(c4_rpc_ctx_t* ctx) {
  if (!ctx->rpc_state.requests) {
    ctx->rpc_state.requests           = safe_calloc(1, sizeof(data_request_t));
    ctx->rpc_state.requests->type     = C4_DATA_TYPE_ETH_RPC;
    ctx->rpc_state.requests->chain_id = ctx->chain_id;
    ctx->rpc_state.requests->method   = C4_DATA_METHOD_POST;
    ctx->rpc_state.requests->encoding = C4_DATA_ENCODING_JSON;

    buffer_t payload = {0};
    bprintf(&payload, "{\"method\": \"%s\", \"params\": %s}", ctx->method, ctx->params);
    ctx->rpc_state.requests->payload = payload.data;

    return C4_PENDING;
  }

  if (ctx->rpc_state.requests->error) {
    ctx->error = bprintf(NULL, "RPC request failed: %s", ctx->rpc_state.requests->error);
    ctx->phase = RPC_PHASE_DONE;
    return C4_ERROR;
  }

  if (ctx->rpc_state.requests->response.data) {
    bytes_t resp = ctx->rpc_state.requests->response;
    char*   tmp  = safe_malloc(resp.len + 1);
    memcpy(tmp, resp.data, resp.len);
    tmp[resp.len] = '\0';

    json_t rpc_json  = json_parse(tmp);
    json_t rpc_error = json_get(rpc_json, "error");

    if (rpc_error.type != JSON_TYPE_NOT_FOUND) {
      json_t msg = json_get(rpc_error, "message");
      ctx->error = (msg.type == JSON_TYPE_STRING)
                       ? bprintf(NULL, "%j", msg)
                       : bprintf(NULL, "RPC error");
      safe_free(tmp);
      ctx->phase = RPC_PHASE_DONE;
      return C4_ERROR;
    }

    json_t rpc_result = json_get(rpc_json, "result");
    if (rpc_result.type != JSON_TYPE_NOT_FOUND) {
      bytes_t result_bytes = bytes_dup(bytes((uint8_t*) rpc_result.start, rpc_result.len));
      free(ctx->rpc_state.requests->response.data);
      ctx->rpc_state.requests->response = result_bytes;
    }

    safe_free(tmp);
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
          if (ctx->proof.data)
            return rpc_start_verifier(ctx, ctx->proof);
          if (ctx->use_remote_prover) {
            ctx->phase = RPC_PHASE_RPC;
            return rpc_handle_remote_proof(ctx);
          }
          ctx->prover = c4_prover_create(ctx->method, ctx->params, ctx->chain_id, ctx->prover_flags);
          ctx->phase  = RPC_PHASE_PROVING;
#ifdef TEST
          storage_plugin_t storage = {0};
          c4_get_storage_config(&storage);
          char     name[100];
          buffer_t state_buf = {0};
          sbprintf(name, "states_%l", (uint64_t) ctx->chain_id);
          if (storage.get && storage.get(name, &state_buf) && state_buf.data.data && state_buf.data.len)
            ctx->prover->client_state = state_buf.data;
          else
            buffer_free(&state_buf);
#endif
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
    case RPC_PHASE_RPC:
      return &ctx->rpc_state;
    default:
      return NULL;
  }
}

void c4_rpc_ctx_free(c4_rpc_ctx_t* ctx) {
  if (!ctx) return;
  if (ctx->method) free(ctx->method);
  if (ctx->params) free(ctx->params);
  if (ctx->prover) c4_prover_free(ctx->prover);
  c4_verify_free_data(&ctx->verifier);
  if (ctx->proof_owned && ctx->proof.data) free(ctx->proof.data);
  if (ctx->witness_keys.data) free(ctx->witness_keys.data);
  if (ctx->rpc_state.requests)
    c4_request_free(ctx->rpc_state.requests);
  if (ctx->rpc_state.error) safe_free(ctx->rpc_state.error);
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
        bprintf(&buf, "\"result\": %r", ctx->rpc_state.requests->response);
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

    case C4_PENDING: {
      c4_state_t* state = c4_rpc_get_state(ctx);
      if (state)
        append_pending_requests(&buf, state, req_ptr_as_string);
      else
        bprintf(&buf, "\"requests\": []");
      break;
    }
  }

  return bprintf(&buf, "}");
}
