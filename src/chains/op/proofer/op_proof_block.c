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
#include "op_types.h"
#include "proofer.h"
#include "ssz.h"
#include "version.h"
#include <inttypes.h> // Include this header for PRIu64 and PRIx64
#include <stdlib.h>
#include <string.h>

c4_status_t c4_op_proof_block(proofer_ctx_t* ctx) {
  // first try to fetch the block from the preconfs
  uint8_t  path[200]    = {0};
  bytes_t  preconf_data = {0};
  buffer_t buf2         = stack_buffer(path);
  json_t   block_number = json_at(ctx->params, 0);

  TRY_ASYNC(c4_send_internal_request(ctx, bprintf(&buf2, "preconfs/%j", block_number), NULL, 0, &preconf_data)); // get the raw-data

  if (!preconf_data.len)
    THROW_ERROR("No preconf data found, currently only supports preconfs");

  // build the proof
  ssz_builder_t block_proof   = ssz_builder_for_op_type(OP_SSZ_VERIFY_BLOCK_PROOF);
  ssz_builder_t preconf_proof = ssz_builder_for_op_type(OP_SSZ_VERIFY_PRECONF_PROOF);
  ssz_add_bytes(&preconf_proof, "payload", bytes_slice(preconf_data, 0, preconf_data.len - 65));
  ssz_add_bytes(&preconf_proof, "signature", bytes_slice(preconf_data, preconf_data.len - 65, 65));
  ssz_add_builders(&block_proof, "block_proof", preconf_proof);

  ctx->proof = eth_create_proof_request(
      ctx->chain_id,
      NULL_SSZ_BUILDER,
      block_proof,
      NULL_SSZ_BUILDER);

  return C4_SUCCESS;
}
