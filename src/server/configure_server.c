/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
#include "chains.h"
#include "configure.h"
#include "logger.h"
#include "server.h"
#include "tracing.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(PROVER_CACHE) && defined(CHAIN_ETH)
#include "chains/eth/prover/logs_cache.h"
#endif

http_server_t http_server = {

    // Set default values
    .host              = "127.0.0.1", // Localhost only by default (security best practice)
    .port              = 8090,
    .memcached_host    = "", // Empty by default - memcached is optional
    .memcached_port    = 11211,
    .memcached_pool    = 20,
    .loglevel          = LOG_WARN,
    .req_timeout       = 120,
    .chain_id          = 1,
    .rpc_nodes         = "https://nameless-sly-reel.quiknode.pro/5937339c28c09a908994b74e2514f0f6cfdac584/,https://eth-mainnet.g.alchemy.com/v2/B8W2IZrDkCkkjKxQOl70XNIy4x4PT20S,https://rpc.ankr.com/eth/33d0414ebb46bda32a461ecdbd201f9cf5141a0acb8f95c718c23935d6febfcd",
    .beacon_nodes      = "https://lodestar-mainnet.chainsafe.io/",
    .prover_nodes      = "",
    .checkpointz_nodes = "https://sync-mainnet.beaconcha.in,https://beaconstate.info,https://sync.invis.tools,https://beaconstate.ethstaker.cc",

    .stream_beacon_events             = 0,
    .period_store                     = NULL,
    .period_backfill_delay_ms         = 100, // default 100ms to be gentle with public APIs,
    .period_backfill_max_periods      = 2,   // default backfill up to 2 periods,
    .preconf_storage_dir              = "./preconfs",
    .preconf_ttl_minutes              = 30, // 30 minutes TTL,
    .preconf_cleanup_interval_minutes = 5,  // Cleanup every 5 minutes,
                                            // preconf_use_gossip removed - now using automatic HTTP fallback,
    // Web UI (disabled by default for security)
    .web_ui_enabled = 0,

    // Heuristic load-balancing defaults
    .max_concurrency_default    = 8,
    .max_concurrency_cap        = 64,
    .latency_target_ms          = 200,
    .conc_cooldown_ms           = 30000,
    .overflow_slots             = 1,
    .saturation_wait_ms         = 100,
    .method_stats_half_life_sec = 60,
    .block_availability_window  = 512,
    .block_availability_ttl_sec = 300,
    // 0 = auto (use chain block_time as default)
    .rpc_head_poll_interval_ms = 0,
    .rpc_head_poll_enabled     = 1,
    // Latency bias defaults
    .latency_bias_power_x100         = 200, // 2.0
    .latency_backpressure_power_x100 = 200, // 2.0,
    .latency_bias_offset_ms          = 50,  // ms,

    // cURL pool defaults
    .curl.http2_enabled         = 1,
    .curl.pool_max_host         = 4,
    .curl.pool_max_total        = 64,
    .curl.pool_maxconnects      = 128,
    .curl.upkeep_interval_ms    = 15000,
    .curl.tcp_keepalive_enabled = 1,
    .curl.tcp_keepidle_s        = 30,
    .curl.tcp_keepintvl_s       = 15,

    .eth_logs_cache_blocks = 0,

#ifdef TEST
    .test_dir = NULL,
#endif
    // Tracing defaults
    .tracing_enabled        = 0,
    .tracing_url            = "",
    .tracing_service_name   = "colibri-stateless",
    .tracing_sample_percent = 10 // 10%
};

