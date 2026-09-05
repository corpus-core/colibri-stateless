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

#include "cl_req.h"
#include "beacon_types.h"
#include "crypto.h"
#include "eth_compute_units.h"
#include "json.h"
#include "ssz.h"
#include <string.h>

/** Empty `?slot=` / `?parent_root=` answers must not stick for `DEFAULT_TTL`. */
#define CL_HEADER_QUERY_TTL 6u

#define JSON_BEACON_HEADER_MESSAGE       "{slot:suint,proposer_index:suint,parent_root:bytes32,state_root:bytes32,body_root:bytes32}"
#define JSON_BEACON_HEADER_ENTRY         "{root:bytes32,canonical?:bool,header:{message:" JSON_BEACON_HEADER_MESSAGE "}}"
#define JSON_BEACON_HEADER_OBJECT        "{data:" JSON_BEACON_HEADER_ENTRY "}"
#define JSON_BEACON_HEADER_LIST          "{data:[" JSON_BEACON_HEADER_ENTRY "]}"
#define JSON_BEACON_FINALITY             "{data:{current_justified:{epoch:suint,root:bytes32},finalized:{epoch:suint,root:bytes32}}}"
#define JSON_BEACON_HISTORICAL_SUMMARIES "{data:{historical_summaries:[{block_summary_root:bytes32,state_summary_root:bytes32}],proof:[bytes32]}}"

/**
 * Shared enqueue-and-probe routine for all request variants.
 *
 * Computes the request-id from `(path, query)`, looks up the request in
 * `ctx->state`, and either creates it (returning `C4_PENDING`) or reports
 * the current status. Never touches `ctx->state.error`; error reporting is
 * the caller's responsibility (either by threading the error through the
 * returned `data_request_t.error`, or by wrapping into a `THROW_ERROR`).
 *
 * Ownership: on the "new request" path, the URL buffer is transferred to
 * `data_request_t.url` (must NOT be freed here). On the "existing request"
 * path, the local buffer is freed.
 *
 * Compute-unit accounting: `cu_cost` is added to `ctx` only when a new
 * `data_request_t` is enqueued -- looking up an already-completed request
 * (success or sticky error) is free. That keeps CU accounting tied to
 * network work, not to sticky-error re-probes on async re-entries.
 */
static c4_status_t send_request_impl(prover_ctx_t*           ctx,
                                     char*                   path,
                                     char*                   query,
                                     uint32_t                ttl,
                                     data_request_type_t     data_type,
                                     data_request_encoding_t data_encoding,
                                     uint32_t                client_type,
                                     uint32_t                cu_cost,
                                     data_request_t**        out_req) {
  bytes32_t id     = {0};
  buffer_t  buffer = {0};
  buffer_add_chars(&buffer, path);
  if (query) {
    buffer_add_chars(&buffer, "?");
    buffer_add_chars(&buffer, query);
  }
  sha256(buffer.data, id);
  data_request_t* data_request = c4_state_get_data_request_by_id(&ctx->state, id);
  if (data_request) {
    buffer_free(&buffer);
    if (out_req) *out_req = data_request;
    if (c4_state_is_pending(data_request)) return C4_PENDING;
    if (!data_request->error && data_request->response.data) return C4_SUCCESS;
    return C4_ERROR;
  }
  eth_cu_add(ctx, cu_cost);
  data_request = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
  memcpy(data_request->id, id, 32);
  data_request->url                   = (char*) buffer.data.data;
  data_request->encoding              = data_encoding;
  data_request->method                = C4_DATA_METHOD_GET;
  data_request->type                  = data_type;
  data_request->ttl                   = ttl;
  data_request->preferred_client_type = client_type;
  c4_state_add_request(&ctx->state, data_request);
  if (out_req) *out_req = data_request;
  return C4_PENDING;
}

