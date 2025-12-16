/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */
#include "eth_conf.h"
#include "handler.h"
#include "logger.h"
#include "period_store.h"
#include "period_store_zk_prover.h"
#include "server.h"
#include "util/bytes.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <sys/stat.h>

// Cache for SP1 prover network balance to avoid reading a file on every metrics scrape.
static double   g_sp1_balance_cache_value         = 0.0;
static int      g_sp1_balance_cache_valid         = 0;
static uint64_t g_sp1_balance_cache_mtime_s       = 0;
static uint64_t g_sp1_balance_cache_updated_s     = 0;
static uint64_t g_sp1_balance_cache_last_run_s    = 0;
static uint64_t g_sp1_balance_cache_last_check_ms = 0;

// PROVE token has 18 decimals.
static const double PROVE_TOKEN_DECIMALS = 1e18;

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

  // Blocks root verification marker metrics (blocks_root.bin).
  bprintf(data, "# HELP colibri_blocks_root_last_verified_period Last period with verified blocks_root.bin marker.\n");
  bprintf(data, "# TYPE colibri_blocks_root_last_verified_period gauge\n");
  bprintf(data, "colibri_blocks_root_last_verified_period{chain_id=\"%d\"} %l\n",
          (uint32_t) server->chain_id,
          c4_ps_blocks_root_last_verified_period());

  bprintf(data, "# HELP colibri_blocks_root_last_verified_timestamp_seconds Timestamp of last verified blocks_root.bin marker (seconds).\n");
  bprintf(data, "# TYPE colibri_blocks_root_last_verified_timestamp_seconds gauge\n");
  bprintf(data, "colibri_blocks_root_last_verified_timestamp_seconds{chain_id=\"%d\"} %l\n",
          (uint32_t) server->chain_id,
          c4_ps_blocks_root_last_verified_timestamp_seconds());

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

  // SP1 Prover Network balance (optional, written by eth-sync-script when SP1_BALANCE_FILE is set).
  if (eth_config.period_store) {
    char* path = bprintf(NULL, "%s/sp1_balance.txt", eth_config.period_store);

    // Force refresh after a new proof run (balance likely changed).
    uint64_t now_ms          = current_ms();
    uint64_t prover_last_run = prover_stats.last_run_timestamp;
    int      force_refresh   = (prover_last_run != g_sp1_balance_cache_last_run_s);

    // Additionally, avoid even calling stat too often if scrapes are very frequent.
    // With a 15s scrape interval, this keeps syscalls minimal.
    int time_refresh = (g_sp1_balance_cache_last_check_ms == 0) ||
                       (now_ms - g_sp1_balance_cache_last_check_ms > 15000);

    if (path && (force_refresh || time_refresh)) {
      g_sp1_balance_cache_last_check_ms = now_ms;

      struct stat st;
      if (stat(path, &st) == 0) {
        uint64_t mtime_s = (uint64_t) st.st_mtime;

        // Read only if file changed or cache not valid, or we forced refresh.
        if (force_refresh || !g_sp1_balance_cache_valid || mtime_s != g_sp1_balance_cache_mtime_s) {
          bytes_t b = bytes_read(path);
          if (b.data && b.len) {
            // Ensure null termination for parsing.
            char* s = (char*) safe_malloc((size_t) b.len + 1);
            memcpy(s, b.data, b.len);
            s[b.len] = 0;

            errno     = 0;
            char* end = NULL;
            // Balance can exceed uint64_t; Prometheus gauges are float64 anyway.
            // Parse as double (decimal). This is lossy for very large values but good enough for monitoring.
            double v = strtod(s, &end);
            if (errno == 0 && end && end != s && isfinite(v)) {
              g_sp1_balance_cache_value     = v;
              g_sp1_balance_cache_valid     = 1;
              g_sp1_balance_cache_mtime_s   = mtime_s;
              g_sp1_balance_cache_updated_s = mtime_s;
            }
            else {
              g_sp1_balance_cache_value     = 0.0;
              g_sp1_balance_cache_valid     = 0;
              g_sp1_balance_cache_mtime_s   = mtime_s;
              g_sp1_balance_cache_updated_s = mtime_s;
            }

            safe_free(s);
          }
          else {
            g_sp1_balance_cache_value     = 0.0;
            g_sp1_balance_cache_valid     = 0;
            g_sp1_balance_cache_mtime_s   = mtime_s;
            g_sp1_balance_cache_updated_s = mtime_s;
          }
          safe_free(b.data);
        }
      }
      else if (force_refresh) {
        // If we expected an update after a run but file is missing, clear cache once.
        g_sp1_balance_cache_value     = 0.0;
        g_sp1_balance_cache_valid     = 0;
        g_sp1_balance_cache_mtime_s   = 0;
        g_sp1_balance_cache_updated_s = 0;
      }

      // Remember last run timestamp so we don't force every scrape.
      g_sp1_balance_cache_last_run_s = prover_last_run;
    }

    bprintf(data, "# HELP colibri_prover_network_balance Current SP1 prover network balance in PROVE tokens (decimals=18, best-effort).\n");
    bprintf(data, "# TYPE colibri_prover_network_balance gauge\n");
    bprintf(data, "colibri_prover_network_balance{chain_id=\"%d\"} %f\n",
            (uint32_t) server->chain_id,
            g_sp1_balance_cache_value / PROVE_TOKEN_DECIMALS);

    bprintf(data, "# HELP colibri_prover_network_balance_valid 1 if balance file was read and parsed successfully.\n");
    bprintf(data, "# TYPE colibri_prover_network_balance_valid gauge\n");
    bprintf(data, "colibri_prover_network_balance_valid{chain_id=\"%d\"} %d\n", (uint32_t) server->chain_id, g_sp1_balance_cache_valid);

    bprintf(data, "# HELP colibri_prover_network_balance_timestamp_seconds mtime of balance file (seconds).\n");
    bprintf(data, "# TYPE colibri_prover_network_balance_timestamp_seconds gauge\n");
    bprintf(data, "colibri_prover_network_balance_timestamp_seconds{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, g_sp1_balance_cache_updated_s);

    safe_free(path);
  }
}
