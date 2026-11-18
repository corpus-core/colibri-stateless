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

#include "arb_verify.h"
#include "beacon_types.h"
#include "crypto.h"
#include "el_header.h"
#include "header_cache.h"
#include "plugin.h"
#include "ssz.h"
#include <string.h>
#include <zstd.h>

// Nitro genesis of Arbitrum One. Feed sequence = L2 block number - this value.
#define ARB_ONE_NITRO_GENESIS 22207817ull
// Fixed fields after an empty blockMetadata: delayedMessagesRead, kind, poster, l1BlockNumber, timestamp.
#define ARB_FEED_TAIL_MIN 45u
// Same cap OP uses before inflating a sequencer payload.
#define ARB_FEED_TAIL_MAX (32u * 1024u * 1024u)

static const char ARB_FEED_PREFIX[] = "Arbitrum Nitro Feed:";
_Static_assert(sizeof(ARB_FEED_PREFIX) == 21, "prefix is 20 bytes plus the terminator");

/**
 * Recovers the 20-byte signer of a secp256k1 signature over `digest`.
 *
 * `v` may be 0/1 or 27/28. There is no EIP-191 prefix.
 *
 * @param digest 32-byte keccak of the feed preimage
 * @param signature 65 bytes `r || s || v`
 * @param address recovered signer
 * @return `true` when recovery succeeds
 */
static bool recover_signer(bytes32_t digest, bytes_t signature, address_t address) {
  uint8_t pub[64]  = {0};
  uint8_t hash[32] = {0};
  if (!secp256k1_recover(digest, signature, pub)) return false;
  keccak(bytes(pub, 64), hash);
  memcpy(address, hash + 12, 20);
  return true;
}

bool arb_feed_recover_signer(uint64_t chain_id, uint64_t sequence, bytes32_t block_hash, bytes_t signed_tail, bytes_t signature, uint8_t address[20]) {
  bytes32_t digest = {0};
  if (!address || signature.len != 65) return false;
  arb_feed_digest(chain_id, sequence, block_hash, signed_tail, digest);
  return recover_signer(digest, signature, address);
}

/**
 * Inflates a ZSTD `signedTail`. The frame must carry its content size, and that
 * size must sit inside the tail cap, so the output length is known before allocation.
 *
 * @param state error sink
 * @param compressed ZSTD frame
 * @param out owned uncompressed bytes on success
 * @return `C4_SUCCESS` or `C4_ERROR`
 */
static c4_status_t decompress_tail(c4_state_t* state, bytes_t compressed, bytes_t* out) {
  unsigned long long expected = ZSTD_getFrameContentSize(compressed.data, compressed.len);
  if (expected == ZSTD_CONTENTSIZE_ERROR || expected == ZSTD_CONTENTSIZE_UNKNOWN ||
      expected == 0 || expected > ARB_FEED_TAIL_MAX) {
    c4_state_add_error(state, "failed to decompress signed tail");
    return C4_ERROR;
  }
  uint8_t* buf    = safe_malloc((size_t) expected);
  size_t   actual = ZSTD_decompress(buf, (size_t) expected, compressed.data, compressed.len);
  if (ZSTD_isError(actual) || actual != expected) {
    safe_free(buf);
    c4_state_add_error(state, "failed to decompress signed tail");
    return C4_ERROR;
  }
  *out = bytes(buf, (uint32_t) actual);
  return C4_SUCCESS;
}

void arb_feed_digest(uint64_t chain_id, uint64_t sequence, bytes32_t block_hash, bytes_t signed_tail, bytes32_t out) {
  buffer_t msg = {0};
  uint8_t  be[8];
  buffer_append(&msg, bytes((uint8_t*) ARB_FEED_PREFIX, sizeof(ARB_FEED_PREFIX) - 1));
  uint64_to_be(be, chain_id);
  buffer_append(&msg, bytes(be, 8));
  uint64_to_be(be, sequence);
  buffer_append(&msg, bytes(be, 8));
  buffer_append(&msg, bytes(block_hash, 32));
  buffer_append(&msg, signed_tail);
  keccak(msg.data, out);
  buffer_free(&msg);
}

c4_status_t arb_verify_feed_proof(verify_ctx_t* ctx, ssz_ob_t block, bytes_t* el_header, bytes32_t block_hash) {
  if (!block.def || strcmp(block.def->name, ETH_SSZ_NAME_ARB_FEED_PROOF) != 0)
    THROW_ERROR("invalid block type!");
  // `c4_verify_block` only calls this hook for an Arbitrum chain. The same gate
  // covers a direct call. The digest then uses the request's own chain id.
  if (c4_chain_type(ctx->chain_id) != C4_CHAIN_TYPE_ARBITRUM)
    THROW_ERROR("chain not supported");

  bytes_t header = ssz_get(&block, "elHeader").bytes;
  if (!eth_el_header_is_valid(header))
    THROW_ERROR("arb feed header is not an execution header");
  bytes_t number = eth_el_header_get(header, EL_BLOCK_NUMBER);
  if (number.len == 0 || number.len > 8)
    THROW_ERROR("invalid block number");
  uint64_t block_number = bytes_as_be(number);
  if (block_number < ARB_ONE_NITRO_GENESIS)
    THROW_ERROR("block is before the nitro genesis");

  bytes_t  signature = ssz_get(&block, "signature").bytes;
  ssz_ob_t tail_ob   = ssz_get(&block, "signedTail");
  if (signature.len != 65 || !tail_ob.def || !tail_ob.def->name)
    THROW_ERROR("invalid sequencer proof!");

  bool compressed = strcmp(tail_ob.def->name, ETH_SSZ_NAME_UNCOMPRESSED) != 0;
  if (compressed && strcmp(tail_ob.def->name, ETH_SSZ_NAME_COMPRESSED_ZSTD) != 0)
    THROW_ERROR("invalid sequencer proof!");
  if (!compressed && tail_ob.bytes.len > ARB_FEED_TAIL_MAX)
    THROW_ERROR("arb feed tail too large");

  bytes_t tail  = tail_ob.bytes;
  bool    owned = false;
  if (compressed) {
    TRY_ASYNC(decompress_tail(&ctx->state, tail_ob.bytes, &tail));
    owned = true;
  }
  if (tail.len < ARB_FEED_TAIL_MIN) {
    if (owned) safe_free(tail.data);
    THROW_ERROR("signed tail too short");
  }

  bytes32_t hash   = {0};
  bytes32_t digest = {0};
  keccak(header, hash);
  arb_feed_digest(ctx->chain_id, block_number - ARB_ONE_NITRO_GENESIS, hash, tail, digest);
  if (owned) safe_free(tail.data);

  address_t signer    = {0};
  uint8_t   trusted[20] = {0};
  uint64_t  timestamp   = 0;
  uint64_t  l1_number   = 0;
  if (!c4_get_cached_batch_poster(ctx->chain_id, trusted, &timestamp, &l1_number))
    THROW_ERROR("unknown batch poster");
  if (!recover_signer(digest, signature, signer) || memcmp(signer, trusted, 20) != 0)
    THROW_ERROR("invalid sequencer signature");

  memcpy(block_hash, hash, 32);
#ifdef EL_HEADER_CACHE
  c4_header_cache_put(ctx->chain_id, block_number, block_hash, header, NULL);
#endif
  *el_header = header;
  return C4_SUCCESS;
}

void arb_register_block_proof_verify(void) {
  c4_register_block_proof_verify(C4_CHAIN_TYPE_ARBITRUM, arb_verify_feed_proof);
}