static bool convert_to_ssz(prover_ctx_t* ctx, data_request_t* data_request, ssz_ob_t* result) {
  json_t json_result = json_parse((const char*) result->bytes.data);
  json_t data        = json_get(json_result, "data");

  if (data.type != JSON_TYPE_OBJECT) {
    c4_state_add_error(&ctx->state, "Invalid JSON response");
    return false;
  }

  if (result->def == NULL) {
    uint64_t            slot  = json_get_uint64(json_get(data, "message"), "slot");
    const chain_spec_t* chain = c4_eth_get_chain_spec(ctx->chain_id);
    if (chain == NULL) {
      c4_state_add_error(&ctx->state, "unsupported chain id!");
      return false;
    }
    fork_id_t fork = c4_chain_fork_id(ctx->chain_id, epoch_for_slot(slot, chain));
    result->def    = eth_ssz_type_for_fork(ETH_SSZ_SIGNED_BEACON_BLOCK_CONTAINER, fork, ctx->chain_id);
    if (!result->def) {
      c4_state_add_error(&ctx->state, "no definition for ETH_SSZ_SIGNED_BEACON_BLOCK_CONTAINER!");
      return false;
    }
  }
  ssz_ob_t ssz_result = ssz_from_json(data, result->def, &ctx->state);
  if (!ssz_result.bytes.data) {
    c4_state_add_error(&ctx->state, "Invalid SSZ response");
    return false;
  }

  safe_free(data_request->response.data);
  data_request->response = ssz_result.bytes;
  result->bytes          = ssz_result.bytes;
  return true;
}

static bool beacon_slot_missing_error(const char* error) {
  if (!error) return false;
  return strstr(error, "404") != NULL || strstr(error, "not been found") != NULL;
}

static c4_status_t c4_send_beacon_json_with_client_type(prover_ctx_t* ctx, char* path, char* query, uint32_t ttl, json_t* result, uint32_t client_type, data_request_t** out_req);
static c4_status_t c4_send_beacon_ssz_with_client_type(prover_ctx_t* ctx, char* path, char* query, const ssz_def_t* def, uint32_t ttl, ssz_ob_t* result, uint32_t client_type, data_request_t** out_req);
static c4_status_t cl_validate_signed_beacon_block(prover_ctx_t* ctx, ssz_ob_t* signed_block, data_request_t* req);

static c4_status_t c4_send_beacon_json(prover_ctx_t* ctx, char* path, char* query, uint32_t ttl, json_t* result, data_request_t** req) {
  return c4_send_beacon_json_with_client_type(ctx, path, query, ttl, result, 0, req);
}

static c4_status_t c4_send_beacon_json_no_throw(prover_ctx_t* ctx, char* path, char* query, uint32_t ttl, data_request_t** out_req) {
#ifdef HTTP_SERVER
  uint32_t client_type = ctx->client_type;
#else
  uint32_t client_type = 0;
#endif
  return send_request_impl(ctx, path, query, ttl,
                           C4_DATA_TYPE_BEACON_API, C4_DATA_ENCODING_JSON,
                           client_type, CU_BEACON_JSON, out_req);
}

static c4_status_t c4_send_beacon_json_with_client_type(prover_ctx_t* ctx, char* path, char* query, uint32_t ttl, json_t* result, uint32_t client_type, data_request_t** out_req) {
#ifdef HTTP_SERVER
  client_type |= ctx->client_type;
#endif
  data_request_t* req    = NULL;
  c4_status_t     status = send_request_impl(ctx, path, query, ttl,
                                             C4_DATA_TYPE_BEACON_API, C4_DATA_ENCODING_JSON,
                                             client_type, CU_BEACON_JSON, &req);
  if (out_req) *out_req = req;
  if (status == C4_ERROR) THROW_ERROR(req && req->error ? req->error : "Data request failed");
  if (status == C4_SUCCESS) {
    json_t response = json_parse((char*) req->response.data);
    if (response.type == JSON_TYPE_INVALID) THROW_ERROR("Invalid JSON response");
    *result = response;
  }
  return status;
}

c4_status_t c4_send_beacon_ssz(prover_ctx_t* ctx, char* path, char* query, const ssz_def_t* def, uint32_t ttl, ssz_ob_t* result, data_request_t** req) {
  return c4_send_beacon_ssz_with_client_type(ctx, path, query, def, ttl, result, 0, req);
}

