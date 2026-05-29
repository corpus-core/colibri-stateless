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

#include "eth_tools.h"
#include "beacon.h"
#include "beacon_types.h"
#include "bytes.h"
#include "eth_account.h"
#include "eth_compute_units.h"
#include "eth_tx.h"
#include "prover.h"
#include "version.h"

static void set_data(ssz_builder_t* req, const char* name, ssz_builder_t data) {
  if (data.def)
    ssz_add_builders(req, name, data);
  else
    ssz_add_bytes(req, name, bytes(NULL, 1));
}

bytes_t eth_create_proof_request(chain_id_t chain_id, ssz_builder_t data, ssz_builder_t proof, ssz_builder_t sync_data) {
  ssz_builder_t c4_req = ssz_builder_for_type(ETH_SSZ_VERIFY_REQUEST);

  // build the request
  ssz_add_bytes(&c4_req, "version", bytes(c4_protocol_version_bytes, 4));
  set_data(&c4_req, "data", data);
  set_data(&c4_req, "proof", proof);
  set_data(&c4_req, "sync_data", sync_data);

  // set chain_engine
  *c4_req.fixed.data.data = (uint8_t) c4_chain_type(chain_id);
  return ssz_builder_to_bytes(&c4_req).bytes;
}

#ifdef PROVER_CACHE
uint8_t* c4_eth_receipt_cachekey(bytes32_t target, bytes32_t blockhash) {
  if (target != blockhash) memcpy(target, blockhash, 32);
  target[0] = 'R';
  target[1] = 'T';
  return target;
}
#endif

// Union variant selectors for ETH_STATE_BLOCK_UNION (see verify_proof_types.h).
#define ETH_STATE_BLOCK_UNION_NONE         0
#define ETH_STATE_BLOCK_UNION_BLOCKHASH    1
#define ETH_STATE_BLOCK_UNION_BLOCKNUMBER  2
#define ETH_STATE_BLOCK_UNION_BLOCKCONTEXT 3
#define ETH_STATE_BLOCK_UNION_TIMESTAMP    4

static void ssz_add_block_proof(ssz_builder_t* builder, beacon_block_t* block_data, gindex_t block_index, bool use_block_context, bool use_timestamp) {
  if (use_block_context) {
    ssz_builder_t bc   = ssz_builder_for_type(ETH_SSZ_DATA_CALL_BLOCK_CONTEXT);
    ssz_ob_t      exec = block_data->execution;
    ssz_add_bytes(&bc, "blockNumber", ssz_get(&exec, "blockNumber").bytes);
    ssz_add_bytes(&bc, "timestamp", ssz_get(&exec, "timestamp").bytes);
    ssz_add_bytes(&bc, "coinbase", ssz_get(&exec, "feeRecipient").bytes);
    ssz_add_bytes(&bc, "prevRandao", ssz_get(&exec, "prevRandao").bytes);
    ssz_add_bytes(&bc, "baseFeePerGas", ssz_get(&exec, "baseFeePerGas").bytes);
    ssz_add_bytes(&bc, "blockHash", ssz_get(&exec, "blockHash").bytes);
    ssz_add_bytes(&bc, "gasLimit", ssz_get(&exec, "gasLimit").bytes);
    ssz_add_bytes(&bc, "excessBlobGas", ssz_get(&exec, "excessBlobGas").bytes);
    ssz_add_builders(builder, "block", bc);
    return;
  }

  if (use_timestamp) {
    // Timestamp-only variant for account `latest` freshness gate. The verifier
    // proves {stateRoot, timestamp} against the body root in one multi-proof,
    // so the block payload here only carries the timestamp leaf (8 bytes LE).
    uint8_t buffer[9] = {0};
    buffer[0]         = ETH_STATE_BLOCK_UNION_TIMESTAMP;
    memcpy(buffer + 1, ssz_get(&block_data->execution, "timestamp").bytes.data, 8);
    ssz_add_bytes(builder, "block", bytes(buffer, 9));
    return;
  }

  uint8_t  buffer[33] = {0};
  uint32_t l          = 1;
  if (block_index == GINDEX_BLOCHASH) {
    l         = 33;
    buffer[0] = ETH_STATE_BLOCK_UNION_BLOCKHASH;
    memcpy(buffer + 1, ssz_get(&block_data->execution, "blockHash").bytes.data, 32);
  }
  else if (block_index == GINDEX_BLOCKUMBER) {
    l         = 9;
    buffer[0] = ETH_STATE_BLOCK_UNION_BLOCKNUMBER;
    memcpy(buffer + 1, ssz_get(&block_data->execution, "blockNumber").bytes.data, 8);
  }
  ssz_add_bytes(builder, "block", bytes(buffer, l));
}

