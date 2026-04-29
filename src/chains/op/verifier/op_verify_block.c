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

#include "beacon_types.h"
#include "bytes.h"
#include "chains.h"
#include "crypto.h"
#include "eth_account.h"
#include "eth_tx.h"
#include "eth_verify.h"
#include "json.h"
#include "logger.h"
#include "op_chains_conf.h"
#include "op_types.h"
#include "op_verify.h"
#include "op_zstd.h"
#include "patricia.h"
#include "plugin.h"
#include "rlp.h"
#include "ssz.h"
#include "sync_committee.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const ssz_def_t EXECUTION_PAYLOAD_CONTAINER = SSZ_CONTAINER("payload", DENEP_EXECUTION_PAYLOAD);

/**
 * Build the storage key used for the cached OP execution payload.
 * Single slot per chain - any new full payload replaces the previous one.
 */
static void op_payload_key(chain_id_t chain_id, char* out) {
  sbprintf(out, "op_payload_%l", (uint64_t) chain_id);
}

/**
 * Load a previously verified execution payload from local storage.
 *
 * @param chain_id chain identifier
 * @return raw decompressed bytes [parent_hash(32) | ssz_execution_payload], or NULL_BYTES if absent.
 *         Caller owns the returned buffer and must `safe_free(result.data)`.
 */
static bytes_t op_load_cached_payload(chain_id_t chain_id) {
  storage_plugin_t storage = {0};
  c4_get_storage_config(&storage);
  if (!storage.get) return NULL_BYTES;

  char name[64] = {0};
  op_payload_key(chain_id, name);
  buffer_t buf = {0};
  if (!storage.get(name, &buf) || !buf.data.data || buf.data.len < 32) {
    if (buf.data.data) buffer_free(&buf);
    return NULL_BYTES;
  }
  return buf.data;
}

/**
 * Persist a freshly verified execution payload and update the chain client state.
 * The previous cached entry is implicitly replaced; the chain state transitions to
 * `C4_STATE_SYNC_EXECUTION_PAYLOAD` referencing (block_number, blockhash).
 *
 * @param chain_id chain identifier
 * @param decompressed_data full decompressed preconf data: [parent_hash(32) | ssz_execution_payload]
 * @param block_number block number of the verified payload
 * @param blockhash block hash of the verified payload
 */
static void op_store_cached_payload(chain_id_t chain_id, bytes_t decompressed_data, uint64_t block_number, bytes32_t blockhash) {
  storage_plugin_t storage = {0};
  c4_get_storage_config(&storage);
  if (!storage.set) return;

  char name[64] = {0};
  op_payload_key(chain_id, name);
  storage.set(name, decompressed_data);

  c4_chain_state_t state             = {.status = C4_STATE_SYNC_EXECUTION_PAYLOAD};
  state.data.block.block_number      = block_number;
  memcpy(state.data.block.blockhash, blockhash, 32);
  c4_set_chain_state(chain_id, &state);
}

static void verify_signature(bytes_t data, bytes_t signature, uint64_t chain_id, address_t address) {
  uint8_t buf[96] = {0};
  uint8_t pub[64] = {0};
  uint64_to_be(buf + 64 - 8, chain_id);
  keccak(data, buf + 64);
  keccak(bytes(buf, 96), buf);
  secp256k1_recover(buf, signature, pub);
  keccak(bytes(pub, 64), buf);
  memcpy(address, buf + 12, 20);
}

/**
 * Verify that the (already extracted) execution payload matches the user-requested block.
 *
 * @param ctx verify context (used for error reporting)
 * @param ep execution payload to inspect
 * @param block_number user-requested block (hex number or hex blockhash JSON string), may be NULL
 * @return true on match (or no constraint), false on mismatch (error already added to ctx)
 */
static bool match_requested_block(verify_ctx_t* ctx, ssz_ob_t* ep, json_t* block_number) {
  if (!block_number || block_number->len <= 2 || block_number->start[1] != '0' || block_number->start[2] != 'x')
    return true;

  bytes32_t buf    = {0};
  buffer_t  buffer = stack_buffer(buf);
  if (block_number->len == 68) { // blockhash
    json_as_bytes(*block_number, &buffer);
    bytes_t block_hash = ssz_get(ep, "blockHash").bytes;
    if (memcmp(buf, block_hash.data, 32)) {
      c4_state_add_error(&ctx->state, "blockhash mismatch");
      return false;
    }
  }
  else if (json_as_uint64(*block_number) != ssz_get_uint64(ep, "blockNumber")) {
    c4_state_add_error(&ctx->state, "blocknumber mismatch");
    return false;
  }
  return true;
}

/**
 * Reconstruct an `ssz_ob_t*` execution payload in-place at the start of the given buffer.
 * The first 32 bytes (parent_hash) are overwritten by the ssz_ob_t struct; the payload bytes
 * (offset 32..end) remain intact and are referenced via `bytes_slice`.
 *
 * The caller must have already extracted parent_hash before invoking this helper.
 */