c4_status_t c4_send_beacon_ssz_no_throw(prover_ctx_t* ctx, char* path, char* query, uint32_t ttl, data_request_t** out_req) {
#ifdef HTTP_SERVER
  uint32_t client_type = ctx->client_type;
#else
  uint32_t client_type = 0;
#endif
  return send_request_impl(ctx, path, query, ttl,
                           C4_DATA_TYPE_BEACON_API, C4_DATA_ENCODING_SSZ,
                           client_type, CU_BEACON_SSZ, out_req);
}

static c4_status_t c4_send_beacon_ssz_with_client_type(prover_ctx_t* ctx, char* path, char* query, const ssz_def_t* def, uint32_t ttl, ssz_ob_t* result, uint32_t client_type, data_request_t** out_req) {
#ifdef HTTP_SERVER
  client_type |= ctx->client_type;
#endif
  data_request_t* req    = NULL;
  c4_status_t     status = send_request_impl(ctx, path, query, ttl,
                                             C4_DATA_TYPE_BEACON_API, C4_DATA_ENCODING_SSZ,
                                             client_type, CU_BEACON_SSZ, &req);
  if (out_req) *out_req = req;
  if (status == C4_ERROR) THROW_ERROR(req && req->error ? req->error : "Data request failed");
  if (status == C4_SUCCESS) {
    *result = (ssz_ob_t) {.def = def, .bytes = req->response};
    if (!req->validated) {
      if (result->bytes.len > 20 && result->bytes.data[0] == '{' && result->bytes.data[1] == '"' && !convert_to_ssz(ctx, req, result)) return C4_ERROR;
      if (def) {
        if (!ssz_is_valid(*result, true, &ctx->state)) return C4_ERROR;
        req->validated = true;
      }
    }
  }
  return status;
}

c4_status_t c4_send_internal_request_no_throw(prover_ctx_t* ctx, char* path, char* query, uint32_t ttl, data_request_t** out_req) {
  return send_request_impl(ctx, path, query, ttl,
                           C4_DATA_TYPE_INTERN, C4_DATA_ENCODING_SSZ,
                           0, CU_INTERNAL_REQUEST, out_req);
}

c4_status_t c4_send_internal_request(prover_ctx_t* ctx, char* path, char* query, uint32_t ttl, bytes_t* result) {
  data_request_t* req    = NULL;
  c4_status_t     status = c4_send_internal_request_no_throw(ctx, path, query, ttl, &req);
  if (status == C4_ERROR) THROW_ERROR(req && req->error ? req->error : "Data request failed");
  if (status == C4_SUCCESS) *result = req->response;
  return status;
}

c4_status_t cl_get_finality_checkpoints(prover_ctx_t* ctx, json_t* result) {
  data_request_t* req = NULL;
  TRY_ASYNC(c4_send_beacon_json(ctx, "eth/v1/beacon/states/head/finality_checkpoints", NULL, 0, result, &req));
  if (req && !req->validated) {
    CHECK_JSON(*result, JSON_BEACON_FINALITY, "Invalid finality checkpoints: ");
    req->validated = true;
  }
  *result = json_get(*result, "data");
  return C4_SUCCESS;
}

c4_status_t cl_get_headers_at_slot(prover_ctx_t* ctx, uint64_t slot, json_t* result) {
  char     path[200]   = {0};
  buffer_t path_buffer = stack_buffer(path);

  bprintf(&path_buffer, "eth/v1/beacon/headers?slot=%l", slot);
  data_request_t* req    = NULL;
  c4_status_t     status = c4_send_beacon_json_no_throw(ctx, path, NULL, CL_HEADER_QUERY_TTL, &req);
  if (status == C4_PENDING) return C4_PENDING;
  if (status == C4_SUCCESS) {
    json_t response = json_parse((char*) req->response.data);
    if (response.type == JSON_TYPE_INVALID) THROW_ERROR("Invalid JSON response");
    if (!req->validated) {
      CHECK_JSON(response, JSON_BEACON_HEADER_LIST, "Invalid beacon headers: ");
      req->validated = true;
    }
    *result = response;
    return C4_SUCCESS;
  }
  if (beacon_slot_missing_error(req ? req->error : NULL)) {
    *result = (json_t) {.type = JSON_TYPE_NOT_FOUND};
    return C4_SUCCESS;
  }
  THROW_ERROR(req && req->error ? req->error : "Data request failed");
}

