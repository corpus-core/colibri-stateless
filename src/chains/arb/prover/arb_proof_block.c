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
#include "bytes.h"
#include "cl_req.h"
#include "crypto.h"
#include "eip2935.h"
#include "el_header.h"
#include "eth_req.h"
#include "json.h"
#include "rlp.h"
#include "ssz.h"
#include <string.h>

/** Quoted 32-byte word (`"0x"` + 64 nibbles + `"`) plus a terminator. */
#define JSON_BYTES32_SIZE 69

typedef struct {
  bytes_t   el_header;
  bytes_t   feed_proof;
  bytes_t   newer_header;
  bytes_t   el_body; // ETH_SSZ_EL_BLOCK_CONTENT, empty when the request is header-only
  json_t    history_proof;
  bytes32_t el_block_hash;
} arb_el_snap_t;

static bool is_latest(json_t block) {
  return block.type == JSON_TYPE_STRING && block.start && block.len == 8 && strncmp(block.start, "\"latest\"", 8) == 0;
}

static bool is_hex_digit(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/** Quoted `0x` plus `digits` hex characters. The quotes are part of the JSON token. */
static bool is_quoted_hex(json_t block, int digits) {
  if (block.type != JSON_TYPE_STRING || !block.start || block.len != digits + 4) return false;
  if (block.start[0] != '"' || block.start[block.len - 1] != '"' || block.start[1] != '0' || block.start[2] != 'x') return false;
  for (int i = 3; i < block.len - 1; i++)
    if (!is_hex_digit(block.start[i])) return false;
  return true;
}

static bool json_field_present(json_t block, const char* name) {
  json_t field = json_get(block, name);
  return field.type != JSON_TYPE_NOT_FOUND && field.type != JSON_TYPE_NULL;
}

/** Header layout of an `eth_getBlockByHash` object, from the newest field it carries. */
static fork_id_t header_fork(json_t block) {
  if (json_field_present(block, EL_SLOT_NUMBER) || json_field_present(block, EL_BLOCK_ACCESS_LIST_HASH))
    return C4_FORK_GLOAS;
  if (json_field_present(block, EL_REQUESTS_HASH))
    return C4_FORK_ELECTRA;
  if (json_field_present(block, EL_PARENT_BEACON_BLOCK_ROOT) || json_field_present(block, EL_BLOB_GAS_USED) || json_field_present(block, EL_EXCESS_BLOB_GAS))
    return C4_FORK_DENEB;
  if (json_field_present(block, EL_WITHDRAWALS_ROOT))
    return C4_FORK_CAPELLA;
  return C4_FORK_BELLATRIX;
}

static void latest_id(bytes32_t id) {
  uint8_t buf[2] = {'A', 0};
  keccak(bytes(buf, 2), id);
}

static void snap_id(json_t block, bool with_body, bytes32_t id) {
  buffer_t buf  = {0};
  uint8_t  flag = with_body ? 'F' : 'B';
  buffer_append(&buf, bytes(&flag, 1));
  if (block.start && block.len) buffer_append(&buf, bytes((uint8_t*) block.start, (uint32_t) block.len));
  keccak(buf.data, id);
  buffer_free(&buf);
}

static void fill_from_snap(eth_block_t* out, const arb_el_snap_t* snap) {
  const ssz_def_t* def = eth_ssz_verification_type(ETH_SSZ_ARB_FEED_PROOF);
  ssz_ob_t         ob  = {.def = def, .bytes = snap->feed_proof};
  memset(out, 0, sizeof(*out));
  out->proof_type                      = C4_BLOCK_PROOF_TYPE_SEQUENCER;
  out->el_header                       = snap->el_header;
  out->sequencer.payload               = snap->feed_proof;
  out->sequencer.history.newer_header  = snap->newer_header;
  out->sequencer.history.history_proof = snap->history_proof;
  if (def) out->sequencer.signature = ssz_get(&ob, "signature").bytes;
  memcpy(out->el_block_hash, snap->el_block_hash, 32);
  if (snap->el_body.data) {
    out->el_body.bytes = snap->el_body;
    out->el_body.def   = eth_ssz_verification_type(ETH_SSZ_EL_BLOCK_CONTENT);
  }
  if (snap->el_header.data)
    out->slot = eth_el_header_get_uint64(snap->el_header, EL_BLOCK_NUMBER);
}

/**
 * Copies the feed proof and, when set, the older header and the execution body into one
 * state-owned snapshot. `header` and `body` are freed. `history` is borrowed from a
 * request that lives in the same state.
 *
 * @param ctx prover context
 * @param id snapshot key
 * @param feed arbFeedProof container (no union selector)
 * @param header RLP header of an older block, or empty when `feed` is the requested block
 * @param history eth_getProof of the history contract, borrowed
 * @param block_hash hash of `header`; ignored when `header` is empty
 * @param body `ETH_SSZ_EL_BLOCK_CONTENT` bytes, or empty for a header-only snapshot
 * @param out output block
 * @return `C4_SUCCESS` or `C4_ERROR`
 */
static c4_status_t cache_snap(prover_ctx_t* ctx, bytes32_t id, bytes_t feed, bytes_t header, json_t history, const uint8_t* block_hash, bytes_t body, eth_block_t* out) {
  uint32_t       size = (uint32_t) sizeof(arb_el_snap_t) + feed.len + header.len + body.len;
  arb_el_snap_t* snap = safe_calloc(1, size);
  uint8_t*       tail = (uint8_t*) (snap + 1);
  memcpy(tail, feed.data, feed.len);
  snap->feed_proof = bytes(tail, feed.len);
  tail += feed.len;
  const ssz_def_t* def         = eth_ssz_verification_type(ETH_SSZ_ARB_FEED_PROOF);
  ssz_ob_t         ob          = {.def = def, .bytes = snap->feed_proof};
  bytes_t          feed_header = ssz_get(&ob, "elHeader").bytes;
  if (!feed_header.data || feed_header.len == 0 || ssz_get(&ob, "signature").bytes.len != 65) {
    safe_free(snap);
    safe_free(header.data);
    safe_free(body.data);
    THROW_ERROR("arb feed proof is missing the header or signature");
  }
  if (header.data) {
    memcpy(tail, header.data, header.len);
    snap->el_header = bytes(tail, header.len);
    tail += header.len;
    snap->newer_header  = feed_header;
    snap->history_proof = history;
    memcpy(snap->el_block_hash, block_hash, 32);
    safe_free(header.data);
  }
  else {
    snap->el_header = feed_header;
    keccak(snap->el_header, snap->el_block_hash);
  }
  if (body.data) {
    memcpy(tail, body.data, body.len);
    snap->el_body = bytes(tail, body.len);
    safe_free(body.data);
  }
  c4_state_cache_set(&ctx->state, id, bytes((uint8_t*) snap, size));
  fill_from_snap(out, snap);
  return C4_SUCCESS;
}

static c4_status_t load_head(prover_ctx_t* ctx, const arb_el_snap_t** head) {
  bytes32_t id = {0};
  latest_id(id);
  bytes_t cached = c4_state_cache_get(&ctx->state, id);
  if (cached.data) {
    *head = (const arb_el_snap_t*) cached.data;
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
  eth_block_t ignored = {0};
  TRY_ASYNC(cache_snap(ctx, id, feed, NULL_BYTES, (json_t) {0}, NULL, NULL_BYTES, &ignored));
  cached = c4_state_cache_get(&ctx->state, id);
  if (!cached.data) THROW_ERROR("arb feed proof was not cached");
  *head = (const arb_el_snap_t*) cached.data;
  return C4_SUCCESS;
}

/**
 * Full RLP item (prefix plus payload) of a payload view returned by `rlp_decode`.
 * The view must be a list or a string that has its own length prefix.
 *
 * @param payload item payload inside an encoded buffer
 * @return the item including its prefix
 */
static bytes_t rlp_with_prefix(bytes_t payload) {
  if (payload.len < 56) return bytes(payload.data - 1, payload.len + 1);
  uint32_t nbytes = 0;
  uint32_t len    = payload.len;
  while (len) {
    nbytes++;
    len >>= 8;
  }
  return bytes(payload.data - (int) nbytes - 1, payload.len + nbytes + 1);
}

/**
 * Loads the execution body of `block_hash` via `debug_getRawBlock`.
 *
 * A block encoded as header, transactions and uncles (no Shanghai withdrawals list)
 * is stored with an empty withdrawals list. The header taken from the raw block
 * must hash to `block_hash`.
 *
 * @param ctx prover context
 * @param block_hash trusted block hash
 * @param body owned `ETH_SSZ_EL_BLOCK_CONTENT` bytes on success
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
static c4_status_t fetch_el_body(prover_ctx_t* ctx, const uint8_t* block_hash, bytes_t* body) {
  bytes_t raw = NULL_BYTES;
  TRY_ASYNC(eth_debug_get_raw_block(ctx, block_hash, &raw));

  buffer_t rebuilt = {0};
  bytes_t  list    = raw;
  bytes_t  source  = raw;
  int      count   = rlp_decode(&list, 0, &list) == RLP_LIST ? (int) rlp_decode(&list, -1, NULL) : 0;
  if (count == 3) {
    bytes_t header = NULL_BYTES;
    bytes_t txs    = NULL_BYTES;
    bytes_t uncles = NULL_BYTES;
    if (rlp_decode(&list, 0, &header) != RLP_LIST || rlp_decode(&list, 1, &txs) != RLP_LIST || rlp_decode(&list, 2, &uncles) != RLP_LIST)
      THROW_ERROR("raw block is not a header, transactions and uncles");
    buffer_append(&rebuilt, rlp_with_prefix(header));
    buffer_append(&rebuilt, rlp_with_prefix(txs));
    buffer_append(&rebuilt, rlp_with_prefix(uncles));
    uint8_t empty_list = 0xc0;
    buffer_append(&rebuilt, bytes(&empty_list, 1));
    rlp_to_list(&rebuilt);
    source = rebuilt.data;
  }

  ssz_builder_t body_builder = ssz_builder_for_type(ETH_SSZ_EL_BLOCK_CONTENT);
  bytes_t       raw_header   = NULL_BYTES;
  // On failure `raw_header` may point into `source`: do not free it.
  if (eth_el_header_get_from_raw_block(&ctx->state, source, &raw_header, &body_builder) != C4_SUCCESS) {
    ssz_builder_free(&body_builder);
    buffer_free(&rebuilt);
    return C4_ERROR;
  }
  bytes32_t got = {0};
  keccak(raw_header, got);
  bool match = memcmp(got, block_hash, 32) == 0;
  safe_free(raw_header.data);
  buffer_free(&rebuilt);
  if (!match) {
    ssz_builder_free(&body_builder);
    THROW_ERROR("raw block does not match the proven block hash");
  }
  *body = ssz_builder_to_bytes(&body_builder).bytes;
  if (!body->data) THROW_ERROR("execution payload is null");
  return C4_SUCCESS;
}

/**
 * Returns the signed head. When `with_body` is set, the execution body of that
 * head is fetched and cached under `id`.
 *
 * @param ctx prover context
 * @param id snapshot key of the request
 * @param head signed head
 * @param with_body include transactions and withdrawals
 * @param out output block
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
static c4_status_t deliver_head(prover_ctx_t* ctx, bytes32_t id, const arb_el_snap_t* head, bool with_body, eth_block_t* out) {
  if (!with_body) {
    fill_from_snap(out, head);
    return C4_SUCCESS;
  }
  bytes_t body = NULL_BYTES;
  TRY_ASYNC(fetch_el_body(ctx, head->el_block_hash, &body));
  return cache_snap(ctx, id, head->feed_proof, NULL_BYTES, (json_t) {0}, NULL, body, out);
}

/**
 * Proves `requested` from the state of the signed head via the EIP-2935 history contract.
 *
 * @param ctx prover context
 * @param id snapshot key of the request
 * @param head signed head
 * @param head_number L2 block number of `head`
 * @param requested L2 block number to prove
 * @param by_hash true when the caller named a block hash
 * @param want_hash that hash, ignored unless `by_hash`
 * @param with_body include transactions and withdrawals of `requested`
 * @param out output block
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
static c4_status_t prove_older(prover_ctx_t* ctx, bytes32_t id, const arb_el_snap_t* head, uint64_t head_number, uint64_t requested, bool by_hash, bytes32_t want_hash, bool with_body, eth_block_t* out) {
  uint64_t window = eth_eip2935_history_window(ctx->chain_id);
  if (requested > head_number)
    THROW_ERROR_WITH("block %l is newer than the signed head %l", requested, head_number);
  if (head_number - requested > window)
    THROW_ERROR_WITH("block %l is outside the EIP-2935 history window of the signed head %l", requested, head_number);

  json_t    history = {0};
  bytes32_t stored  = {0};
  TRY_ASYNC(c4_eth_get_eip2935_proof(ctx, requested, head_number, &history, stored));
  if (bytes_all_zero(bytes(stored, 32)))
    THROW_ERROR_WITH("the EIP-2935 history contract holds no hash for block %l", requested);
  if (by_hash && memcmp(stored, want_hash, 32) != 0)
    THROW_ERROR("the requested block hash is not the one stored in the EIP-2935 history contract");

  char tag[JSON_BYTES32_SIZE] = {0};
  sbprintf(tag, "\"0x%x\"", bytes(stored, 32));
  json_t tag_json = {.type = JSON_TYPE_STRING, .start = tag, .len = 68};
  json_t block    = {0};
  TRY_ASYNC(eth_get_block(ctx, tag_json, false, &block));
  if (block.type != JSON_TYPE_OBJECT) THROW_ERROR_WITH("block %l was not found", requested);

  bytes_t     header = NULL_BYTES;
  c4_status_t built  = eth_el_header_build_from_json(&ctx->state, &header, header_fork(block), block);
  if (built != C4_SUCCESS) {
    safe_free(header.data);
    return built;
  }
  bytes32_t got = {0};
  keccak(header, got);
  if (memcmp(got, stored, 32) != 0 || eth_el_header_get_uint64(header, EL_BLOCK_NUMBER) != requested) {
    safe_free(header.data);
    THROW_ERROR_WITH("the execution header of block %l does not match the EIP-2935 history contract", requested);
  }
  bytes_t body = NULL_BYTES;
  if (with_body) {
    c4_status_t fetched = fetch_el_body(ctx, stored, &body);
    if (fetched != C4_SUCCESS) {
      safe_free(header.data);
      return fetched;
    }
  }
  return cache_snap(ctx, id, head->feed_proof, header, history, stored, body, out);
}

c4_status_t arb_get_el_block(prover_ctx_t* ctx, json_t block, eth_block_t* out, bool with_body) {
  // A hash is 32 bytes. A block number is 1..16 hex digits; anything else that
  // starts with 0x (`"0x"`, `"0xzz"`, more than 16 nibbles) is not a block id.
  // `json_as_uint64` would turn those into 0.
  bool by_hash = is_quoted_hex(block, 64);
  int  digits  = block.type == JSON_TYPE_STRING && block.len > 4 ? block.len - 4 : 0;
  bool by_num  = !by_hash && digits >= 1 && digits <= 16 && is_quoted_hex(block, digits);
  if (!is_latest(block) && !by_hash && !by_num)
    THROW_ERROR("only latest, a block number or a block hash is supported");

  bytes32_t id = {0};
  snap_id(block, with_body, id);
  bytes_t existing = c4_state_cache_get(&ctx->state, id);
  if (existing.data) {
    fill_from_snap(out, (const arb_el_snap_t*) existing.data);
    return C4_SUCCESS;
  }

  const arb_el_snap_t* head = NULL;
  TRY_ASYNC(load_head(ctx, &head));

  uint64_t  head_number = eth_el_header_get_uint64(head->el_header, EL_BLOCK_NUMBER);
  bytes32_t want        = {0};
  uint64_t  requested   = 0;
  if (is_latest(block))
    return deliver_head(ctx, id, head, with_body, out);
  if (by_hash) {
    if (!json_as_bytes32(block, want)) THROW_ERROR("invalid block hash");
    if (memcmp(want, head->el_block_hash, 32) == 0)
      return deliver_head(ctx, id, head, with_body, out);
    json_t found = {0};
    TRY_ASYNC(eth_get_block(ctx, block, false, &found));
    if (found.type != JSON_TYPE_OBJECT) THROW_ERROR("block not found");
    requested = json_get_uint64(found, "number");
    if (requested == head_number) THROW_ERROR("block hash is not the signed head");
  }
  else {
    requested = json_as_uint64(block);
    if (requested == head_number)
      return deliver_head(ctx, id, head, with_body, out);
  }
  return prove_older(ctx, id, head, head_number, requested, by_hash, want, with_body, out);
}

bool arb_add_feed_proof(prover_ctx_t* ctx, ssz_builder_t* builder, eth_block_t* block_data, blockroot_proof_t* historic) {
  (void) ctx;
  (void) historic;
  const ssz_def_t* def = eth_ssz_verification_type(ETH_SSZ_ARB_FEED_PROOF);
  if (block_data->proof_type != C4_BLOCK_PROOF_TYPE_SEQUENCER || !block_data->sequencer.payload.data || !def)
    return false;
  if (block_data->sequencer.history.newer_header.data) {
    if (block_data->sequencer.history.history_proof.type != JSON_TYPE_OBJECT) return false;
    ssz_builder_t newer = ssz_builder_for_type(ETH_SSZ_ARB_FEED_PROOF);
    buffer_append(&newer.fixed, block_data->sequencer.payload);
    c4_eth_add_eip2935_proof(builder, block_data->el_header, block_data->sequencer.history.history_proof, newer);
    return true;
  }
  ssz_ob_t feed = {.def = def, .bytes = block_data->sequencer.payload};
  ssz_add_ob(builder, "elProof", feed);
  return true;
}

void arb_register_block_proof_prover(void) {
  c4_register_block_proof_prover(C4_CHAIN_TYPE_ARBITRUM, arb_add_feed_proof, arb_get_el_block);
}
