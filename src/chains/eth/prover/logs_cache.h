/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */
#ifndef C4_ETH_LOGS_CACHE_H
#define C4_ETH_LOGS_CACHE_H

#include "json.h"
#include "prover.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef PROVER_CACHE

/**
 * Enables the in-memory logs cache and sets the maximum number of contiguous blocks retained.
 *
 * @param max_blocks capacity in blocks (FIFO eviction when exceeded)
 */
void c4_eth_logs_cache_enable(uint32_t max_blocks);

/** Disables the logs cache and frees cached blocks. */
void c4_eth_logs_cache_disable(void);

/**
 * @return true if the logs cache is enabled
 */
bool c4_eth_logs_cache_is_enabled(void);

/**
 * Adds a fully built block to the cache. Requires contiguous `block_number`.
 *
 * @param block_number execution block number
 * @param logs_bloom 256-byte logs bloom from the block header
 * @param receipts_array JSON receipt array for the block (used to rebuild matching logs)
 */
void c4_eth_logs_cache_add_block(uint64_t block_number, const uint8_t* logs_bloom, json_t receipts_array);

/**
 * @param from_block inclusive lower block bound
 * @param to_block inclusive upper block bound
 * @return true if the cache holds every block in `[from_block, to_block]`
 */
bool c4_eth_logs_cache_has_range(uint64_t from_block, uint64_t to_block);

/**
 * Attempts to serve `eth_getLogs` from cache.
 *
 * On success (`*served_from_cache == true`), `out_logs` contains a JSON array
 * equivalent to the RPC result. Returns `C4_SUCCESS` on synchronous success,
 * `C4_PENDING` if async requests were scheduled, or `C4_ERROR` on failure.
 *
 * @param ctx prover context
 * @param filter `eth_getLogs` filter JSON
 * @param out_logs receives log array when served from cache
 * @param served_from_cache set to true when the filter was fully satisfied from cache
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
c4_status_t c4_eth_logs_cache_scan(prover_ctx_t* ctx, json_t filter, json_t* out_logs, bool* served_from_cache);

/**
 * Returns aggregate cache size metrics for Prometheus export.
 *
 * @param blocks number of cached blocks
 * @param txs total cached transactions across blocks
 * @param events total log events stored
 */
void c4_eth_logs_cache_stats(uint64_t* blocks, uint64_t* txs, uint64_t* events);

/**
 * Returns cache hit/miss counters.
 *
 * @param hits successful cache serves
 * @param misses filter ranges not fully cached
 * @param bloom_skips blocks skipped due to bloom mismatch
 */
void c4_eth_logs_cache_counters(uint64_t* hits, uint64_t* misses, uint64_t* bloom_skips);

/** @return lowest cached block number, or `0` if empty */
uint64_t c4_eth_logs_cache_first_block(void);

/** @return highest cached block number, or `0` if empty */
uint64_t c4_eth_logs_cache_last_block(void);

/** @return configured maximum number of blocks the cache may hold */
uint32_t c4_eth_logs_cache_capacity_blocks(void);

#endif // PROVER_CACHE

#ifdef __cplusplus
}
#endif

#endif
