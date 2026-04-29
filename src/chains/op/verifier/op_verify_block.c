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

void op_payload_key(chain_id_t chain_id, char* out) {
  sbprintf(out, "op_payload_%l", (uint64_t) chain_id);
}

bytes_t op_load_cached_payload(chain_id_t chain_id) {
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

void op_store_cached_payload(chain_id_t chain_id, bytes_t decompressed_data, uint64_t block_number, bytes32_t blockhash) {
  storage_plugin_t storage = {0};
  c4_get_storage_config(&storage);
  if (!storage.set) return;

  // Only-if-newer guard: never replace a cached payload with one for an older or
  // identical block. This both honours the desired caching policy (always advance)
  // and reduces the race-window where parallel in-flight requests could be invalidated.
  c4_chain_state_t current = c4_get_chain_state(chain_id);
  if (current.status == C4_STATE_SYNC_EXECUTION_PAYLOAD && block_number <= current.data.block.block_number)
    return;

  char name[64] = {0};
  op_payload_key(chain_id, name);
  storage.set(name, decompressed_data);

  c4_chain_state_t state        = {.status = C4_STATE_SYNC_EXECUTION_PAYLOAD};
  state.data.block.block_number = block_number;
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
 * Allocate a fresh `ssz_ob_t` view onto `[data + 32 .. end]` (the SSZ execution payload
 * portion of a `[parent_hash | payload]` buffer). The buffer itself is NOT copied;
 * its lifetime is managed by whoever owns `data` (typically `state.requests`).
 *
 * Caller must `safe_free(ep)` to release the wrapper; the underlying bytes are
 * released elsewhere (state cleanup or storage helpers).
 */
static ssz_ob_t* make_payload_view(bytes_t data) {
  ssz_ob_t* ep = safe_calloc(1, sizeof(ssz_ob_t));
  ep->def      = &EXECUTION_PAYLOAD_CONTAINER;
  ep->bytes    = bytes_slice(data, 32, data.len - 32);
  return ep;
}

/**
 * Adopt `bytes` as a new `C4_DATA_TYPE_CACHE` entry on `ctx->state.requests` keyed by
 * `blockhash`. Ownership of `bytes.data` is transferred and will be released via
 * `c4_state_free` on verifier teardown; the caller must NOT free it afterwards.
 *
 * Skips insertion if an entry with the same id already exists (prevents duplicate
 * snapshots when multiple proofs in one request reference the same block).
 */
static void adopt_cache_entry(verify_ctx_t* ctx, bytes32_t blockhash, bytes_t bytes) {
  if (c4_state_get_data_request_by_id(&ctx->state, blockhash)) {
    safe_free(bytes.data);
    return;
  }
  data_request_t* snap = safe_calloc(1, sizeof(data_request_t));
  snap->type           = C4_DATA_TYPE_CACHE;
  snap->chain_id       = ctx->chain_id;
  snap->response       = bytes;
  snap->validated      = true;
  memcpy(snap->id, blockhash, 32);
  snap->next           = ctx->state.requests;
  ctx->state.requests  = snap;
}

ssz_ob_t* op_extract_verified_execution_payload(verify_ctx_t* ctx, ssz_ob_t block_proof, json_t* block_number, bytes32_t parent_hash) {
  // Cached-ref path: client signalled (OP_BLOCKPROOF_UNION variant `cached_ref`) that it
  // already has this block. The 32-byte payload is the blockhash hint identifying the snapshot.
  if (block_proof.def && block_proof.bytes.len == 32 && strcmp(block_proof.def->name, "cached_ref") == 0) {
    bytes32_t hint = {0};
    memcpy(hint, block_proof.bytes.data, 32);

    bytes_t cached     = NULL_BYTES;
    bool    from_state = false;

    // Fast path: snapshot taken at request start (race-free across in-flight requests).
    data_request_t* snap = c4_state_get_data_request_by_id(&ctx->state, hint);
    if (snap && snap->type == C4_DATA_TYPE_CACHE && snap->response.data && snap->response.len >= 32) {
      cached     = snap->response;
      from_state = true;
    }
    else {
      // Offline / no-snapshot path: read directly from local storage.
      cached = op_load_cached_payload(ctx->chain_id);
      if (!cached.data || cached.len < 32) {
        if (cached.data) safe_free(cached.data);
        c4_state_add_error(&ctx->state, "block_proof is cached_ref but no cached execution payload available");
        return NULL;
      }
    }

    // Defensive: the storage might have advanced since the prover decided to emit cached_ref.
    // Verify the cached payload's blockhash actually matches the prover-supplied hint.
    ssz_ob_t view_tmp = {.def = &EXECUTION_PAYLOAD_CONTAINER, .bytes = bytes_slice(cached, 32, cached.len - 32)};
    bytes_t  bh       = ssz_get(&view_tmp, "blockHash").bytes;
    if (bh.len != 32 || memcmp(bh.data, hint, 32) != 0) {
      if (!from_state) safe_free(cached.data);
      c4_state_add_error(&ctx->state, "cached payload blockhash does not match prover hint (stale cache)");
      return NULL;
    }

    // Hand storage-loaded bytes over to state.requests for auto-cleanup; subsequent
    // proofs in the same verification reuse the same snapshot.
    if (!from_state) adopt_cache_entry(ctx, hint, cached);

    if (parent_hash) memcpy(parent_hash, cached.data, 32);
    ssz_ob_t* execution_payload = make_payload_view(cached);
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

  ssz_ob_t payload_view = {.def = &EXECUTION_PAYLOAD_CONTAINER, .bytes = bytes_slice(decompressed_data, 32, decompressed_data.len - 32)};
  if (!match_requested_block(ctx, &payload_view, block_number)) {
    safe_free(decompressed_data.data);
    return NULL;
  }

  bytes_t   bh_bytes = ssz_get(&payload_view, "blockHash").bytes;
  uint64_t  bn       = ssz_get_uint64(&payload_view, "blockNumber");
  bytes32_t bh       = {0};
  if (bh_bytes.len == 32) {
    memcpy(bh, bh_bytes.data, 32);
    // Persist for cross-request caching (only-if-newer guard inside).
    op_store_cached_payload(ctx->chain_id, decompressed_data, bn, bh);
  }

  if (parent_hash) memcpy(parent_hash, decompressed_data.data, 32);

  // Adopt the decompressed buffer into state.requests so further proofs in the same
  // verification (e.g. multiple block proofs in eth_getLogs) can find it via the
  // blockhash without a second ZSTD decompression. Auto-freed by `c4_state_free`.
  if (bh_bytes.len == 32) {
    adopt_cache_entry(ctx, bh, decompressed_data);
  }
  else {
    // No blockhash to key by - fall back to legacy ownership: the buffer must be
    // freed by someone else; emit a warning-grade error so callers notice.
    c4_state_add_error(&ctx->state, "execution payload has no blockHash field");
    safe_free(decompressed_data.data);
    return NULL;
  }

  return make_payload_view(decompressed_data);
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
