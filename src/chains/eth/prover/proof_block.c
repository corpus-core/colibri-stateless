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

#include "beacon.h"
#include "beacon_types.h"
#include "eth_req.h"
#include "eth_tools.h"
#include "historic_proof.h"
#include "json.h"
#include "prover.h"
#include "ssz.h"
#include "version.h"
#include <inttypes.h> // Include this header for PRIu64 and PRIx64
#include <stdlib.h>
#include <string.h>

c4_status_t c4_proof_block(prover_ctx_t* ctx) {
  uint8_t           empty_selector = 0;
  beacon_block_t    block          = {0};
  bytes32_t         body_root      = {0};
  ssz_builder_t     block_proof    = ssz_builder_for_type(ETH_SSZ_VERIFY_BLOCK_PROOF);
  blockroot_proof_t historic_proof = {0};
  ssz_builder_t     sync_proof     = NULL_SSZ_BUILDER;

  // fetch the block
  TRY_ASYNC(c4_beacon_get_block_for_eth(ctx, json_at(ctx->params, 0), &block));
  TRY_ASYNC(c4_check_blockroot_proof(ctx, &historic_proof, &block));
  TRY_ASYNC(c4_get_syncdata_proof(ctx, &historic_proof.sync, &sync_proof));

  // create merkle proof
  gindex_t ep_gindex              = ssz_gindex(block.body.def, 1, "executionPayload");
  bytes_t  execution_payload_proof = NULL_BYTES;
#ifdef PROVER_CACHE
  if (block.merkle_cache.valid)
    execution_payload_proof = ssz_create_multi_proof_from_body_cache(&block.merkle_cache, body_root, &ep_gindex, 1);
  if (!execution_payload_proof.data)
#endif
    execution_payload_proof = ssz_create_proof(block.body, body_root, ep_gindex);

  // build the proof
  ssz_add_builders(&block_proof, "executionPayload", (ssz_builder_t) {.def = block.execution.def, .fixed = {.data = bytes_dup(block.execution.bytes)}});
  ssz_add_bytes(&block_proof, "proof", execution_payload_proof);
  safe_free(execution_payload_proof.data);
  ssz_add_builders(&block_proof, "header", c4_proof_add_header(block.header, body_root));
  ssz_add_header_proof(&block_proof, &block, historic_proof);

  ctx->proof = eth_create_proof_request(
      ctx->chain_id,
      NULL_SSZ_BUILDER,
      block_proof,
      sync_proof);

  c4_free_block_proof(&historic_proof);

  return C4_SUCCESS;
}

c4_status_t c4_proof_block_number(prover_ctx_t* ctx) {
  uint8_t           empty_selector = 0;
  beacon_block_t    block          = {0};
  bytes32_t         body_root      = {0};
  ssz_builder_t     block_proof    = ssz_builder_for_type(ETH_SSZ_VERIFY_BLOCK_NUMBER_PROOF);
  blockroot_proof_t historic_proof = {0};
  ssz_builder_t     sync_proof     = NULL_SSZ_BUILDER;

  // fetch the block
  TRY_ASYNC(c4_beacon_get_block_for_eth(ctx, json_parse("\"latest\""), &block));
  TRY_ASYNC(c4_check_blockroot_proof(ctx, &historic_proof, &block));
  TRY_ASYNC(c4_get_syncdata_proof(ctx, &historic_proof.sync, &sync_proof));

  // create merkle proof
  gindex_t bn_gi[2] = {ssz_gindex(block.body.def, 2, "executionPayload", "blockNumber"),
                        ssz_gindex(block.body.def, 2, "executionPayload", "timestamp")};
  bytes_t  execution_payload_proof = NULL_BYTES;
#ifdef PROVER_CACHE
  if (block.merkle_cache.valid)
    execution_payload_proof = ssz_create_multi_proof_from_body_cache(&block.merkle_cache, body_root, bn_gi, 2);
  if (!execution_payload_proof.data)
#endif
    execution_payload_proof = ssz_create_multi_proof(block.body, body_root, 2, bn_gi[0], bn_gi[1]);

  // build the proof
  ssz_add_bytes(&block_proof, "blockNumber", ssz_get(&block.execution, "blockNumber").bytes);
  ssz_add_bytes(&block_proof, "timestamp", ssz_get(&block.execution, "timestamp").bytes);
  ssz_add_bytes(&block_proof, "proof", execution_payload_proof);
  ssz_add_builders(&block_proof, "header", c4_proof_add_header(block.header, body_root));
  ssz_add_header_proof(&block_proof, &block, historic_proof);
  safe_free(execution_payload_proof.data);

  ctx->proof = eth_create_proof_request(
      ctx->chain_id,
      NULL_SSZ_BUILDER,
      block_proof,
      sync_proof);

  c4_free_block_proof(&historic_proof);

  return C4_SUCCESS;
}

