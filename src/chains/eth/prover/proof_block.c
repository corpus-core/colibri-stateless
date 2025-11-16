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
  bytes_t execution_payload_proof = ssz_create_proof(block.body, body_root, ssz_gindex(block.body.def, 1, "executionPayload"));

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
  bytes_t execution_payload_proof = ssz_create_multi_proof(block.body, body_root, 2,
                                                           ssz_gindex(block.body.def, 2, "executionPayload", "blockNumber"),
                                                           ssz_gindex(block.body.def, 2, "executionPayload", "timestamp"));

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