static void config() {
  conf_string(&http_server.host, "HOST", "host", 'h', "Host/IP address to bind to (127.0.0.1=localhost only, 0.0.0.0=all interfaces)");
  conf_int(&http_server.port, "PORT", "port", 'p', "Port to listen on", 1, 65535);
  conf_string(&http_server.memcached_host, "MEMCACHED_HOST", "memcached_host", 'm', "hostnane of the memcached server");
  conf_key(http_server.witness_key, "WITNESS_KEY", "witness_key", 'w', "hexcode or path to a private key used as signer for the witness");
  conf_int(&http_server.memcached_port, "MEMCACHED_PORT", "memcached_port", 'P', "port of the memcached server", 1, 65535);
  conf_int(&http_server.memcached_pool, "MEMCACHED_POOL", "memcached_pool", 'S', "pool size of the memcached server", 1, 100);
  conf_int(&http_server.loglevel, "LOG_LEVEL", "log_level", 'l', "log level", 0, 5);
  conf_int(&http_server.req_timeout, "REQUEST_TIMEOUT", "req_timeout", 't', "request timeout", 1, 300);
  conf_int(&http_server.chain_id, "CHAIN_ID", "chain_id", 'c', "chain id", 1, 0xFFFFFFF);
  conf_string(&http_server.rpc_nodes, "RPC", "rpc", 'r', "list of rpc endpoints");
  conf_string(&http_server.beacon_nodes, "BEACON", "beacon", 'b', "list of beacon nodes api endpoints");
  conf_string(&http_server.prover_nodes, "PROVER", "prover", 'R', "list of remote prover endpoints");
  conf_string(&http_server.checkpointz_nodes, "CHECKPOINTZ", "checkpointz", 'z', "list of checkpointz server endpoints");
  conf_int(&http_server.stream_beacon_events, "BEACON_EVENTS", "beacon_events", 'e', "activates beacon event streaming", 0, 1);
  conf_int(&http_server.period_backfill_delay_ms, "C4_PERIOD_BACKFILL_DELAY_MS", "period_backfill_delay_ms", 0, "delay between backfill requests (ms)", 0, 60000);
  conf_int(&http_server.period_backfill_max_periods, "C4_PERIOD_BACKFILL_MAX_PERIODS", "period_backfill_max_periods", 0, "max number of periods to backfill at startup", 0, 10000);
  // Optional logs cache size in blocks (default 0 = disabled). Only enabled when beacon events are active.
  conf_int(&http_server.eth_logs_cache_blocks, "ETH_LOGS_CACHE_BLOCKS", "eth_logs_cache_blocks", 0, "max number of contiguous blocks to cache logs for eth_getLogs", 0, 131072);

  conf_string(&http_server.period_store, "DATA", "data", 'd', "path to the data-directory holding blockroots and light client updates");
  conf_string(&http_server.preconf_storage_dir, "PRECONF_DIR", "preconf_dir", 'P', "directory for storing preconfirmations");
  conf_int(&http_server.preconf_ttl_minutes, "PRECONF_TTL", "preconf_ttl", 'T', "TTL for preconfirmations in minutes", 1, 1440);
  conf_int(&http_server.preconf_cleanup_interval_minutes, "PRECONF_CLEANUP_INTERVAL", "preconf_cleanup_interval", 'C', "cleanup interval in minutes", 1, 60);
  // preconf_use_gossip option removed - now using automatic HTTP fallback

  conf_int(&http_server.web_ui_enabled, "WEB_UI_ENABLED", "web_ui_enabled", 'u', "enable web-based configuration UI (0=disabled, 1=enabled)", 0, 1);

  // Heuristic load-balancing configuration (ENV/args)
  conf_int(&http_server.max_concurrency_default, "C4_MAX_CONCURRENCY_DEFAULT", "max_concurrency_default", 'M', "default per-server max concurrency", 1, 4096);
  conf_int(&http_server.max_concurrency_cap, "C4_MAX_CONCURRENCY_CAP", "max_concurrency_cap", 'K', "cap for dynamic concurrency", 1, 65535);
  conf_int(&http_server.latency_target_ms, "C4_LATENCY_TARGET_MS", "latency_target_ms", 'L', "target latency for AIMD (ms)", 10, 100000);
  conf_int(&http_server.conc_cooldown_ms, "C4_CONC_COOLDOWN_MS", "conc_cooldown_ms", 'o', "cooldown for concurrency adjustments (ms)", 0, 600000);
  conf_int(&http_server.overflow_slots, "C4_OVERFLOW_SLOTS", "overflow_slots", 'v', "overflow slots per server when saturated", 0, 16);
  conf_int(&http_server.saturation_wait_ms, "C4_SATURATION_WAIT_MS", "saturation_wait_ms", 'W', "short wait on saturation before overflow (ms)", 0, 10000);
  conf_int(&http_server.method_stats_half_life_sec, "C4_METHOD_STATS_HALF_LIFE_SEC", "method_stats_half_life_sec", 'H', "half-life for method stats (sec)", 1, 3600);
  conf_int(&http_server.block_availability_window, "C4_BLOCK_AVAIL_WINDOW", "block_availability_window", 'B', "block availability window size", 64, 8192);
  conf_int(&http_server.block_availability_ttl_sec, "C4_BLOCK_AVAIL_TTL_SEC", "block_availability_ttl_sec", 'J', "block availability TTL (sec)", 10, 86400);
  conf_int(&http_server.rpc_head_poll_interval_ms, "C4_RPC_HEAD_POLL_INTERVAL_MS", "rpc_head_poll_interval_ms", 'q', "interval for eth_blockNumber polling (ms)", 500, 60000);
  conf_int(&http_server.rpc_head_poll_enabled, "C4_RPC_HEAD_POLL_ENABLED", "rpc_head_poll_enabled", 'Q', "enable head polling (0/1)", 0, 1);
  // Latency bias/backpressure tuning
  conf_int(&http_server.latency_bias_power_x100, "C4_LATENCY_BIAS_POWER_X100", "latency_bias_power_x100", 0, "exponent*100 for latency bias (e.g. 200=2.0)", 50, 1000);
  conf_int(&http_server.latency_backpressure_power_x100, "C4_LATENCY_BACKPRESSURE_POWER_X100", "latency_backpressure_power_x100", 0, "exponent*100 for backpressure penalty (e.g. 200=2.0)", 50, 1000);
  conf_int(&http_server.latency_bias_offset_ms, "C4_LATENCY_BIAS_OFFSET_MS", "latency_bias_offset_ms", 0, "offset added to latency for stability (ms)", 0, 1000);

  // cURL pool configuration (ENV/args)
  conf_int(&http_server.curl.http2_enabled, "C4_HTTP2", "http2", 0, "enable HTTP/2 (0/1)", 0, 1);
  conf_int(&http_server.curl.pool_max_host, "C4_POOL_MAX_HOST", "pool_max_host", 0, "max connections per host", 1, 1024);
  conf_int(&http_server.curl.pool_max_total, "C4_POOL_MAX_TOTAL", "pool_max_total", 0, "max total connections", 1, 65536);
  conf_int(&http_server.curl.pool_maxconnects, "C4_POOL_MAXCONNECTS", "pool_maxconnects", 0, "connection cache size", 1, 65536);
  conf_int(&http_server.curl.upkeep_interval_ms, "C4_UPKEEP_MS", "upkeep_ms", 0, "upkeep interval (ms)", 0, 600000);
  conf_int(&http_server.curl.tcp_keepalive_enabled, "C4_TCP_KEEPALIVE", "tcp_keepalive", 0, "TCP keepalive (0/1)", 0, 1);
  conf_int(&http_server.curl.tcp_keepidle_s, "C4_TCP_KEEPIDLE", "tcp_keepidle", 0, "TCP keepidle seconds", 1, 3600);
  conf_int(&http_server.curl.tcp_keepintvl_s, "C4_TCP_KEEPINTVL", "tcp_keepintvl", 0, "TCP keepintvl seconds", 1, 3600);

#ifdef TEST
  conf_string(&http_server.test_dir, "TEST_DIR", "test_dir", 'x', "TEST MODE: record all responses to TESTDATA_DIR/server/<test_dir>/");
#endif

  // Tracing (ENV/args)
  conf_int(&http_server.tracing_enabled, "C4_TRACING_ENABLED", "tracing_enabled", 0, "enable tracing (0/1)", 0, 1);
  conf_string(&http_server.tracing_url, "C4_TRACING_URL", "tracing_url", 0, "Zipkin v2 endpoint (e.g. http://localhost:9411/api/v2/spans)");
  conf_string(&http_server.tracing_service_name, "C4_TRACING_SERVICE", "tracing_service", 0, "Tracing service name");
  conf_int(&http_server.tracing_sample_percent, "C4_TRACING_SAMPLE_PERCENT", "tracing_sample_percent", 0, "Tracing sample rate percent (0..100)", 0, 100);
}

void c4_configure(int argc, char* argv[]) {
  c4_init_config(argc, argv);

  // applymain config parameters
  config();

  if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0))
    c4_write_usage();
  else
    c4_write_config();

  c4_set_log_level(http_server.loglevel);
  // Apply tracing configuration
  tracing_configure(http_server.tracing_enabled != 0,
                    http_server.tracing_url,
                    http_server.tracing_service_name,
                    (double) http_server.tracing_sample_percent / 100.0);
#if defined(PROVER_CACHE) && defined(CHAIN_ETH)
  if (http_server.stream_beacon_events && http_server.eth_logs_cache_blocks > 0) {
    c4_eth_logs_cache_enable((uint32_t) http_server.eth_logs_cache_blocks);
    log_info("eth_logs_cache enabled with capacity: %d blocks", (uint32_t) http_server.eth_logs_cache_blocks);
  }
  else {
    c4_eth_logs_cache_disable();
    log_info("eth_logs_cache disabled (beacon_events=%d, capacity=%d)", (uint32_t) http_server.stream_beacon_events, (uint32_t) http_server.eth_logs_cache_blocks);
  }
#endif
}
