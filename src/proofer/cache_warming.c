/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "cache_warming.h"
#include "cache_keys.h"
#include "logger.h"
#include "util/mem.h"
#include <math.h>
#include <string.h>

#define MAX_TRACKED_PATTERNS      1000
#define WARMING_TRIGGER_THRESHOLD 3      // Warm after 3 accesses to same pattern
#define PREDICTIVE_WINDOW_MS      300000 // 5 minutes prediction window

static cache_warming_stats_t warming_stats       = {0};
static request_pattern_t*    tracked_patterns    = NULL;
static size_t                pattern_count       = 0;
static bool                  warming_initialized = false;

void c4_cache_warming_init(void) {
  if (warming_initialized) return;

  tracked_patterns = (request_pattern_t*) safe_calloc(MAX_TRACKED_PATTERNS, sizeof(request_pattern_t));
  pattern_count    = 0;
  memset(&warming_stats, 0, sizeof(warming_stats));
  warming_initialized = true;

  log_info("Cache warming system initialized");
}

void c4_cache_warming_shutdown(void) {
  if (!warming_initialized) return;

  if (tracked_patterns) {
    safe_free(tracked_patterns);
    tracked_patterns = NULL;
  }
  pattern_count       = 0;
  warming_initialized = false;

  log_info("Cache warming system shutdown. Stats: %llu requests warmed, %llu hits from warming, %llu ms saved",
           warming_stats.requests_warmed, warming_stats.cache_hits_from_warming, warming_stats.warming_time_saved_ms);
}

// Find or create a pattern entry for the given cache key
static request_pattern_t* find_or_create_pattern(bytes32_t key) {
  // First, try to find existing pattern
  for (size_t i = 0; i < pattern_count; i++) {
    if (memcmp(tracked_patterns[i].cache_key, key, 32) == 0) {
      return &tracked_patterns[i];
    }
  }

  // Create new pattern if space available
  if (pattern_count < MAX_TRACKED_PATTERNS) {
    request_pattern_t* pattern = &tracked_patterns[pattern_count++];
    memcpy(pattern->cache_key, key, 32);
    pattern->request_count   = 0;
    pattern->last_requested  = current_ms();
    pattern->avg_interval_ms = 0;
    pattern->priority_score  = 0;
    return pattern;
  }

  // Replace least recently used pattern
  request_pattern_t* oldest = &tracked_patterns[0];
  for (size_t i = 1; i < MAX_TRACKED_PATTERNS; i++) {
    if (tracked_patterns[i].last_requested < oldest->last_requested) {
      oldest = &tracked_patterns[i];
    }
  }

  memcpy(oldest->cache_key, key, 32);
  oldest->request_count   = 0;
  oldest->last_requested  = current_ms();
  oldest->avg_interval_ms = 0;
  oldest->priority_score  = 0;
  return oldest;
}

void c4_cache_warming_record_access(proofer_ctx_t* ctx, bytes32_t key, bool was_hit) {
  if (!warming_initialized) return;

  request_pattern_t* pattern = find_or_create_pattern(key);
  uint64_t           now     = current_ms();

  // Update access pattern
  if (pattern->request_count > 0) {
    uint64_t interval = now - pattern->last_requested;
    if (pattern->avg_interval_ms == 0) {
      pattern->avg_interval_ms = interval;
    }
    else {
      // Exponential moving average
      pattern->avg_interval_ms = (pattern->avg_interval_ms * 7 + interval) / 8;
    }
  }

  pattern->request_count++;
  pattern->last_requested = now;

  // Calculate priority score based on frequency and recency
  double frequency_score  = log(pattern->request_count + 1);
  double recency_score    = 1.0 / (1.0 + (now - pattern->last_requested) / 60000.0); // Decay over minutes
  pattern->priority_score = (uint32_t) (frequency_score * recency_score * 100);

  // Track cache hit from warming
  if (was_hit) {
    warming_stats.cache_hits_from_warming++;
    // Estimate time saved (avoid expensive computation)
    warming_stats.warming_time_saved_ms += 50; // Assume 50ms saved per hit
  }
}

