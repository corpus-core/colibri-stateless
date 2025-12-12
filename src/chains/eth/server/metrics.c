/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */
#include "handler.h"
#include "logger.h"
#include "period_prover.h"
#include "server.h"
#include "util/bytes.h"

#if defined(PROVER_CACHE) && defined(CHAIN_ETH)
#include "chains/eth/prover/logs_cache.h"
#endif

void eth_server_metrics(http_server_t* server, buffer_t* data) {
  ETH_HANDLER_CHECK(server);
#if defined(PROVER_CACHE) && defined(CHAIN_ETH)
  if (c4_eth_logs_cache_is_enabled()) {
    uint64_t hits = 0, misses = 0, bloom_skips = 0;
    uint64_t blocks = 0, txs = 0, events = 0;
    c4_eth_logs_cache_counters(&hits, &misses, &bloom_skips);
    c4_eth_logs_cache_stats(&blocks, &txs, &events);
    uint64_t first_block = c4_eth_logs_cache_first_block();
    uint64_t last_block  = c4_eth_logs_cache_last_block();
    uint32_t capacity    = c4_eth_logs_cache_capacity_blocks();

    bprintf(data, "# HELP colibri_eth_logs_cache_hits_total Total eth_getLogs served from cache.\n");
    bprintf(data, "# TYPE colibri_eth_logs_cache_hits_total counter\n");
    bprintf(data, "colibri_eth_logs_cache_hits_total{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, hits);

    bprintf(data, "# HELP colibri_eth_logs_cache_misses_total Total eth_getLogs cache misses.\n");
    bprintf(data, "# TYPE colibri_eth_logs_cache_misses_total counter\n");
    bprintf(data, "colibri_eth_logs_cache_misses_total{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, misses);

    bprintf(data, "# HELP colibri_eth_logs_bloom_skipped_blocks_total Blocks skipped by bloom prefilter.\n");
    bprintf(data, "# TYPE colibri_eth_logs_bloom_skipped_blocks_total counter\n");
    bprintf(data, "colibri_eth_logs_bloom_skipped_blocks_total{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, bloom_skips);

    bprintf(data, "# HELP colibri_eth_logs_cached_blocks Number of blocks currently in logs cache.\n");
    bprintf(data, "# TYPE colibri_eth_logs_cached_blocks gauge\n");
    bprintf(data, "colibri_eth_logs_cached_blocks{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, blocks);

    bprintf(data, "# HELP colibri_eth_logs_cache_capacity_blocks Logs cache capacity in blocks.\n");
    bprintf(data, "# TYPE colibri_eth_logs_cache_capacity_blocks gauge\n");
    bprintf(data, "colibri_eth_logs_cache_capacity_blocks{chain_id=\"%d\"} %d\n", (uint32_t) server->chain_id, (uint32_t) capacity);

    bprintf(data, "# HELP colibri_eth_logs_cached_txs Estimated number of txs covered by cache.\n");
    bprintf(data, "# TYPE colibri_eth_logs_cached_txs gauge\n");
    bprintf(data, "colibri_eth_logs_cached_txs{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, txs);

    bprintf(data, "# HELP colibri_eth_logs_cached_events Number of events indexed in cache.\n");
    bprintf(data, "# TYPE colibri_eth_logs_cached_events gauge\n");
    bprintf(data, "colibri_eth_logs_cached_events{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, events);

    bprintf(data, "# HELP colibri_eth_logs_cache_first_block First block number in cache.\n");
    bprintf(data, "# TYPE colibri_eth_logs_cache_first_block gauge\n");
    bprintf(data, "colibri_eth_logs_cache_first_block{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, first_block);

    bprintf(data, "# HELP colibri_eth_logs_cache_last_block Last block number in cache.\n");
    bprintf(data, "# TYPE colibri_eth_logs_cache_last_block gauge\n");
    bprintf(data, "colibri_eth_logs_cache_last_block{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, last_block);

    bprintf(data, "\n");
  }
  else {
    // If disabled, export zeros for visibility
    bprintf(data, "# HELP colibri_eth_logs_cache_hits_total Total eth_getLogs served from cache.\n");
    bprintf(data, "# TYPE colibri_eth_logs_cache_hits_total counter\n");
    bprintf(data, "colibri_eth_logs_cache_hits_total{chain_id=\"%d\"} 0\n", (uint32_t) server->chain_id);
    bprintf(data, "# HELP colibri_eth_logs_cache_misses_total Total eth_getLogs cache misses.\n");
    bprintf(data, "# TYPE colibri_eth_logs_cache_misses_total counter\n");
    bprintf(data, "colibri_eth_logs_cache_misses_total{chain_id=\"%d\"} 0\n", (uint32_t) server->chain_id);
    bprintf(data, "# HELP colibri_eth_logs_cached_blocks Number of blocks currently in logs cache.\n");
    bprintf(data, "# TYPE colibri_eth_logs_cached_blocks gauge\n");
    bprintf(data, "colibri_eth_logs_cached_blocks{chain_id=\"%d\"} 0\n", (uint32_t) server->chain_id);
  }
#endif
  // Period store sync metrics (always export for visibility)
  bprintf(data, "# HELP colibri_period_sync_last_slot Last slot persisted to period store.\n");
  bprintf(data, "# TYPE colibri_period_sync_last_slot gauge\n");
  bprintf(data, "colibri_period_sync_last_slot{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, server->stats.period_sync_last_slot);

  bprintf(data, "# HELP colibri_period_sync_last_slot_timestamp_seconds Timestamp of last persisted slot (seconds).\n");
  bprintf(data, "# TYPE colibri_period_sync_last_slot_timestamp_seconds gauge\n");
  bprintf(data, "colibri_period_sync_last_slot_timestamp_seconds{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, (uint64_t) (server->stats.period_sync_last_slot_ts / 1000));

  bprintf(data, "# HELP colibri_period_sync_lag_slots Lag between latest known slot and persisted slot.\n");
  bprintf(data, "# TYPE colibri_period_sync_lag_slots gauge\n");
  bprintf(data, "colibri_period_sync_lag_slots{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, server->stats.period_sync_lag_slots);

  bprintf(data, "# HELP colibri_period_sync_queue_depth Current queue depth of pending writes.\n");
  bprintf(data, "# TYPE colibri_period_sync_queue_depth gauge\n");
  bprintf(data, "colibri_period_sync_queue_depth{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, server->stats.period_sync_queue_depth);

  bprintf(data, "# HELP colibri_period_sync_written_slots_total Slots written directly from new_head.\n");
  bprintf(data, "# TYPE colibri_period_sync_written_slots_total counter\n");
  bprintf(data, "colibri_period_sync_written_slots_total{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, server->stats.period_sync_written_slots_total);

  bprintf(data, "# HELP colibri_period_sync_backfilled_slots_total Slots written via backfill.\n");
  bprintf(data, "# TYPE colibri_period_sync_backfilled_slots_total counter\n");
  bprintf(data, "colibri_period_sync_backfilled_slots_total{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, server->stats.period_sync_backfilled_slots_total);

  bprintf(data, "# HELP colibri_period_sync_errors_total Errors encountered during period sync.\n");
  bprintf(data, "# TYPE colibri_period_sync_errors_total counter\n");
  bprintf(data, "colibri_period_sync_errors_total{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, server->stats.period_sync_errors_total);

  bprintf(data, "# HELP colibri_period_sync_retries_total Number of backfill retry scheduling events.\n");
  bprintf(data, "# TYPE colibri_period_sync_retries_total counter\n");
  bprintf(data, "colibri_period_sync_retries_total{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, server->stats.period_sync_retries_total);

  // Prover Metrics
  bprintf(data, "# HELP colibri_prover_last_run_timestamp_seconds Timestamp of the last proof run.\n");
  bprintf(data, "# TYPE colibri_prover_last_run_timestamp_seconds gauge\n");
  bprintf(data, "colibri_prover_last_run_timestamp_seconds{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, prover_stats.last_run_timestamp);

  bprintf(data, "# HELP colibri_prover_last_check_timestamp_seconds Timestamp of the last check loop.\n");
  bprintf(data, "# TYPE colibri_prover_last_check_timestamp_seconds gauge\n");
  bprintf(data, "colibri_prover_last_check_timestamp_seconds{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, prover_stats.last_check_timestamp);

  bprintf(data, "# HELP colibri_prover_last_run_duration_seconds Duration of the last proof run in seconds.\n");
  bprintf(data, "# TYPE colibri_prover_last_run_duration_seconds gauge\n");
  bprintf(data, "colibri_prover_last_run_duration_seconds{chain_id=\"%d\"} %f\n", (uint32_t) server->chain_id, (double) prover_stats.last_run_duration_ms / 1000.0);

  bprintf(data, "# HELP colibri_prover_last_run_status Status of the last proof run (0=success, 1=error).\n");
  bprintf(data, "# TYPE colibri_prover_last_run_status gauge\n");
  bprintf(data, "colibri_prover_last_run_status{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, prover_stats.last_run_status);

  bprintf(data, "# HELP colibri_prover_current_period The target period being processed.\n");
  bprintf(data, "# TYPE colibri_prover_current_period gauge\n");
  bprintf(data, "colibri_prover_current_period{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, prover_stats.current_period);

  bprintf(data, "# HELP colibri_prover_success_total Total successful proof runs.\n");
  bprintf(data, "# TYPE colibri_prover_success_total counter\n");
  bprintf(data, "colibri_prover_success_total{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, prover_stats.total_success);

  bprintf(data, "# HELP colibri_prover_failure_total Total failed proof runs.\n");
  bprintf(data, "# TYPE colibri_prover_failure_total counter\n");
  bprintf(data, "colibri_prover_failure_total{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, prover_stats.total_failure);
}
