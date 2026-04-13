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
#include "bytes.h"
#include "compat.h"
#include "plugin.h"
#ifdef CHAIN_ETH
#include "sync_committee.h"
#endif
#include <stdlib.h>
#include <string.h>
#define MAX_PROOF_DEPTH 10 // max number of proofs requested by the verifier

static bool bytes_memmem(bytes_t p, const char* needle) {
  size_t nl = strlen(needle);
  if (!p.data || p.len < nl) return false;
#ifdef __GLIBC__
  return memmem(p.data, p.len, needle, nl) != NULL;
#else
  for (size_t i = 0; i + nl <= p.len; i++) {
    if (memcmp(p.data + i, needle, nl) == 0) return true;
  }
  return false;
#endif
}

/** Appends `,"key":"csv_value"` (the CSV is transmitted as-is; server splits on comma). */
static void append_proxy_fields(buffer_t* payload, c4_rpc_ctx_t* ctx) {
  if (ctx->prover_mode != C4_PROVER_MODE_PROXY) return;
  if (ctx->proxy_rpc_urls && *ctx->proxy_rpc_urls && !strchr(ctx->proxy_rpc_urls, '"'))
    bprintf(payload, ",\"rpc\":\"%s\"", ctx->proxy_rpc_urls);
  if (ctx->proxy_beacon_urls && *ctx->proxy_beacon_urls && !strchr(ctx->proxy_beacon_urls, '"'))
    bprintf(payload, ",\"beacon\":\"%s\"", ctx->proxy_beacon_urls);
}

static void enrich_pending_prover_requests(c4_rpc_ctx_t* ctx) {
  for (data_request_t* req = ctx->verifier.state.requests; req; req = req->next) {
    if (req->type != C4_DATA_TYPE_PROVER || !c4_state_is_pending(req)) continue;
    if (req->method != C4_DATA_METHOD_POST || !req->payload.data || req->payload.len < 2) continue;
    if (bytes_memmem(req->payload, "\"version\"")) continue;
    if (req->payload.data[req->payload.len - 1] != (uint8_t) '}') continue;

    buffer_t out = {0};
    buffer_append(&out, bytes(req->payload.data, req->payload.len - 1));
    c4_append_prover_request_props(&out, ctx->chain_id, ctx->prover_flags, ctx->witness_keys);
    append_proxy_fields(&out, ctx);
    bprintf(&out, "}");
    safe_free(req->payload.data);
    req->payload = out.data;
  }
}
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
                                c4_prover_mode_t prover_mode) {
  c4_rpc_ctx_t* ctx = safe_calloc(1, sizeof(c4_rpc_ctx_t));
  if (!method || strlen(method) == 0) {
    ctx->error = strdup("method cannot be NULL or empty");
    ctx->phase = RPC_PHASE_DONE;
    return ctx;
  }
  if (prover_mode < C4_PROVER_MODE_LOCAL || prover_mode > C4_PROVER_MODE_LIGHT_CLIENT) {
    ctx->error = strdup("invalid prover_mode value");
    ctx->phase = RPC_PHASE_DONE;
    return ctx;
  }
  ctx->method       = strdup(method);
  ctx->params       = strdup(params ? params : "[]");
  ctx->chain_id     = chain_id;
  ctx->prover_flags = prover_flags;
  ctx->verify_flags = verify_flags;
  ctx->prover_mode  = prover_mode;
  ctx->phase        = RPC_PHASE_INIT;
  ctx->method_type  = METHOD_UNDEFINED;
  if (prover_mode == C4_PROVER_MODE_HYBRID || prover_mode == C4_PROVER_MODE_LIGHT_CLIENT)
    ctx->prover_flags |= C4_PROVER_FLAG_HYBRID;
  if (prover_mode == C4_PROVER_MODE_LIGHT_CLIENT)
    ctx->prover_flags |= C4_PROVER_FLAG_LIGHT_CLIENT;
  return ctx;
}