c4_status_t cl_get_header_by_root(prover_ctx_t* ctx, bytes32_t root, json_t* result) {
  char     path[200]   = {0};
  buffer_t path_buffer = stack_buffer(path);

  bprintf(&path_buffer, "eth/v1/beacon/headers/0x%x", bytes(root, 32));
  data_request_t* req = NULL;
  TRY_ASYNC(c4_send_beacon_json(ctx, path, NULL, DEFAULT_TTL, result, &req));
  if (req && !req->validated) {
    CHECK_JSON(*result, JSON_BEACON_HEADER_OBJECT, "Invalid beacon header: ");
    req->validated = true;
  }
  return C4_SUCCESS;
}

c4_status_t cl_get_headers_by_parent_root(prover_ctx_t* ctx, bytes32_t parent_root, json_t* result) {
  char     path[200]   = {0};
  buffer_t path_buffer = stack_buffer(path);

  bprintf(&path_buffer, "eth/v1/beacon/headers?parent_root=0x%x", bytes(parent_root, 32));
  data_request_t* req = NULL;
  TRY_ASYNC(c4_send_beacon_json_with_client_type(ctx, path, NULL, CL_HEADER_QUERY_TTL, result, BEACON_SUPPORTS_PARENT_ROOT_HEADERS, &req));
  if (req && !req->validated) {
    CHECK_JSON(*result, JSON_BEACON_HEADER_LIST, "Invalid beacon headers: ");
    req->validated = true;
  }
  return C4_SUCCESS;
}

c4_status_t cl_get_header_message_by_root(prover_ctx_t* ctx, bytes32_t root, json_t* message) {
  json_t result = {0};
  TRY_ASYNC(cl_get_header_by_root(ctx, root, &result));
  json_t val = json_get(result, "data");
  if (val.type != JSON_TYPE_OBJECT) THROW_ERROR("Invalid header!");
  val      = json_get(val, "header");
  *message = json_get(val, "message");
  if (!message->start) THROW_ERROR("Invalid header!");
  return C4_SUCCESS;
}

c4_status_t cl_get_historical_summaries(prover_ctx_t* ctx, bytes_t state_root, json_t* history_proof) {
  if (ctx->state.error) return C4_ERROR;
  uint8_t         tmp[200] = {0};
  buffer_t        buf      = stack_buffer(tmp);
  bool            nimbus   = (ctx->flags & C4_PROVER_FLAG_NIMBUS) != 0;
  const char*     path     = nimbus ? "nimbus/v1/debug/beacon/states/0x%b/historical_summaries"
                                    : "eth/v1/lodestar/states/0x%b/historical_summaries";
  uint32_t        client   = nimbus ? BEACON_CLIENT_NIMBUS : BEACON_CLIENT_LODESTAR;
  data_request_t* req      = NULL;
  c4_status_t     status   = c4_send_beacon_json_with_client_type(ctx, bprintf(&buf, path, state_root), NULL, 120, history_proof, client, &req);
  if (status == C4_SUCCESS && req && !req->validated) {
    CHECK_JSON(*history_proof, JSON_BEACON_HISTORICAL_SUMMARIES, "Invalid historical summaries: ");
    req->validated = true;
  }
  return status;
}

static c4_status_t cl_validate_signed_beacon_block(prover_ctx_t* ctx, ssz_ob_t* signed_block, data_request_t* req) {
  if (!signed_block || !signed_block->bytes.data) THROW_ERROR("no block data!");
  if (signed_block->bytes.len < 108) THROW_ERROR_WITH("Invalid block data len=%d !", signed_block->bytes.len);
  bytes_t  data   = signed_block->bytes;
  uint32_t offset = uint32_from_le(data.data);
  if (offset > data.len - 8) THROW_ERROR_WITH("Invalid block data offset[%d] > data_len[%d] - 8 : %b !", offset, data.len, bytes(data.data, data.len < 200 ? data.len : 200));
  uint64_t            slot  = uint64_from_le(data.data + offset);
  const chain_spec_t* chain = c4_eth_get_chain_spec(ctx->chain_id);
  if (chain == NULL) THROW_ERROR("unsupported chain id!");
  fork_id_t fork    = c4_chain_fork_id(ctx->chain_id, epoch_for_slot(slot, chain));
  signed_block->def = eth_ssz_type_for_fork(ETH_SSZ_SIGNED_BEACON_BLOCK_CONTAINER, fork, ctx->chain_id);
  if (!signed_block->def) THROW_ERROR("Invalid fork id!");
  if (req && req->validated) return C4_SUCCESS;
  if (!ssz_is_valid(*signed_block, true, &ctx->state)) return C4_ERROR;
  if (req) req->validated = true;
  return C4_SUCCESS;
}

