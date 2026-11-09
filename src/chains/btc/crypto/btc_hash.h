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

#ifndef btc_hash_h__
#define btc_hash_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "bytes.h"

/** Serialized Bitcoin block header size, in bytes. */
#define BTC_HEADER_SIZE 80u
/** Offset of the little-endian version word. */
#define BTC_HEADER_VERSION 0u
/** Offset of the previous-block hash in internal byte order. */
#define BTC_HEADER_PARENT 4u
/** Offset of the transaction merkle root in internal byte order. */
#define BTC_HEADER_MERKLE 36u
/** Offset of the little-endian unix timestamp. */
#define BTC_HEADER_TIME 68u
/** Offset of the little-endian compact target (`nBits`). */
#define BTC_HEADER_BITS 72u
/** Offset of the little-endian nonce. */
#define BTC_HEADER_NONCE 76u

/**
 * Writes `SHA256(SHA256(data))` to `out`.
 *
 * This is Bitcoin's HASH256. It is not `sha256_merkle`, which hashes the concatenation only once.
 * `data.data` may be NULL only when `data.len` is 0.
 *
 * @param data bytes to hash
 * @param out 32-byte internal-order digest
 */
void btc_hash256(bytes_t data, uint8_t out[32]) NONNULL_FOR((2));

/**
 * Writes HASH256 of the concatenation of `parts` to `out`.
 *
 * Empty parts are skipped. A part with `len > 0` must have a non-NULL `data` pointer.
 *
 * @param parts array of slices, in order
 * @param count number of slices
 * @param out 32-byte internal-order digest
 * @return true when the digest was written
 */
bool btc_hash256_parts(const bytes_t* parts, size_t count, uint8_t out[32]) NONNULL_FOR((3));

/**
 * Writes HASH256 of the 64-byte concatenation `left || right`.
 *
 * `left`, `right`, and `out` may alias.
 *
 * @param left first 32-byte hash
 * @param right second 32-byte hash
 * @param out 32-byte parent hash
 */
void btc_hash256_pair(const uint8_t left[32], const uint8_t right[32], uint8_t out[32]) NONNULL;

/**
 * Reverses a 32-byte hash between internal order and Bitcoin Core display order.
 *
 * `in` and `out` may be the same buffer.
 *
 * @param in source hash
 * @param out reversed hash
 */
void btc_hash_reverse(const uint8_t in[32], uint8_t out[32]) NONNULL;

/**
 * Compares two internal-order hashes as Bitcoin 256-bit little-endian integers.
 *
 * @param a first hash
 * @param b second hash
 * @return negative if `a < b`, zero if equal, positive if `a > b`
 */
int btc_hash_cmp(const uint8_t a[32], const uint8_t b[32]) NONNULL;

/**
 * Decodes a compact `nBits` target into a 32-byte little-endian integer.
 *
 * Rejects a negative mantissa, an overflow past 256 bits, and a zero target.
 * The network pow-limit is not applied, so a regtest target such as `0x207fffff` still decodes.
 * On failure `out` is cleared.
 *
 * @param bits compact target from a block header
 * @param out 32-byte little-endian target
 * @return true when `out` holds a non-zero in-range target
 */
bool btc_target_from_bits(uint32_t bits, uint8_t out[32]) NONNULL_FOR((2));

/**
 * Checks that an 80-byte header meets its own compact target.
 *
 * The header hash is `btc_hash256` of the 80 bytes. Proof-of-work holds when that hash, read as a
 * little-endian integer, is less than or equal to the decoded `nBits` target. A passing header is
 * not evidence that the header is on the main chain: difficulty binding against a checkpoint or a
 * cached epoch target is a separate step, and this function does not enforce a network pow-limit.
 *
 * @param header exactly 80 header bytes
 * @param hash_out internal-order header hash; required even when the caller only needs the boolean
 * @return true when the header has a legal target and its hash meets that target
 */
bool btc_header_check(bytes_t header, uint8_t hash_out[32]) NONNULL_FOR((2));

/**
 * Checks a concatenated sequence of 80-byte headers.
 *
 * Each header must pass `btc_header_check`. From the second header on, its parent field must be
 * the previous header's internal-order hash. Timestamps and epoch retargets are not checked here.
 *
 * @param headers one or more headers, length a positive multiple of 80
 * @param first_hash internal-order hash of the first header, or NULL
 * @param last_hash internal-order hash of the last header, or NULL; may alias `first_hash`, in which case the last hash is written
 * @return true when every header meets its target and each parent link matches
 */
bool btc_check_header_chain(bytes_t headers, uint8_t* first_hash, uint8_t* last_hash);

#ifdef __cplusplus
}
#endif

#endif