c4_status_t c4_proof_block_header(prover_ctx_t* ctx) {
  beacon_block_t    block          = {0};
  bytes32_t         body_root      = {0};
  ssz_builder_t     header_proof   = ssz_builder_for_type(ETH_SSZ_VERIFY_BLOCK_HEADER_PROOF);
  ssz_builder_t     data           = ssz_builder_for_type(ETH_SSZ_DATA_BLOCK_HEADER);
  blockroot_proof_t historic_proof = {0};
  ssz_builder_t     sync_proof     = NULL_SSZ_BUILDER;
  json_t            block_arg      = json_len(ctx->params) > 0 ? json_at(ctx->params, 0) : json_parse("\"latest\"");

  // fetch the block (default to "latest" if no params given, e.g. for eth_blobBaseFee)
  TRY_ASYNC(c4_beacon_get_block_for_eth(ctx, block_arg, &block));
  TRY_ASYNC(c4_check_blockroot_proof(ctx, &historic_proof, &block));
  TRY_ASYNC(c4_get_syncdata_proof(ctx, &historic_proof.sync, &sync_proof));

  // create multi-merkle proof for 12 selected execution payload fields
  const gindex_t* gi                      = c4_block_header_gindexes(ctx->chain_id, ssz_get_uint64(&block.header, "slot"));
  bytes_t         execution_payload_proof = NULL_BYTES;
#ifdef PROVER_CACHE
  if (block.merkle_cache.valid)
    execution_payload_proof = ssz_create_multi_proof_from_body_cache(&block.merkle_cache, body_root, gi, BLOCK_HEADER_FIELD_COUNT);
  if (!execution_payload_proof.data)
#endif
    execution_payload_proof = ssz_create_multi_proof(block.body, body_root, BLOCK_HEADER_FIELD_COUNT,
                                                     gi[0], gi[1], gi[2], gi[3], gi[4], gi[5],
                                                     gi[6], gi[7], gi[8], gi[9], gi[10], gi[11]);

  // build the data
  ssz_add_bytes(&data, "parentHash", ssz_get(&block.execution, "parentHash").bytes);
  ssz_add_bytes(&data, "stateRoot", ssz_get(&block.execution, "stateRoot").bytes);
  ssz_add_bytes(&data, "receiptsRoot", ssz_get(&block.execution, "receiptsRoot").bytes);
  ssz_add_bytes(&data, "logsBloom", ssz_get(&block.execution, "logsBloom").bytes);
  ssz_add_bytes(&data, "blockNumber", ssz_get(&block.execution, "blockNumber").bytes);
  ssz_add_bytes(&data, "gasLimit", ssz_get(&block.execution, "gasLimit").bytes);
  ssz_add_bytes(&data, "gasUsed", ssz_get(&block.execution, "gasUsed").bytes);
  ssz_add_bytes(&data, "timestamp", ssz_get(&block.execution, "timestamp").bytes);
  ssz_add_bytes(&data, "baseFeePerGas", ssz_get(&block.execution, "baseFeePerGas").bytes);
  ssz_add_bytes(&data, "blockHash", ssz_get(&block.execution, "blockHash").bytes);
  ssz_add_bytes(&data, "blobGasUsed", ssz_get(&block.execution, "blobGasUsed").bytes);
  ssz_add_bytes(&data, "excessBlobGas", ssz_get(&block.execution, "excessBlobGas").bytes);

  // build the proof
  ssz_add_bytes(&header_proof, "proof", execution_payload_proof);
  ssz_add_builders(&header_proof, "header", c4_proof_add_header(block.header, body_root));
  ssz_add_header_proof(&header_proof, &block, historic_proof);
  safe_free(execution_payload_proof.data);

  ctx->proof = eth_create_proof_request(
      ctx->chain_id,
      data,
      header_proof,
      sync_proof);

  c4_free_block_proof(&historic_proof);

  return C4_SUCCESS;
}