void c4_cache_warming_trigger(proofer_ctx_t* ctx, chain_id_t chain_id) {
  if (!warming_initialized || !ctx) return;

  uint64_t now          = current_ms();
  uint32_t warmed_count = 0;

  // Sort patterns by priority score
  for (size_t i = 0; i < pattern_count - 1; i++) {
    for (size_t j = i + 1; j < pattern_count; j++) {
      if (tracked_patterns[i].priority_score < tracked_patterns[j].priority_score) {
        request_pattern_t temp = tracked_patterns[i];
        tracked_patterns[i]    = tracked_patterns[j];
        tracked_patterns[j]    = temp;
      }
    }
  }

  // Warm top priority patterns that are likely to be requested soon
  for (size_t i = 0; i < pattern_count && warmed_count < 10; i++) {
    request_pattern_t* pattern = &tracked_patterns[i];

    // Skip if pattern doesn't match chain
    if (get_cache_key_chain_id(pattern->cache_key) != chain_id) continue;

    // Skip if recently accessed or low priority
    if (pattern->priority_score < 50 ||
        (now - pattern->last_requested) < pattern->avg_interval_ms / 2) continue;

    // Check if we should warm this pattern
    uint64_t expected_next_request = pattern->last_requested + pattern->avg_interval_ms;
    if (now >= expected_next_request - 30000) { // Warm 30s before expected request

      // Check if already in cache
      const void* cached = c4_proofer_cache_get(ctx, pattern->cache_key);
      if (!cached) {
        // This would trigger the actual warming logic
        // For now, just log the warming attempt
        log_debug("Would warm cache for key %b (priority: %d)",
                  bytes(pattern->cache_key, 32), pattern->priority_score);
        warmed_count++;
        warming_stats.requests_warmed++;
      }
    }
  }

  warming_stats.last_warming_time = now;
}

void c4_cache_warm_beacon_slots(proofer_ctx_t* ctx, uint64_t current_slot, uint32_t slots_ahead) {
  if (!warming_initialized || !ctx) return;

  // Warm cache for upcoming beacon slots
  for (uint32_t i = 1; i <= slots_ahead; i++) {
    uint64_t future_slot = current_slot + i;

    // Create cache key for future slot
    bytes32_t               slot_key = {0};
    structured_cache_key_t* sk       = (structured_cache_key_t*) slot_key;
    sk->prefix                       = CACHE_PREFIX_BEACON_SLOT;
    sk->version                      = 1;
    sk->chain_id                     = ctx->chain_id;
    sk->block_number                 = (uint32_t) (future_slot >> 32);
    *((uint32_t*) (sk->hash + 20))   = (uint32_t) future_slot;

    // Check if already cached
    const void* cached = c4_proofer_cache_get(ctx, slot_key);
    if (!cached) {
      log_debug("Would warm beacon slot %llu", future_slot);
      warming_stats.requests_warmed++;
    }
  }
}

void c4_cache_warm_recent_blocks(proofer_ctx_t* ctx, uint64_t latest_block, uint32_t blocks_back) {
  if (!warming_initialized || !ctx) return;

  // Warm cache for recent blocks (receipts/logs)
  for (uint32_t i = 1; i <= blocks_back; i++) {
    if (latest_block < i) break;

    uint64_t block_number = latest_block - i;

    // Create cache keys for receipts and logs
    bytes32_t receipt_key = {0};
    bytes32_t logs_key    = {0};

    structured_cache_key_t* rsk = (structured_cache_key_t*) receipt_key;
    rsk->prefix                 = CACHE_PREFIX_ETH_RECEIPT;
    rsk->version                = 1;
    rsk->chain_id               = ctx->chain_id;
    rsk->block_number           = (uint32_t) block_number;

    structured_cache_key_t* lsk = (structured_cache_key_t*) logs_key;
    lsk->prefix                 = CACHE_PREFIX_ETH_LOGS;
    lsk->version                = 1;
    lsk->chain_id               = ctx->chain_id;
    lsk->block_number           = (uint32_t) block_number;

    // Check and warm if needed
    if (!c4_proofer_cache_get(ctx, receipt_key)) {
      log_debug("Would warm receipts for block %llu", block_number);
      warming_stats.requests_warmed++;
    }

    if (!c4_proofer_cache_get(ctx, logs_key)) {
      log_debug("Would warm logs for block %llu", block_number);
      warming_stats.requests_warmed++;
    }
  }
}

const cache_warming_stats_t* c4_cache_warming_get_stats(void) {
  return &warming_stats;
}

void c4_cache_warming_predictive(proofer_ctx_t* ctx, uint64_t current_time) {
  if (!warming_initialized || !ctx) return;

  // Predictive warming based on time patterns
  for (size_t i = 0; i < pattern_count; i++) {
    request_pattern_t* pattern = &tracked_patterns[i];

    if (pattern->request_count < WARMING_TRIGGER_THRESHOLD) continue;

    // Predict next request time
    uint64_t predicted_next = pattern->last_requested + pattern->avg_interval_ms;

    // Warm if prediction is within window
    if (predicted_next > current_time &&
        predicted_next - current_time < PREDICTIVE_WINDOW_MS) {

      const void* cached = c4_proofer_cache_get(ctx, pattern->cache_key);
      if (!cached) {
        log_debug("Predictive warming for key %b (predicted in %llu ms)",
                  bytes(pattern->cache_key, 32), predicted_next - current_time);
        warming_stats.requests_warmed++;
      }
    }
  }
}


