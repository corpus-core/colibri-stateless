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

#ifndef btc_merkle_h__
#define btc_merkle_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "bytes.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * Upper bound for `btc_merkle_root`.
 *
 * A consensus block is far smaller than this. The cap keeps a leaf list from forcing a huge allocation.
 */
#define BTC_MERKLE_MAX_LEAVES 1000000u

/**
 * Verifies a transaction's inclusion in a Bitcoin merkle root.
 *
 * `proof` is a sequence of 32-byte sibling hashes in internal order, one per level, from the leaf
 * up to the root. The walk always consumes the whole proof: an intermediate hash that already
 * equals `root` is not accepted. `tx_count` is required so the odd-node padding duplicate can be
 * told apart from a duplicated transaction (CVE-2012-2459, `bad-txns-duplicate`).
 *
 * When the current node is the last node of an odd level, the sibling must be a copy of the
 * current hash. That copy is legal only in that position. Every other identical adjacent pair is rejected.
 * A duplicate that is not on this path is invisible to the walk; `btc_merkle_root` is what reports it.
 *
 * @param leaf internal-order txid or wtxid
 * @param index leaf index in the block, starting at 0
 * @param tx_count number of leaves in the tree, at least 1
 * @param proof concatenated sibling hashes, length a multiple of 32
 * @param root expected merkle root in internal order
 * @return true when the leaf is at `index` in a tree of `tx_count` leaves with that root
 */
bool btc_merkle_verify(const uint8_t leaf[32], uint32_t index, uint32_t tx_count, bytes_t proof, const uint8_t root[32]);

/**
 * Computes the merkle root of internal-order leaves.
 *
 * The last node of an odd level is duplicated, matching Bitcoin. If `mutated` is non-NULL it is set
 * when any real adjacent pair is identical. Padding is not counted. A mutated root must not be used
 * as an inclusion commitment. This function allocates and is meant for a trusted leaf list, not for
 * untrusted proof bytes (`btc_merkle_verify` does not allocate).
 *
 * @param leaves `count` concatenated 32-byte hashes
 * @param count number of leaves, from 1 to `BTC_MERKLE_MAX_LEAVES`
 * @param root receives the merkle root
 * @param mutated receives whether a duplicate pair was present; may be NULL
 * @return true when `root` was written
 */
bool btc_merkle_root(const uint8_t* leaves, uint32_t count, uint8_t root[32], bool* mutated);

#ifdef __cplusplus
}
#endif

#endif