ssz_builder_t eth_ssz_create_state_proof(prover_ctx_t* ctx, json_t block_number, beacon_block_t* block, blockroot_proof_t* historic_proof, bool is_call) {
  bytes32_t     body_root         = {0};
  ssz_builder_t state_proof       = ssz_builder_for_type(ETH_SSZ_VERIFY_STATE_PROOF);
  bool          use_block_context = is_call && ctx->version >= c4_version_number(1, 1, 15); // block context is supported since version 1.1.15
  bytes_t       proof             = NULL_BYTES;
  gindex_t      block_index       = use_block_context ? 0 : eth_get_gindex_for_block(c4_chain_fork_id(ctx->chain_id, block->slot >> 5), block_number);
  // Timestamp variant for account proofs: the request used a non-pinned block tag
  // (`block_index == 0`, e.g. "latest"/"safe"/"finalized"), so add an 8-byte
  // timestamp leaf alongside the stateRoot so the verifier can run a freshness
  // gate on `latest`. Requires client version 1.1.27+ -- older clients still see
  // the legacy `none` variant.
  bool          use_timestamp     = !is_call && block_index == 0 && ctx->version >= c4_version_number(1, 1, 27);

  if (use_block_context) {
    const gindex_t* gi = c4_call_block_context_gindexes();
    eth_cu_add_multi_proof(ctx, CALL_BLOCK_CONTEXT_FIELD_COUNT);
#ifdef PROVER_CACHE
    if (block->merkle_cache.valid)
      proof = ssz_create_multi_proof_from_body_cache(&block->merkle_cache, body_root, gi, CALL_BLOCK_CONTEXT_FIELD_COUNT);
    if (!proof.data)
#endif
      proof = ssz_create_multi_proof(block->body, body_root, CALL_BLOCK_CONTEXT_FIELD_COUNT,
                                     gi[0], gi[1], gi[2], gi[3], gi[4], gi[5], gi[6], gi[7], gi[8]);
    ssz_add_block_proof(&state_proof, block, 0, true, false);
  }
  else if (use_timestamp) {
    gindex_t state_index = ssz_gindex(block->body.def, 2, "executionPayload", "stateRoot");
    gindex_t ts_index    = ssz_gindex(block->body.def, 2, "executionPayload", "timestamp");
    eth_cu_add_multi_proof(ctx, 2);
#ifdef PROVER_CACHE
    if (block->merkle_cache.valid) {
      gindex_t gi_arr[2] = {state_index, ts_index};
      proof              = ssz_create_multi_proof_from_body_cache(&block->merkle_cache, body_root, gi_arr, 2);
    }
    if (!proof.data)
#endif
      proof = ssz_create_multi_proof(block->body, body_root, 2, state_index, ts_index);
    ssz_add_block_proof(&state_proof, block, 0, false, true);
  }
  else {
    gindex_t state_index = ssz_gindex(block->body.def, 2, "executionPayload", "stateRoot");
    if (block_index == 0)
      eth_cu_add_proof(ctx);
    else
      eth_cu_add_multi_proof(ctx, 2);
#ifdef PROVER_CACHE
    if (block->merkle_cache.valid) {
      gindex_t gi_arr[2] = {state_index, block_index};
      int      gi_len    = block_index == 0 ? 1 : 2;
      proof               = ssz_create_multi_proof_from_body_cache(&block->merkle_cache, body_root, gi_arr, gi_len);
    }
    if (!proof.data)
#endif
      proof = block_index == 0
                  ? ssz_create_proof(block->body, body_root, state_index)
                  : ssz_create_multi_proof(block->body, body_root, 2, block_index, state_index);
    ssz_add_block_proof(&state_proof, block, block_index, false, false);
  }

  ssz_add_bytes(&state_proof, "proof", proof);
  ssz_add_builders(&state_proof, "header", c4_proof_add_header(block->header, body_root));
  ssz_add_header_proof(&state_proof, block, *historic_proof);

  safe_free(proof.data);
  return state_proof;
}
