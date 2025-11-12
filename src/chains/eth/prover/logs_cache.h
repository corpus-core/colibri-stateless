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

void c4_eth_logs_cache_enable(uint32_t max_blocks);
void c4_eth_logs_cache_disable(void);
bool c4_eth_logs_cache_is_enabled(void);

// Add a fully built block to the cache. Requires contiguous block_number.
// logs_bloom must be 256 bytes.
void c4_eth_logs_cache_add_block(uint64_t block_number, const uint8_t* logs_bloom, json_t receipts_array);

// Check if the cache has a contiguous range [from_block, to_block] (inclusive)
bool c4_eth_logs_cache_has_range(uint64_t from_block, uint64_t to_block);

// Attempt to serve eth_getLogs from cache. On success (served_from_cache=true),
// out_logs will contain a JSON array equivalent to RPC result.
// Returns C4_SUCCESS on synchronous success, C4_PENDING if async requests were scheduled,
// or C4_ERROR on failure.
c4_status_t c4_eth_logs_cache_scan(prover_ctx_t* ctx, json_t filter, json_t* out_logs, bool* served_from_cache);

// Stats for Prometheus
void     c4_eth_logs_cache_stats(uint64_t* blocks, uint64_t* txs, uint64_t* events);
void     c4_eth_logs_cache_counters(uint64_t* hits, uint64_t* misses, uint64_t* bloom_skips);
uint64_t c4_eth_logs_cache_first_block(void);
uint64_t c4_eth_logs_cache_last_block(void);
uint32_t c4_eth_logs_cache_capacity_blocks(void);

#endif // PROVER_CACHE

#ifdef __cplusplus
}
#endif

#endif