void c4_rpc_ctx_set_proxy_urls(c4_rpc_ctx_t* ctx, const char* rpc_urls, const char* beacon_urls) {
  if (!ctx) return;
  if (ctx->proxy_rpc_urls) {
    free(ctx->proxy_rpc_urls);
    ctx->proxy_rpc_urls = NULL;
  }
  if (ctx->proxy_beacon_urls) {
    free(ctx->proxy_beacon_urls);
    ctx->proxy_beacon_urls = NULL;
  }
  if (rpc_urls && *rpc_urls) ctx->proxy_rpc_urls = strdup(rpc_urls);
  if (beacon_urls && *beacon_urls) ctx->proxy_beacon_urls = strdup(beacon_urls);
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

static c4_status_t rpc_handle_verifying(c4_rpc_ctx_t* ctx);

static c4_status_t rpc_start_verifier(c4_rpc_ctx_t* ctx, bytes_t proof) {
  verify_flags_t vf = ctx->verify_flags;
  if (ctx->prover_mode == C4_PROVER_MODE_REMOTE || ctx->prover_mode == C4_PROVER_MODE_PROXY || ctx->prover_mode == C4_PROVER_MODE_HYBRID ||
      ctx->prover_mode == C4_PROVER_MODE_LIGHT_CLIENT)
    vf |= VERIFY_FLAG_REMOTE_PROVER;
  if (ctx->prover_mode == C4_PROVER_MODE_HYBRID || ctx->prover_mode == C4_PROVER_MODE_LIGHT_CLIENT)
    vf |= VERIFY_FLAG_HYBRID;
  c4_status_t status = c4_verify_init(&ctx->verifier, proof, ctx->method, json_parse(ctx->params),
                                      ctx->chain_id, vf);
  if (status == C4_ERROR) {
    ctx->phase = RPC_PHASE_DONE;
    ctx->error = strdup(ctx->verifier.state.error ? ctx->verifier.state.error : "verifier init failed");
    return C4_ERROR;
  }
  if (ctx->witness_keys.data && ctx->witness_keys.len)
    ctx->verifier.witness_keys = bytes_dup(ctx->witness_keys);

  if (vf & VERIFY_FLAG_PROOF_ONLY) {
    ctx->phase            = RPC_PHASE_DONE;
    ctx->verifier.data    = (ssz_ob_t) {.def = &ssz_bytes_list, .bytes = proof};
    ctx->verifier.success = true;
    return C4_SUCCESS;
  }

  ctx->phase = RPC_PHASE_VERIFYING;
  return rpc_handle_verifying(ctx);
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

static c4_status_t rpc_handle_remote_proof(c4_rpc_ctx_t* ctx) {
  if (!ctx->rpc_state.requests) {
    ctx->rpc_state.requests           = safe_calloc(1, sizeof(data_request_t));
    ctx->rpc_state.requests->type     = C4_DATA_TYPE_PROVER;
    ctx->rpc_state.requests->chain_id = ctx->chain_id;
    ctx->rpc_state.requests->method   = C4_DATA_METHOD_POST;
    ctx->rpc_state.requests->encoding = C4_DATA_ENCODING_SSZ;
    buffer_t method_buf               = {0};
    buffer_t params_buf               = {0};
    buffer_t payload                  = {0};

    c4_get_prover_payload(ctx->chain_id, ctx->method, ctx->params, ctx->verify_flags, &method_buf, &params_buf);

    bprintf(&payload, "{\"method\": \"%s\", \"params\": %s",
            method_buf.data.len ? (char*) method_buf.data.data : ctx->method,
            params_buf.data.len ? (char*) params_buf.data.data : ctx->params);
    buffer_free(&method_buf);
    buffer_free(&params_buf);
    c4_append_prover_request_props(&payload, ctx->chain_id, ctx->prover_flags, ctx->witness_keys);
    append_proxy_fields(&payload, ctx);
    bprintf(&payload, "}");
    ctx->rpc_state.requests->payload = payload.data;

    return C4_PENDING;
  }

  // fallback to local proving in case of an error
  if (ctx->rpc_state.requests->error) {
    c4_request_free(ctx->rpc_state.requests);
    ctx->rpc_state.requests = NULL;
    ctx->prover             = c4_prover_create(ctx->method, ctx->params, ctx->chain_id, ctx->prover_flags);
    ctx->phase              = RPC_PHASE_PROVING;
    return rpc_handle_proving(ctx);
  }

  if (ctx->rpc_state.requests->response.data) {
    ctx->proof       = ctx->rpc_state.requests->response;
    ctx->proof_owned = false; // proof is still owned by the rpc_state.requests
    return rpc_start_verifier(ctx, ctx->proof);
  }

  return C4_PENDING;
}

static c4_status_t rpc_handle_unproofable(c4_rpc_ctx_t* ctx) {
  if (!ctx->rpc_state.requests) {
    buffer_t payload = {0};
    bprintf(&payload, "{\"jsonrpc\": \"2.0\",\"id\": 1, \"method\": \"%s\", \"params\": %s}", ctx->method, ctx->params);

    ctx->rpc_state.requests           = safe_calloc(1, sizeof(data_request_t));
    ctx->rpc_state.requests->type     = C4_DATA_TYPE_ETH_RPC;
    ctx->rpc_state.requests->chain_id = ctx->chain_id;
    ctx->rpc_state.requests->method   = C4_DATA_METHOD_POST;
    ctx->rpc_state.requests->encoding = C4_DATA_ENCODING_JSON;
    ctx->rpc_state.requests->payload  = payload.data;

    return C4_PENDING;
  }

  if (ctx->rpc_state.requests->error) {
    ctx->error = bprintf(NULL, "RPC request failed: %s", ctx->rpc_state.requests->error);
    ctx->phase = RPC_PHASE_DONE;
    return C4_ERROR;
  }

  if (ctx->rpc_state.requests->response.data) {
    // make it null terminated
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
      ctx->phase         = RPC_PHASE_DONE;
      ctx->verifier.data = (ssz_ob_t) {.def = &ssz_json_def, .bytes = bytes_dup(bytes((uint8_t*) rpc_result.start, rpc_result.len))};
      ctx->verifier.flags |= VERIFY_FLAG_FREE_DATA;
      safe_free(tmp);
      return C4_SUCCESS;
    }
    else {
      safe_free(tmp);
      ctx->error = bprintf(NULL, "RPC result is not found");
      ctx->phase = RPC_PHASE_DONE;
      return C4_ERROR;
    }
  }

  return C4_PENDING;
}

/* ── Local prover for verifier-emitted PROVER requests (PAP) ── */

static data_request_t* find_pending_prover_request(c4_state_t* state) {
  for (data_request_t* req = state->requests; req; req = req->next) {
    if (req->type == C4_DATA_TYPE_PROVER && c4_state_is_pending(req))
      return req;
  }
  return NULL;
}

static prover_ctx_t* create_prover_from_request(data_request_t* req, chain_id_t chain_id, prover_flags_t prover_flags) {
  if (!req->payload.data || !req->payload.len || req->payload.len > (UINT32_MAX - 1)) {
    req->error = strdup("Empty or oversized prover request payload");
    return NULL;
  }
  char* tmp = safe_malloc(req->payload.len + 1);
  memcpy(tmp, req->payload.data, req->payload.len);
  tmp[req->payload.len] = '\0';

  json_t root = json_parse(tmp);
  if (root.type != JSON_TYPE_OBJECT) {
    safe_free(tmp);
    req->error = strdup("Invalid prover request payload");
    return NULL;
  }
  json_t method = json_get(root, "method");
  json_t params = json_get(root, "params");

  if (method.type != JSON_TYPE_STRING || params.type == JSON_TYPE_NOT_FOUND) {
    safe_free(tmp);
    req->error = strdup("Invalid prover request payload");
    return NULL;
  }

  char* method_str = bprintf(NULL, "%j", method);
  char* params_str = bprintf(NULL, "%j", params);
  safe_free(tmp);

  prover_ctx_t* pctx = c4_prover_create(method_str, params_str, chain_id, prover_flags);
  safe_free(method_str);
  safe_free(params_str);
  if (!pctx) {
    req->error = strdup("Failed to create local prover");
    return NULL;
  }
  pctx->client_state = c4_get_client_state(chain_id);
  if (pctx->state.error) {
    req->error        = pctx->state.error;
    pctx->state.error = NULL;
    c4_prover_free(pctx);
    return NULL;
  }
  return pctx;
}

static void free_request_prover(c4_rpc_ctx_t* ctx) {
  if (!ctx->request_prover) return;
  c4_prover_free(ctx->request_prover->ctx);
  safe_free(ctx->request_prover);
  ctx->request_prover = NULL;
}

static bool is_remote_delegated_method(data_request_t* req) {
  if (req->method != C4_DATA_METHOD_POST) return req->method == C4_DATA_METHOD_GET;
  if (!req->payload.data || !req->payload.len || req->payload.len > (UINT32_MAX - 1)) return false;
  char* tmp = safe_malloc(req->payload.len + 1);
  memcpy(tmp, req->payload.data, req->payload.len);
  tmp[req->payload.len] = '\0';
  json_t root   = json_parse(tmp);
  json_t method = json_get(root, "method");
  bool   remote = false;
  if (method.type == JSON_TYPE_STRING) {
    char* m = bprintf(NULL, "%j", method);
    remote  = strcmp(m, "eth_getBlockHeader") == 0 ||
             strcmp(m, "eth_getBlockByNumber") == 0 ||
             strcmp(m, "eth_getBlockByHash") == 0;
    safe_free(m);
  }
  safe_free(tmp);
  return remote;
}

// returns true if there is a prover request which needs to be handled
static bool check_prover_requests(c4_rpc_ctx_t* ctx) {
  if (ctx->request_prover) return true;
  if (ctx->prover_mode == C4_PROVER_MODE_REMOTE || ctx->prover_mode == C4_PROVER_MODE_PROXY) {
    enrich_pending_prover_requests(ctx);
    return false;
  }
  data_request_t* prover_req = find_pending_prover_request(&ctx->verifier.state);
  if (!prover_req) return false;
  if ((ctx->prover_mode == C4_PROVER_MODE_HYBRID || ctx->prover_mode == C4_PROVER_MODE_LIGHT_CLIENT) && is_remote_delegated_method(prover_req)) {
    enrich_pending_prover_requests(ctx);
    return false;
  }
  prover_ctx_t* pctx = create_prover_from_request(prover_req, ctx->chain_id, ctx->prover_flags);
  if (!pctx) return false;
  request_prover_t* rp = safe_calloc(1, sizeof(request_prover_t));
  rp->request          = prover_req;
  rp->ctx              = pctx;
  ctx->request_prover  = rp;
  return true;
}

static c4_status_t handle_request_prover(c4_rpc_ctx_t* ctx) {
  request_prover_t* rp     = ctx->request_prover;
  c4_status_t       status = c4_prover_execute(rp->ctx);
  switch (status) {
    case C4_SUCCESS:
      rp->request->response            = bytes_dup(rp->ctx->proof);
      rp->request->response_node_index = 0;
      free_request_prover(ctx);
      return C4_SUCCESS;
    case C4_ERROR:
      rp->request->error = strdup(rp->ctx->state.error ? rp->ctx->state.error : "local prover failed");
      free_request_prover(ctx);
      return C4_ERROR;
    default:
      return C4_PENDING;
  }
}

static c4_status_t rpc_handle_verifying(c4_rpc_ctx_t* ctx) {
  for (int i = 0; i < MAX_PROOF_DEPTH; i++) {
    if (ctx->request_prover) // if the verifier requested proof, we need to handle it first.
      TRY_ASYNC(handle_request_prover(ctx));

    c4_status_t status = c4_verify(&ctx->verifier);
    if (status == C4_SUCCESS || status == C4_ERROR) { // are we done?
      ctx->phase = RPC_PHASE_DONE;
      return status;
    }
    // so there are pending requests, let's check if the verifier requested proof again.
    // if there was a prover request, we need repeat the loop to handle it.
    if (!check_prover_requests(ctx)) return C4_PENDING;
  }
  ctx->error = bprintf(NULL, "Max proof depth reached");
  ctx->phase = RPC_PHASE_DONE;
  return C4_ERROR;
}

c4_status_t c4_rpc_execute(c4_rpc_ctx_t* ctx) {
  if (!ctx) return C4_ERROR;
  switch (ctx->phase) {
    case RPC_PHASE_INIT: {
      ctx->method_type = c4_get_method_type(ctx->chain_id, ctx->method,
                                            json_parse(ctx->params), ctx->verify_flags);
      switch (ctx->method_type) {
        case METHOD_PROOFABLE:
          if (ctx->proof.data) // the user passed in a proof
            return rpc_start_verifier(ctx, ctx->proof);
          else if (ctx->prover_mode == C4_PROVER_MODE_REMOTE || ctx->prover_mode == C4_PROVER_MODE_PROXY) {
            ctx->phase = RPC_PHASE_RPC;
            return rpc_handle_remote_proof(ctx);
          }
          else { // local or hybrid: create the proof locally
            ctx->prover               = c4_prover_create(ctx->method, ctx->params, ctx->chain_id, ctx->prover_flags);
            ctx->phase                = RPC_PHASE_PROVING;
            ctx->prover->client_state = c4_get_client_state(ctx->chain_id);
            return rpc_handle_proving(ctx);
          }

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

    case RPC_PHASE_VERIFYING:
      return rpc_handle_verifying(ctx);

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
      if (ctx->request_prover)
        return &ctx->request_prover->ctx->state;
      return &ctx->verifier.state;
    case RPC_PHASE_RPC:
      return &ctx->rpc_state;
    default:
      return NULL;
  }
}

void c4_rpc_ctx_free(c4_rpc_ctx_t* ctx) {
  if (!ctx) return;
  free_request_prover(ctx);
  if (ctx->method) free(ctx->method);
  if (ctx->params) free(ctx->params);
  if (ctx->prover) c4_prover_free(ctx->prover);
  c4_verify_free_data(&ctx->verifier);
  if (ctx->proof_owned && ctx->proof.data) free(ctx->proof.data);
  if (ctx->witness_keys.data) free(ctx->witness_keys.data);
  if (ctx->proxy_rpc_urls) free(ctx->proxy_rpc_urls);
  if (ctx->proxy_beacon_urls) free(ctx->proxy_beacon_urls);
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
