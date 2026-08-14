/*
 * Copyright (c) 2025 corpus.core
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

#include "beacon_types.h"
#include "eth_compute_units.h"
#include "eth_tools.h"
#include "eth_verify.h"
#include "prover.h"
#include "ssz.h"
#include "sync_committee.h"
#include <stdlib.h>

// The `next_sync_committee` gindex is fork-dependent; the resolver lives in
// `beacon_types.c` (`c4_next_sync_committee_gindex`) so all fork-aware selections
// share one source of truth.

static c4_status_t req_client_update(prover_ctx_t* ctx, uint32_t period, uint32_t count, chain_id_t chain_id, bytes_t* data) {
  // This helper enqueues a Beacon-API SSZ request directly on `ctx->state`
  // instead of going through `c4_send_beacon_ssz`, so we have to add the CU
  // ourselves to keep accounting consistent with the rest of the code.
  eth_cu_add(ctx, CU_BEACON_SSZ);
  buffer_t tmp = {0};
  bprintf(&tmp, "eth/v1/beacon/light_client/updates?start_period=%d&count=%d", period, count);

  data_request_t* req = c4_state_get_data_request_by_url(&ctx->state, (char*) tmp.data.data);
  if (req) buffer_free(&tmp);
  if (req && req->response.data) {
    *data = req->response;
    return C4_SUCCESS;
  }
  else if (req && req->error) {
    ctx->state.error = strdup(req->error);
    return C4_ERROR;
  }
  data_request_t* new_req = safe_calloc(1, sizeof(data_request_t));
  new_req->chain_id       = chain_id;
  new_req->url            = (char*) tmp.data.data;
  new_req->encoding       = C4_DATA_ENCODING_SSZ;
  new_req->type           = C4_DATA_TYPE_BEACON_API;
  c4_state_add_request(&ctx->state, new_req);
  return C4_PENDING;
}

typedef struct {

  ssz_ob_t new_pubkeys;
  ssz_ob_t old_pubkeys;
  ssz_ob_t signature_bits;
  ssz_ob_t signature;
  gindex_t gidx;
  bytes_t  proof;
  bytes_t  slot;
  bytes_t  proposer_index;
} period_data_t;

static ssz_ob_t unwrap_lcu_response(prover_ctx_t* ctx, bytes_t data) {
  ssz_ob_t result = {.bytes = NULL_BYTES, .def = NULL};
  if (data.len < 12) return result;
  uint64_t payload_len = uint64_from_le(data.data);
  if (payload_len < 4 || 8 + payload_len > data.len) return result;
  result.bytes         = bytes(data.data + 12, payload_len - 4);
  fork_id_t        fork = c4_eth_get_fork_for_lcu(ctx->chain_id, result.bytes);
  const ssz_def_t* def  = eth_get_light_client_update(fork);
  result.def = def;
  return result;
}

static c4_status_t extract_sync_data(prover_ctx_t* ctx, bytes_t old_data, bytes_t new_data, period_data_t* period) {
  bytes32_t domain    = {0};
  bytes32_t aggregate = {0};

  ssz_ob_t old_update = unwrap_lcu_response(ctx, old_data);
  if (!old_update.def) THROW_ERROR("invalid old client_update");
  ssz_ob_t new_update = unwrap_lcu_response(ctx, new_data);
  if (!new_update.def) THROW_ERROR("invalid new client_update");

  fork_id_t fork = c4_eth_get_fork_for_lcu(ctx->chain_id, new_update.bytes);

  ssz_ob_t old_sync_keys  = ssz_get(&old_update, "nextSyncCommittee");
  ssz_ob_t new_sync_keys  = ssz_get(&new_update, "nextSyncCommittee");
  ssz_ob_t sync_aggregate = ssz_get(&new_update, "syncAggregate");
  ssz_ob_t light_header   = ssz_get(&new_update, "attestedHeader");
  ssz_ob_t header         = ssz_get(&light_header, "beacon");
  period->old_pubkeys     = ssz_get(&old_sync_keys, "pubkeys");
  period->new_pubkeys     = ssz_get(&new_sync_keys, "pubkeys");
  period->signature_bits  = ssz_get(&sync_aggregate, "syncCommitteeBits");
  period->signature       = ssz_get(&sync_aggregate, "syncCommitteeSignature");
  period->slot            = ssz_get(&header, "slot").bytes;
  period->proposer_index  = ssz_get(&header, "proposerIndex").bytes;
  bytes_t  state_proof    = ssz_get(&new_update, "nextSyncCommitteeBranch").bytes;
  ssz_ob_t aggrgated_pub  = ssz_get(&new_sync_keys, "aggregatePubkey");

  if (!eth_calculate_domain(ctx->chain_id, ssz_get_uint64(&header, "slot"), domain)) THROW_ERROR("unsupported chain!");
  //
  memcpy(aggregate, aggrgated_pub.bytes.data + 32, 16);
  sha256_merkle(bytes_slice(aggrgated_pub.bytes, 0, 32), bytes(aggregate, 32), aggregate);

  // define  ssz
  ssz_def_t SIGNING_DATA[] = {
      SSZ_BYTES32("BeaconBlockHeader"),
      SSZ_BYTES32("domain")}; // the domain of the data to sign
  ssz_def_t SIGNING_DATA_CONTAINER   = SSZ_CONTAINER("SigningData", SIGNING_DATA);
  SIGNING_DATA[0]                    = *eth_ssz_type_for_fork(ETH_SSZ_BEACON_BLOCK_HEADER, fork, ctx->chain_id);
  ssz_builder_t signgin_data_builder = ssz_builder_for_def(&SIGNING_DATA_CONTAINER);
  ssz_add_bytes(&signgin_data_builder, "BeaconBlockHeader", header.bytes);
  ssz_add_bytes(&signgin_data_builder, "domain", bytes(domain, 32));
  ssz_ob_t signing_data = ssz_builder_to_bytes(&signgin_data_builder);
  gindex_t state_gidx = ssz_gindex(signing_data.def, 2, "BeaconBlockHeader", "stateRoot");
  eth_cu_add_proof(ctx);
  bytes_t header_proof = ssz_create_proof(signing_data, domain, state_gidx);
  bytes_t  full_proof   = bytes(malloc(header_proof.len + state_proof.len + 32), header_proof.len + state_proof.len + 32);
  memcpy(full_proof.data, aggregate, 32);                                              // 1
  memcpy(full_proof.data + 32, state_proof.data, state_proof.len);                     // 5 for deneb
  memcpy(full_proof.data + 32 + state_proof.len, header_proof.data, header_proof.len); // 4
  safe_free(header_proof.data);
  safe_free(signing_data.bytes.data);
  period->proof = full_proof;
  period->gidx  = ssz_add_gindex(state_gidx, c4_next_sync_committee_gindex(ctx->chain_id, ssz_get_uint64(&header, "slot"))) * 2; // header -> stateRoot -> .... next_sync ->  pubKeys

  return C4_SUCCESS;
}

static c4_status_t create_proof(prover_ctx_t* ctx, period_data_t* period) {
  ssz_builder_t proof = ssz_builder_for_type(ETH_SSZ_VERIFY_SYNC_PROOF);
  ssz_add_bytes(&proof, "oldKeys", period->old_pubkeys.bytes);
  ssz_add_bytes(&proof, "newKeys", period->new_pubkeys.bytes);
  ssz_add_bytes(&proof, "syncCommitteeBits", period->signature_bits.bytes);
  ssz_add_bytes(&proof, "syncCommitteeSignature", period->signature.bytes);
  ssz_add_uint64(&proof, period->gidx);
  ssz_add_bytes(&proof, "slot", period->slot);
  ssz_add_bytes(&proof, "proposerIndex", period->proposer_index);
  ssz_add_bytes(&proof, "proof", period->proof);
  safe_free(period->proof.data);

  ctx->proof = eth_create_proof_request(
      ctx->chain_id,
      NULL_SSZ_BUILDER,
      proof,
      NULL_SSZ_BUILDER);
  return C4_SUCCESS;
}

c4_status_t c4_proof_sync(prover_ctx_t* ctx) {
  bytes_t       old_data      = NULL_BYTES;
  bytes_t       new_data      = NULL_BYTES;
  period_data_t period_values = {0};
  json_t        period_data   = json_at(ctx->params, 0);
  uint32_t      period        = json_as_uint32(period_data);
  c4_status_t   status        = C4_SUCCESS;

  if (period == 0) THROW_ERROR_WITH("Invalid period: %j", period_data);
  TRY_ADD_ASYNC(status, req_client_update(ctx, period - 2, 1, ctx->chain_id, &old_data));
  TRY_ADD_ASYNC(status, req_client_update(ctx, period - 1, 1, ctx->chain_id, &new_data));
  TRY_ASYNC(status);  
  TRY_ASYNC(extract_sync_data(ctx, old_data, new_data, &period_values));
  return create_proof(ctx, &period_values);
}
