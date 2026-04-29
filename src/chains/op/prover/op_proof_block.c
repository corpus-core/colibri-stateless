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
#include "op_prover.h"
#include "op_tools.h"
#include "op_types.h"
#include "prover.h"
#include "ssz.h"
#include "sync_committee.h"
#include "version.h"
#include <inttypes.h> // Include this header for PRIu64 and PRIx64
#include <stdlib.h>
#include <string.h>

/**
 * Decide whether the verifier already has the requested execution payload cached and the
 * preconf payload may therefore be omitted from the proof.
 *
 * The decision uses the `client_state` provided in the prover context. The state must be
 * `C4_STATE_SYNC_EXECUTION_PAYLOAD` and the cached (block_number, blockhash) tuple must
 * match the actually produced preconf payload.
 *
 * @param ctx prover context
 * @param preconf_proof builder containing the (compressed) execution payload of the upcoming block
 * @return true if the verifier can reuse its cache and the prover may emit `block_proof = none`
 */
static bool client_already_has_block(prover_ctx_t* ctx, ssz_builder_t* preconf_proof) {
  if (!ctx->client_state.data || !ctx->client_state.len) return false;
  c4_chain_state_t cs = c4_state_deserialize(ctx->client_state);
  if (cs.status != C4_STATE_SYNC_EXECUTION_PAYLOAD) return false;

  ssz_ob_t* ep = op_get_execution_payload(preconf_proof);
  if (!ep) return false;

  uint64_t bn    = ssz_get_uint64(ep, "blockNumber");
  bytes_t  bh    = ssz_get(ep, "blockHash").bytes;
  bool     match = bn == cs.data.block.block_number && bh.len == 32 &&
               memcmp(bh.data, cs.data.block.blockhash, 32) == 0;
  safe_free(ep);
  return match;
}

void c4_op_add_block_proof(prover_ctx_t* ctx, ssz_builder_t* parent, const char* name, ssz_builder_t* preconf_proof) {
  if (client_already_has_block(ctx, preconf_proof)) {
    log_debug("OP block already cached on client - emitting block_proof = NONE");
    ssz_builder_free(preconf_proof);
    ssz_add_ob(parent, name, (ssz_ob_t) {.def = &ssz_none, .bytes = NULL_BYTES});
  }
  else
    ssz_add_builders(parent, name, *preconf_proof);
}

c4_status_t c4_op_create_block_proof(prover_ctx_t* ctx, json_t block_number, ssz_builder_t* block_proof) {
  uint8_t  path[200]    = {0};
  buffer_t buf2         = stack_buffer(path);
  bytes_t  preconf_data = {0};

  if ((ctx->flags & C4_PROVER_FLAG_UNSTABLE_LATEST) == 0 && block_number.start[1] == 'l')
    bprintf(&buf2, "preconf/pre_latest");
  else
    bprintf(&buf2, "preconf/%j", block_number);

  TRY_ASYNC(c4_send_internal_request(ctx, (char*) buf2.data.data, NULL, 0, &preconf_data)); // get the raw-data
  if (!preconf_data.len) THROW_ERROR("No preconf data found, currently only supports preconfs");
  // Extract payload and signature
  bytes_t payload   = bytes_slice(preconf_data, 0, preconf_data.len - 65);
  bytes_t signature = bytes_slice(preconf_data, preconf_data.len - 65, 65);

  // build the proof
  ssz_builder_t preconf_proof              = ssz_builder_for_op_type(OP_SSZ_VERIFY_PRECONF_PROOF);
  ssz_builder_t payload_builder_compressed = ssz_builder_for_def(ssz_get_def(preconf_proof.def, "payload")->def.container.elements + 0);
  buffer_append(&payload_builder_compressed.fixed, payload);
  ssz_add_builders(&preconf_proof, "payload", payload_builder_compressed);
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
  c4_op_add_block_proof(ctx, &block_proof, "block_proof", &preconf_proof);

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
  c4_op_add_block_proof(ctx, &block_proof, "block_proof", &preconf_proof);

  ctx->proof = op_create_proof_request(
      ctx->chain_id,
      NULL_SSZ_BUILDER,
      block_proof,
      NULL_SSZ_BUILDER);

  return C4_SUCCESS;
}