c4_status_t cl_fetch_signed_beacon_block(prover_ctx_t* ctx, const uint8_t* root, uint64_t slot, ssz_ob_t* signed_block) {
  if (!signed_block) THROW_ERROR("Invalid block data!");
  char     path[200];
  buffer_t buffer   = stack_buffer(path);
  bool     has_hash = root && !bytes_all_zero(bytes((uint8_t*) root, 32));
  uint32_t ttl      = 6;
  if (!has_hash && slot == 0)
    buffer_add_chars(&buffer, "eth/v2/beacon/blocks/head");
  else if (has_hash) {
    bprintf(&buffer, "eth/v2/beacon/blocks/0x%x", bytes((uint8_t*) root, 32));
    ttl = DEFAULT_TTL;
  }
  else
    bprintf(&buffer, "eth/v2/beacon/blocks/%l", slot);

  data_request_t* req = NULL;
  TRY_ASYNC(c4_send_beacon_ssz(ctx, path, NULL, NULL, ttl, signed_block, &req));
  return cl_validate_signed_beacon_block(ctx, signed_block, req);
}

// Lodestar `/eth/v0/beacon/proof/state/{state_id}` response. Kept local so it
// does not leak into fork-agnostic beacon type dispatchers. Limits match
// Lodestar `CompactMultiProofType` (packages/api/src/beacon/routes/proof.ts).
static const ssz_def_t COMPACT_MULTI_PROOF_FIELDS[] = {
    SSZ_LIST("leaves", ssz_bytes32, 10000),
    SSZ_BYTES("descriptor", 2048)};
static const ssz_def_t COMPACT_MULTI_PROOF_CONTAINER =
    SSZ_CONTAINER("CompactMultiProof", COMPACT_MULTI_PROOF_FIELDS);

#define CL_STATE_PROOF_MAX_DESCRIPTOR 2048u

c4_status_t cl_get_state_proof(prover_ctx_t* ctx,
                               bytes32_t     state_root,
                               bytes_t       descriptor,
                               ssz_ob_t*     leaves_out,
                               ssz_ob_t*     descriptor_out) {
  if (!ctx || !leaves_out || !descriptor_out) return C4_ERROR;
  if (descriptor.len == 0 || descriptor.data == NULL)
    THROW_ERROR("cl_get_state_proof: empty descriptor");
  if (descriptor.len > CL_STATE_PROOF_MAX_DESCRIPTOR)
    THROW_ERROR("cl_get_state_proof: descriptor exceeds max size");

  char     path[128] = {0};
  buffer_t query     = {0};
  sbprintf(path, "eth/v0/beacon/proof/state/0x%x", bytes(state_root, 32));
  bprintf(&query, "format=0x%x", descriptor);

  ssz_ob_t    response = {0};
  c4_status_t status   = c4_send_beacon_ssz_with_client_type(
      ctx, path, (char*) query.data.data,
      &COMPACT_MULTI_PROOF_CONTAINER, DEFAULT_TTL, &response,
      BEACON_CLIENT_LODESTAR, NULL);
  buffer_free(&query);
  if (status != C4_SUCCESS) return status;

  ssz_ob_t leaves_ob     = ssz_get(&response, "leaves");
  ssz_ob_t descriptor_ob = ssz_get(&response, "descriptor");

  if (descriptor_ob.bytes.len != descriptor.len ||
      memcmp(descriptor_ob.bytes.data, descriptor.data, descriptor.len) != 0)
    THROW_ERROR("cl_get_state_proof: response descriptor does not match request");

  *leaves_out     = leaves_ob;
  *descriptor_out = descriptor_ob;
  return C4_SUCCESS;
}
