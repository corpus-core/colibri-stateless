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

#ifndef C4_PROOF_TRANSACTION_CACHE_H
#define C4_PROOF_TRANSACTION_CACHE_H

#include "crypto.h" // bytes32_t
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Transaction cache for mapping a transaction hash (Keccak-256, 32 bytes)
 * to its block number and transaction index within that block.
 *
 * The cache is optimized for fast lookups and block-wise FIFO eviction.
 */

/**
 * Inserts or updates a cache entry for a transaction.
 *
 * @param tx_hash 32-byte transaction hash (Keccak-256)
 * @param block_number Block number the transaction belongs to
 * @param tx_index Transaction index within the block
 */
void c4_eth_tx_cache_set(bytes32_t tx_hash, uint64_t block_number, uint32_t tx_index);

/**
 * Looks up a transaction in the cache.
 *
 * @param tx_hash 32-byte transaction hash (Keccak-256)
 * @param block_number Output for block number (nullable)
 * @param tx_index Output for transaction index (nullable)
 * @return true if found, false otherwise
 */
bool c4_eth_tx_cache_get(bytes32_t tx_hash, uint64_t* block_number, uint32_t* tx_index);

/**
 * Resets the entire transaction cache (clears all entries).
 */
void c4_eth_tx_cache_reset(void);

/**
 * Returns the current number of entries in the cache.
 *
 * @return Number of cached transactions
 */
/**
 * Returns the current number of entries in the cache.
 *
 * @return Number of cached transactions
 */
size_t c4_eth_tx_cache_size(void);

/**
 * Ensures capacity by evicting old blocks if needed for batch inserts.
 * Call this before inserting a block's transactions to avoid per-insert eviction.
 *
 * @param number_of_entries_to_add Number of entries that will be inserted next
 */
void c4_eth_tx_cache_reserve(uint32_t number_of_entries_to_add);

/**
 * Sets the maximum number of entries the cache will store. Evicts old blocks
 * immediately if the current size exceeds the new limit.
 *
 * @param max The new maximum number of entries
 */
void c4_eth_tx_cache_set_max_size(uint32_t max);

/**
 * Gets the configured maximum number of entries for the cache.
 *
 * @return Current maximum capacity
 */
size_t c4_eth_tx_cache_capacity(void);

#ifdef __cplusplus
}
#endif

#endif // C4_PROOF_TRANSACTION_CACHE_H
