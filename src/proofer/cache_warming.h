/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef CACHE_WARMING_H
#define CACHE_WARMING_H

#include "cache_keys.h"
#include "proofer.h"
#include <stdint.h>

// Cache warming statistics
typedef struct {
  uint64_t requests_warmed;
  uint64_t cache_hits_from_warming;
  uint64_t warming_time_saved_ms;
  uint64_t last_warming_time;
} cache_warming_stats_t;

// Request pattern tracking for intelligent warming
typedef struct {
  bytes32_t cache_key;
  uint32_t  request_count;
  uint64_t  last_requested;
  uint64_t  avg_interval_ms;
  uint32_t  priority_score;
} request_pattern_t;

/**
 * Initialize cache warming system
 */
void c4_cache_warming_init(void);

/**
 * Shutdown cache warming system
 */
void c4_cache_warming_shutdown(void);

/**
 * Record a cache access pattern for future warming
 * @param ctx Proofer context
 * @param key Cache key that was accessed
 * @param was_hit Whether this was a cache hit or miss
 */
void c4_cache_warming_record_access(proofer_ctx_t* ctx, bytes32_t key, bool was_hit);

/**
 * Trigger proactive cache warming based on access patterns
 * @param ctx Proofer context
 * @param chain_id Chain to warm cache for
 */
void c4_cache_warming_trigger(proofer_ctx_t* ctx, chain_id_t chain_id);

/**
 * Warm cache for upcoming beacon slots
 * @param ctx Proofer context
 * @param current_slot Current beacon slot
 * @param slots_ahead Number of slots to warm ahead
 */
void c4_cache_warm_beacon_slots(proofer_ctx_t* ctx, uint64_t current_slot, uint32_t slots_ahead);

/**
 * Warm cache for recent block receipts/logs
 * @param ctx Proofer context
 * @param latest_block Latest known block number
 * @param blocks_back Number of recent blocks to warm
 */
void c4_cache_warm_recent_blocks(proofer_ctx_t* ctx, uint64_t latest_block, uint32_t blocks_back);

/**
 * Get cache warming statistics
 * @return Pointer to warming statistics
 */
const cache_warming_stats_t* c4_cache_warming_get_stats(void);

/**
 * Predictive cache warming based on time patterns
 * @param ctx Proofer context
 * @param current_time Current timestamp
 */
void c4_cache_warming_predictive(proofer_ctx_t* ctx, uint64_t current_time);

#endif // CACHE_WARMING_H


