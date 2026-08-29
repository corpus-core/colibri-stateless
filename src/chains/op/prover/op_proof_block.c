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
#include "bytes.h"
#include "crypto.h"
#include "el_header.h"
#include "eth_tools.h"
#include "header_cache.h"
#include "json.h"
#include "op_prover.h"
#include "op_verify.h"
#include "prover.h"
#include "ssz.h"
#include <string.h>

typedef struct {
  bytes_t   el_header;
  ssz_ob_t  el_body;
  bytes_t   sequencer_payload;
  bytes_t   sequencer_signature;
  bytes32_t el_block_hash;
} op_el_snap_t;

static void snap_id(json_t block, bool with_body, bytes32_t id) {
  uint8_t buf[80] = {0};
  buf[0]          = with_body ? 1 : 0;
  uint32_t n      = block.len < 64 ? (uint32_t) block.len : 64;
  if (block.start && n) memcpy(buf + 1, block.start, n);
  keccak(bytes(buf, 1 + n), id);
}

static void fill_from_snap(eth_block_t* out, const op_el_snap_t* snap) {
  memset(out, 0, sizeof(*out));
  out->el_header = snap->el_header;
  out->el_body   = snap->el_body;
  memcpy(out->el_block_hash, snap->el_block_hash, 32);
  if (snap->el_header.data)
    out->slot = eth_el_header_get_uint64(snap->el_header, EL_BLOCK_NUMBER);
  if (snap->sequencer_payload.data && snap->sequencer_signature.len == 65) {
    out->proof_type          = C4_BLOCK_PROOF_TYPE_SEQUENCER;
    out->sequencer.payload   = snap->sequencer_payload;
    out->sequencer.signature = snap->sequencer_signature;
  }
}

static bytes_t copy_into_tail(uint8_t** tail, bytes_t src, bool take_ownership) {
  if (!src.data || !src.len) return NULL_BYTES;
  memcpy(*tail, src.data, src.len);
  bytes_t out = bytes(*tail, src.len);
  *tail += src.len;
  if (take_ownership) safe_free(src.data);
  return out;
}

static c4_status_t attach_snap(prover_ctx_t* ctx, bytes32_t id, op_el_snap_t src, eth_block_t* out) {
  uint32_t extra = src.el_header.len +
                   (src.el_body.bytes.data ? src.el_body.bytes.len : 0) +
                   src.sequencer_payload.len +
                   src.sequencer_signature.len;
  op_el_snap_t* snap = safe_calloc(1, sizeof(op_el_snap_t) + extra);
  uint8_t*      tail = (uint8_t*) (snap + 1);
  *snap              = src;
  snap->el_header            = copy_into_tail(&tail, src.el_header, true);
  snap->el_body.bytes        = copy_into_tail(&tail, src.el_body.bytes, true);
  snap->sequencer_payload    = copy_into_tail(&tail, src.sequencer_payload, false);
  snap->sequencer_signature  = copy_into_tail(&tail, src.sequencer_signature, false);

  c4_state_cache_set(&ctx->state, id, bytes((uint8_t*) snap, sizeof(op_el_snap_t) + extra));
  fill_from_snap(out, snap);
  return C4_SUCCESS;
}

#ifdef EL_HEADER_CACHE
static bool try_cached_verified_header(prover_ctx_t* ctx, json_t block, bool with_body, op_el_snap_t* out) {
  const verified_header_entry_t* cached = NULL;
  if (!block.start || block.len < 4) return false;
  if (block.start[1] != '0' || block.start[2] != 'x') return false;

  if (block.len == 68) {
    bytes32_t hash = {0};
    buffer_t  buf  = stack_buffer(hash);
    json_as_bytes(block, &buf);
    if (memcmp(ctx->last_block_hash, hash, 32) != 0) return false;
    cached = c4_header_cache_get_by_hash(ctx->chain_id, hash);
  }
  else {
    cached = c4_header_cache_get_by_number(ctx->chain_id, json_as_uint64(block));
    if (!cached || memcmp(ctx->last_block_hash, cached->block_hash, 32) != 0) return false;
  }
  if (!cached || !cached->el_header.data) return false;
  if (with_body && !cached->el_body.def) return false;

  bytes_t  header = c4_header_cache_get_el_header(ctx->chain_id, cached->block_hash, with_body ? &out->el_body : NULL);
  if (!header.data) return false;
  out->el_header = header;
  memcpy(out->el_block_hash, cached->block_hash, 32);
  return true;
}
#endif

