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
#include "eth_compute_units.h"
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
  beacon_block_t    block          = {0};
  ssz_builder_t     block_proof   = ssz_builder_for_type(ETH_SSZ_VERIFY_BLOCK_PROOF);
  blockroot_proof_t historic_proof = {0};
  ssz_builder_t     sync_proof     = NULL_SSZ_BUILDER;
  json_t            block_arg      = json_len(ctx->params) > 0 ? json_at(ctx->params, 0) : json_parse("\"latest\"");

  // fetch the block (default to "latest" if no params given, e.g. for eth_blobBaseFee)
  TRY_ASYNC(c4_beacon_get_block_for_eth(ctx, block_arg, &block));
  TRY_ASYNC(c4_check_blockroot_proof(ctx, &historic_proof, &block));
  TRY_ASYNC(c4_get_syncdata_proof(ctx, &historic_proof.sync, &sync_proof));

  if (block.execution.def == NULL) THROW_ERROR("execution payload is null");

  ssz_builder_t content_proof = ssz_builder_for_def( ssz_get_def( ssz_get_def(block_proof.def, "body"),"content"));
  ssz_add_ob(&content_proof, "transactions", ssz_get(&block.execution,"transaction"));
  ssz_add_ob(&content_proof, "withdrawals", ssz_get(&block.execution,"withdrawals"));
  ssz_add_builders(&block_proof, "body", content_proof);
  eth_add_block_proof(ctx, &block_proof, &block, &historic_proof);

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
  ssz_builder_t     header_proof   = ssz_builder_for_type(ETH_SSZ_VERIFY_BLOCK_HEADER_PROOF);
  blockroot_proof_t historic_proof = {0};
  ssz_builder_t     sync_proof     = NULL_SSZ_BUILDER;
  json_t            block_arg      = json_len(ctx->params) > 0 ? json_at(ctx->params, 0) : json_parse("\"latest\"");

  // fetch the block (default to "latest" if no params given, e.g. for eth_blobBaseFee)
  TRY_ASYNC(c4_beacon_get_block_for_eth(ctx, block_arg, &block));
  TRY_ASYNC(c4_check_blockroot_proof(ctx, &historic_proof, &block));
  TRY_ASYNC(c4_get_syncdata_proof(ctx, &historic_proof.sync, &sync_proof));

  // build the proof
  eth_add_block_proof(ctx, &header_proof, &block, &historic_proof);

  ctx->proof = eth_create_proof_request(
      ctx->chain_id,
      NULL_SSZ_BUILDER,
      header_proof,
      sync_proof);

  c4_free_block_proof(&historic_proof);
  return C4_SUCCESS;
}