static ssz_ob_t* embed_execution_payload(bytes_t data) {
  ssz_ob_t* ep = (void*) data.data;
  ep->def      = &EXECUTION_PAYLOAD_CONTAINER;
  ep->bytes    = bytes_slice(data, 32, data.len - 32);
  return ep;
}

ssz_ob_t* op_extract_verified_execution_payload(verify_ctx_t* ctx, ssz_ob_t block_proof, json_t* block_number, bytes32_t parent_hash) {
  // Cached path: client signaled (via OP_BLOCKPROOF_UNION = NONE) that it already has this block.
  // Load the previously verified execution payload from local storage.
  if (block_proof.def && block_proof.def->type == SSZ_TYPE_NONE) {
    bytes_t cached = op_load_cached_payload(ctx->chain_id);
    if (!cached.data || cached.len < 32) {
      c4_state_add_error(&ctx->state, "block_proof is none but no cached execution payload available");
      return NULL;
    }

    if (parent_hash) memcpy(parent_hash, cached.data, 32);

    ssz_ob_t* execution_payload = embed_execution_payload(cached);
    if (!match_requested_block(ctx, execution_payload, block_number)) {
      safe_free(execution_payload);
      return NULL;
    }
    return execution_payload;
  }

  const op_chain_config_t* config          = op_get_chain_config(ctx->chain_id);
  address_t                signer          = {0};
  ssz_ob_t                 compressed_data = ssz_get(&block_proof, "payload");
  ssz_ob_t                 signature       = ssz_get(&block_proof, "signature");

  if (config == NULL) {
    c4_state_add_error(&ctx->state, "chain not supported");
    return NULL;
  }

  size_t expected_size = op_zstd_get_decompressed_size(compressed_data.bytes);
  if (expected_size == 0) RETURN_VERIFY_ERROR(ctx, "failed to get decompressed size");

  bytes_t decompressed_data = bytes(safe_malloc(expected_size), expected_size);
  size_t  actual_size       = op_zstd_decompress(compressed_data.bytes, decompressed_data);

  if (actual_size != expected_size) {
    safe_free(decompressed_data.data);
    c4_state_add_error(&ctx->state, "failed to decompress data");
    return NULL;
  }

  // Verify signature from sequencer
  verify_signature(decompressed_data, signature.bytes, ctx->chain_id, signer);

  if (memcmp(config->sequencer_address, signer, 20)) {
    safe_free(decompressed_data.data);
    c4_state_add_error(&ctx->state, "invalid sequencer signature");
    return NULL;
  }

  // Validate against the requested block BEFORE the in-place ssz_ob_t embedding so the
  // decompressed buffer remains intact for caching.
  ssz_ob_t payload_view = {.def = &EXECUTION_PAYLOAD_CONTAINER, .bytes = bytes_slice(decompressed_data, 32, decompressed_data.len - 32)};
  if (!match_requested_block(ctx, &payload_view, block_number)) {
    safe_free(decompressed_data.data);
    return NULL;
  }

  // Cache the freshly verified payload so subsequent requests for the same block can omit it.
  bytes_t   bh_bytes = ssz_get(&payload_view, "blockHash").bytes;
  uint64_t  bn       = ssz_get_uint64(&payload_view, "blockNumber");
  bytes32_t bh       = {0};
  if (bh_bytes.len == 32) {
    memcpy(bh, bh_bytes.data, 32);
    op_store_cached_payload(ctx->chain_id, decompressed_data, bn, bh);
  }

  if (parent_hash) memcpy(parent_hash, decompressed_data.data, 32);

  // Embed ssz_ob_t header in-place: this overwrites the parent_hash bytes (already captured above)
  // and lets the caller free both the wrapper and payload bytes via a single `safe_free`.
  return embed_execution_payload(decompressed_data);
}

bool op_verify_block(verify_ctx_t* ctx) {
  bool      is_blocknumber    = strcmp(ctx->method, "eth_blockNumber") == 0;
  json_t    block_number      = is_blocknumber ? (json_t) {.type = JSON_TYPE_STRING, .start = "\"latest\"", .len = 8} : json_at(ctx->args, 0);
  bool      include_txs       = is_blocknumber ? false : json_as_bool(json_at(ctx->args, 1));
  ssz_ob_t  block_proof       = ssz_get(&ctx->proof, "block_proof");
  bytes32_t parent_root       = {0};
  bytes32_t withdrawel_root   = {0};
  ssz_ob_t* execution_payload = op_extract_verified_execution_payload(ctx, block_proof, &block_number, &parent_root);
  if (!execution_payload) return false;

  if (is_blocknumber) {
    ctx->data       = ssz_get(execution_payload, "blockNumber");
    ctx->data.bytes = bytes_dup(ctx->data.bytes); // need to copy bytes, because payload will be deleted
    ctx->success    = true;
    ctx->flags |= VERIFY_FLAG_FREE_DATA;
  }
  else {
    ssz_hash_tree_root(ssz_get(execution_payload, "withdrawals"), withdrawel_root);
    eth_set_block_data(ctx, ETH_BLOCK_DATA_MASK_ALL, *execution_payload, parent_root, withdrawel_root, include_txs);
  }
  safe_free(execution_payload);
  ctx->success = true;
  return true;
}
