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
#include "json.h"
#include "logger.h"
#include "op_tools.h"
#include "op_types.h"
#include "prover.h"
#include "ssz.h"
#include "version.h"
#include <inttypes.h> // Include this header for PRIu64 and PRIx64
#include <stdlib.h>
#include <string.h>

c4_status_t c4_op_create_block_proof(prover_ctx_t* ctx, json_t block_number, ssz_builder_t* block_proof) {
  uint8_t  path[200]    = {0};
  buffer_t buf2         = stack_buffer(path);
  bytes_t  preconf_data = {0};

  if ((ctx->flags & C4_PROOFER_FLAG_UNSTABLE_LATEST) == 0 && block_number.start[1] == 'l')
    bprintf(&buf2, "preconf/pre_latest");
  else
    bprintf(&buf2, "preconf/%j", block_number);

  TRY_ASYNC(c4_send_internal_request(ctx, (char*) buf2.data.data, NULL, 0, &preconf_data)); // get the raw-data
  if (!preconf_data.len) THROW_ERROR("No preconf data found, currently only supports preconfs");
  // Extract payload and signature
  bytes_t payload   = bytes_slice(preconf_data, 0, preconf_data.len - 65);
  bytes_t signature = bytes_slice(preconf_data, preconf_data.len - 65, 65);

  // build the proof
  ssz_builder_t preconf_proof = ssz_builder_for_op_type(OP_SSZ_VERIFY_PRECONF_PROOF);
  ssz_add_bytes(&preconf_proof, "payload", payload);
  ssz_add_bytes(&preconf_proof, "signature", signature);
  *block_proof = preconf_proof;

  return C4_SUCCESS;
}

c4_status_t c4_op_proof_block(prover_ctx_t* ctx) {
  // first try to fetch the block from the preconfs
  json_t        block_number  = json_at(ctx->params, 0);
  ssz_builder_t preconf_proof = {0};

  TRY_ASYNC(c4_op_create_block_proof(ctx, block_number, &preconf_proof));

  // build the proof
  ssz_builder_t block_proof = ssz_builder_for_op_type(OP_SSZ_VERIFY_BLOCK_PROOF);
  ssz_add_builders(&block_proof, "block_proof", preconf_proof);

  ctx->proof = op_create_proof_request(
      ctx->chain_id,
      NULL_SSZ_BUILDER,
      block_proof,
      NULL_SSZ_BUILDER);

  return C4_SUCCESS;
}
c4_status_t c4_op_proof_blocknumber(prover_ctx_t* ctx) {
  // first try to fetch the block from the preconfs
  ssz_builder_t preconf_proof = {0};

  TRY_ASYNC(c4_op_create_block_proof(ctx, (json_t) {.type = JSON_TYPE_STRING, .start = "\"latest\"", .len = 8}, &preconf_proof));

  // build the proof
  ssz_builder_t block_proof = ssz_builder_for_op_type(OP_SSZ_VERIFY_BLOCK_PROOF);
  ssz_add_builders(&block_proof, "block_proof", preconf_proof);

  ctx->proof = op_create_proof_request(
      ctx->chain_id,
      NULL_SSZ_BUILDER,
      block_proof,
      NULL_SSZ_BUILDER);

  return C4_SUCCESS;
}
