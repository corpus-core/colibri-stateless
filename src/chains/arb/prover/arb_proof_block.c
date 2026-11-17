/*
 * Copyright (c) 2025,2026 corpus.core GmbH
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

#include "arb_prover.h"
#include "beacon_types.h"
#include "cl_req.h"
#include "crypto.h"
#include "el_header.h"
#include "ssz.h"
#include <string.h>

typedef struct {
  bytes_t   el_header;
  bytes_t   feed_proof;
  bytes32_t el_block_hash;
} arb_el_snap_t;

static bool is_latest(json_t block) {
  return block.type == JSON_TYPE_STRING && block.start && block.len == 8 && strncmp(block.start, "\"latest\"", 8) == 0;
}

static void snap_id(bool with_body, bytes32_t id) {
  uint8_t buf[2] = {'A', with_body ? 1 : 0};
  keccak(bytes(buf, 2), id);
}

static void fill_from_snap(eth_block_t* out, const arb_el_snap_t* snap) {
  const ssz_def_t* def = eth_ssz_verification_type(ETH_SSZ_ARB_FEED_PROOF);
  ssz_ob_t         ob  = {.def = def, .bytes = snap->feed_proof};
  memset(out, 0, sizeof(*out));
  out->proof_type        = C4_BLOCK_PROOF_TYPE_SEQUENCER;
  out->el_header         = snap->el_header;
  out->sequencer.payload = snap->feed_proof;
  if (def) out->sequencer.signature = ssz_get(&ob, "signature").bytes;
  memcpy(out->el_block_hash, snap->el_block_hash, 32);
  if (snap->el_header.data)
    out->slot = eth_el_header_get_uint64(snap->el_header, EL_BLOCK_NUMBER);
}

static c4_status_t attach_snap(prover_ctx_t* ctx, bytes32_t id, bytes_t feed, eth_block_t* out) {
  const ssz_def_t* def  = eth_ssz_verification_type(ETH_SSZ_ARB_FEED_PROOF);
  arb_el_snap_t*   snap = safe_calloc(1, sizeof(*snap) + feed.len);
  uint8_t*         tail = (uint8_t*) (snap + 1);
  memcpy(tail, feed.data, feed.len);
  snap->feed_proof = bytes(tail, feed.len);
  ssz_ob_t ob      = {.def = def, .bytes = snap->feed_proof};
  snap->el_header  = ssz_get(&ob, "elHeader").bytes;
  if (!snap->el_header.data || snap->el_header.len == 0 || ssz_get(&ob, "signature").bytes.len != 65) {
    safe_free(snap);
    THROW_ERROR("arb feed proof is missing the header or signature");
  }
  keccak(snap->el_header, snap->el_block_hash);
  c4_state_cache_set(&ctx->state, id, bytes((uint8_t*) snap, sizeof(*snap) + feed.len));
  fill_from_snap(out, snap);
  return C4_SUCCESS;
}

c4_status_t arb_get_el_block(prover_ctx_t* ctx, json_t block, eth_block_t* out, bool with_body) {
  if (!is_latest(block)) THROW_ERROR("only latest is supported");
  if (with_body) THROW_ERROR("arb feed proof does not include transactions");

  bytes32_t id = {0};
  snap_id(with_body, id);
  bytes_t existing = c4_state_cache_get(&ctx->state, id);
  if (existing.data) {
    fill_from_snap(out, (const arb_el_snap_t*) existing.data);
    return C4_SUCCESS;
  }

  bytes_t feed = NULL_BYTES;
  TRY_ASYNC(c4_send_internal_request(ctx, "arb_preconf/latest", NULL, 0, &feed));
  const ssz_def_t* def = eth_ssz_verification_type(ETH_SSZ_ARB_FEED_PROOF);
  ssz_ob_t         ob  = {.def = def, .bytes = feed};
  if (!def || !feed.data || !ssz_is_valid(ob, true, &ctx->state)) {
    if (!ctx->state.error) c4_state_add_error(&ctx->state, "invalid arb feed proof");
    return C4_ERROR;
  }
  return attach_snap(ctx, id, feed, out);
}

bool arb_add_feed_proof(prover_ctx_t* ctx, ssz_builder_t* builder, eth_block_t* block_data, blockroot_proof_t* historic) {
  (void) ctx;
  (void) historic;
  const ssz_def_t* def = eth_ssz_verification_type(ETH_SSZ_ARB_FEED_PROOF);
  if (block_data->proof_type != C4_BLOCK_PROOF_TYPE_SEQUENCER || !block_data->sequencer.payload.data || !def)
    return false;
  ssz_ob_t feed = {.def = def, .bytes = block_data->sequencer.payload};
  ssz_add_ob(builder, "elProof", feed);
  return true;
}

void arb_register_block_proof_prover(void) {
  c4_register_block_proof_prover(C4_CHAIN_TYPE_ARBITRUM, arb_add_feed_proof, arb_get_el_block);
}
