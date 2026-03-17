/*
 * Copyright (c) 2025,2026 corpus.core
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

#ifndef pap_tx_cache_h__
#define pap_tx_cache_h__

#ifdef PAP

#include "chains.h"
#include "crypto.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Client-side PAP transaction cache.
 *
 * Maps `txHash -> (blockNumber, txIndex)` per chain, backed by the
 * `storage_plugin_t` for persistence across sessions. The storage format
 * is the same SSZ `TxCacheSnapshot` served by the prover's `/tx_cache`
 * endpoint, keyed as `"tx_cache_<chain_id>"`.
 *
 * Also manages a persistent *pending transaction list* per chain
 * (key `"tx_pending_<chain_id>"`). Each entry carries a timestamp;
 * entries older than `PAP_PENDING_TX_TTL_S` are pruned automatically.
 */

/**
 * Maximum size of an SSZ tx cache snapshot accepted from the server.
 */
#define PAP_TX_CACHE_MAX_SSZ_SIZE (10u * 1024u * 1024u)

/**
 * Time-to-live for pending transaction entries (in seconds).
 * Entries older than this are silently discarded on load.
 */
#define PAP_PENDING_TX_TTL_S 3600

/**
 * Default maximum number of blocks to request from the server
 * in a single `/tx_cache` fetch.
 */
#define PAP_TX_CACHE_MAX_BLOCKS 256

/**
 * Attempts to load the tx cache for `chain_id` from storage.
 *
 * @param chain_id target chain
 * @return true if cache was loaded and contains data
 */
bool pap_tx_cache_load(chain_id_t chain_id);

/**
 * Populates the in-memory cache from an SSZ `TxCacheSnapshot` blob
 * (as received from the prover `/tx_cache` endpoint) and persists
 * the data to storage under `"tx_cache_<chain_id>"`.
 *
 * @param chain_id target chain
 * @param ssz_data raw SSZ bytes of the TxCacheSnapshot
 */
void pap_tx_cache_populate_from_ssz(chain_id_t chain_id, bytes_t ssz_data);

/**
 * Merges an incremental SSZ `TxCacheSnapshot` blob into the existing
 * in-memory cache. New blocks are appended; blocks already present
 * (same `block_number`) are replaced. Falls back to
 * `pap_tx_cache_populate_from_ssz` when no cache exists yet.
 *
 * @param chain_id target chain
 * @param ssz_data raw SSZ bytes of the (partial) TxCacheSnapshot
 */
void pap_tx_cache_merge_from_ssz(chain_id_t chain_id, bytes_t ssz_data);

/**
 * Returns the highest block number in the loaded cache, or 0 if
 * no cache is loaded for `chain_id`.
 *
 * @param chain_id target chain
 * @return highest cached block number
 */
uint64_t pap_tx_cache_max_block(chain_id_t chain_id);

/**
 * Returns the Unix timestamp (seconds) of the last successful
 * server fetch for `chain_id`, or 0 if never fetched this session.
 *
 * @param chain_id target chain
 * @return timestamp of last server update
 */
uint64_t pap_tx_cache_last_updated(chain_id_t chain_id);

/**
 * Looks up a transaction hash in the cache.
 *
 * @param chain_id target chain
 * @param tx_hash 32-byte keccak hash to look up
 * @param block_number output: block number (set on success, may be NULL)
 * @param tx_index output: tx index within block (set on success, may be NULL)
 * @return true if found
 */
bool pap_tx_cache_get(chain_id_t chain_id, bytes32_t tx_hash,
                      uint64_t* block_number, uint32_t* tx_index);

/**
 * Returns true if the cache for `chain_id` is loaded in memory.
 *
 * @param chain_id target chain
 */
bool pap_tx_cache_is_loaded(chain_id_t chain_id);

/**
 * Resets the in-memory PAP tx cache (for test isolation).
 */
void pap_tx_cache_reset(void);

/**
 * Adds a transaction hash to the persistent pending list.
 *
 * The hash is stored with `time(NULL)` as timestamp. If the hash
 * already exists the entry is refreshed (timestamp updated).
 * Expired entries (older than `PAP_PENDING_TX_TTL_S`) are pruned.
 *
 * @param chain_id target chain
 * @param tx_hash 32-byte transaction hash
 */
void pap_tx_cache_add_pending(chain_id_t chain_id, bytes32_t tx_hash);

/**
 * Returns true if `tx_hash` is in the pending list and not expired.
 *
 * @param chain_id target chain
 * @param tx_hash 32-byte transaction hash
 * @return true if pending
 */
bool pap_tx_cache_is_pending(chain_id_t chain_id, bytes32_t tx_hash);

/**
 * Removes a transaction hash from the persistent pending list.
 *
 * No-op if the hash is not present.
 *
 * @param chain_id target chain
 * @param tx_hash 32-byte transaction hash
 */
void pap_tx_cache_remove_pending(chain_id_t chain_id, bytes32_t tx_hash);

#ifdef __cplusplus
}
#endif

#endif /* PAP */
#endif /* pap_tx_cache_h__ */
