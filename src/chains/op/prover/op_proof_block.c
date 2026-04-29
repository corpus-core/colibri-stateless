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
#include "op_proof_types.h"
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
 * Decide whether the verifier already has the requested execution payload cached
 * and, if so, copy the cached blockhash into `out_hint`.
 *
 * The decision is metadata-only: it compares the user-supplied JSON block reference
 * (hex block number or 32-byte block hash) directly against the cached
 * `(block_number, blockhash)` tuple in `ctx->client_state`. This deliberately
 * avoids decompressing the (~100 kB) preconf payload because:
 *   - block-by-number / block-by-hash requests already carry the comparison key in
 *     the JSON, so no decompression is needed.
 *   - "latest" / "earliest" / "pending" are intentionally NOT optimised here:
 *     deciding cache-hit for those would require decompressing the freshly fetched
 *     preconf, which is the work we want to avoid.
 *
 * @param ctx prover context
 * @param requested user-supplied JSON block reference (e.g. `"0x1234"` or `"0xabcd...``)
 * @param out_hint receives the cached blockhash on hit (32 bytes)
 * @return true if the verifier already has this block cached
 */
static bool client_already_has_block(prover_ctx_t* ctx, json_t requested, bytes32_t out_hint) {
  if (!ctx->client_state.data || !ctx->client_state.len) return false;
  if (!requested.start || requested.len < 4) return false;
  // only hex block references (number or hash) can be matched without decompression
  if (requested.start[1] != '0' || requested.start[2] != 'x') return false;

  c4_chain_state_t cs = c4_state_deserialize(ctx->client_state);
  if (cs.status != C4_STATE_SYNC_EXECUTION_PAYLOAD) return false;

  bool hit = false;
  if (requested.len == 68) { // 0x + 64 hex chars + 2 quotes -> 32-byte block hash
    bytes32_t hash = {0};
    buffer_t  buf  = {.data = bytes(hash, 32), .allocated = -32};
    json_as_bytes(requested, &buf);
    hit = memcmp(hash, cs.data.block.blockhash, 32) == 0;
  }
  else
    hit = json_as_uint64(requested) == cs.data.block.block_number;

  if (hit) memcpy(out_hint, cs.data.block.blockhash, 32);
  return hit;
}

void c4_op_add_block_proof(prover_ctx_t* ctx, json_t requested, ssz_builder_t* parent, const char* name, ssz_builder_t* preconf_proof) {
  bytes32_t hint = {0};
  if (client_already_has_block(ctx, requested, hint)) {
    log_debug("OP block already cached on client - emitting block_proof = cached_ref");
    ssz_builder_free(preconf_proof);
    // Emit union variant `cached_ref` (index 1) carrying the cached blockhash so the verifier
    // can locate the matching `C4_DATA_TYPE_CACHE` snapshot via `c4_state_get_data_request_by_id`.
    ssz_add_ob(parent, name, (ssz_ob_t) {.def = &OP_BLOCKPROOF_UNION[1], .bytes = bytes(hint, 32)});
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
  c4_op_add_block_proof(ctx, block_number, &block_proof, "block_proof", &preconf_proof);

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
  json_t        latest        = (json_t) {.type = JSON_TYPE_STRING, .start = "\"latest\"", .len = 8};

  TRY_ASYNC(c4_op_create_block_proof(ctx, latest, &preconf_proof));

  // build the proof
  ssz_builder_t block_proof = ssz_builder_for_op_type(OP_SSZ_VERIFY_BLOCK_PROOF);
  c4_op_add_block_proof(ctx, latest, &block_proof, "block_proof", &preconf_proof);

  ctx->proof = op_create_proof_request(
      ctx->chain_id,
      NULL_SSZ_BUILDER,
      block_proof,
      NULL_SSZ_BUILDER);

  return C4_SUCCESS;
}