c4_status_t op_get_el_block(prover_ctx_t* ctx, json_t block, eth_block_t* out, bool with_body) {
  bytes32_t id = {0};
  snap_id(block, with_body, id);
  bytes_t existing = c4_state_cache_get(&ctx->state, id);
  if (existing.data) {
    fill_from_snap(out, (op_el_snap_t*) existing.data);
    return C4_SUCCESS;
  }

  op_el_snap_t snap = {0};
#ifdef EL_HEADER_CACHE
  if (try_cached_verified_header(ctx, block, with_body, &snap))
    return attach_snap(ctx, id, snap, out);
#endif

  uint8_t  path[200]    = {0};
  buffer_t path_buf     = stack_buffer(path);
  bytes_t  preconf_data = {0};
  if ((ctx->flags & C4_PROVER_FLAG_UNSTABLE_LATEST) == 0 && block.start && block.start[1] == 'l')
    bprintf(&path_buf, "preconf/pre_latest");
  else
    bprintf(&path_buf, "preconf/%j", block);

  TRY_ASYNC(c4_send_internal_request(ctx, (char*) path_buf.data.data, NULL, 0, &preconf_data));
  if (preconf_data.len < 65) THROW_ERROR("No preconf data found, currently only supports preconfs");

  bytes_t payload   = bytes_slice(preconf_data, 0, preconf_data.len - 65);
  bytes_t signature = bytes_slice(preconf_data, preconf_data.len - 65, 65);

  bytes_t raw = {0};
  TRY_ASYNC(op_decompress_preconf(&ctx->state, payload, &raw));
  c4_status_t st = op_el_from_preconf_bytes(&ctx->state, raw, &snap.el_header, &snap.el_body, snap.el_block_hash);
  safe_free(raw.data);
  if (st != C4_SUCCESS) {
    safe_free(snap.el_header.data);
    safe_free(snap.el_body.bytes.data);
    return C4_ERROR;
  }

  snap.sequencer_payload   = payload;
  snap.sequencer_signature = signature;
  return attach_snap(ctx, id, snap, out);
}

bool op_add_sequencer_proof(prover_ctx_t* ctx, ssz_builder_t* builder, eth_block_t* block_data, blockroot_proof_t* historic) {
  (void) ctx;
  (void) historic;
  if (block_data->proof_type != C4_BLOCK_PROOF_TYPE_SEQUENCER ||
      !block_data->sequencer.payload.data || block_data->sequencer.signature.len != 65)
    return false;

  ssz_builder_t seq     = ssz_builder_for_type(ETH_SSZ_SEQUENCER_PROOF);
  const ssz_def_t* pdef = ssz_get_def(seq.def, "payload");
  if (!pdef || pdef->type != SSZ_TYPE_UNION) return false;
  ssz_builder_t payload_builder = ssz_builder_for_def(pdef->def.container.elements + 0);
  buffer_append(&payload_builder.fixed, block_data->sequencer.payload);
  ssz_add_builders(&seq, "payload", payload_builder);
  ssz_add_bytes(&seq, "signature", block_data->sequencer.signature);
  ssz_add_builders(builder, "block", seq);
  return true;
}

void op_register_block_proof_prover(void) {
  c4_register_block_proof_prover(C4_CHAIN_TYPE_OP, op_add_sequencer_proof, op_get_el_block);
}
