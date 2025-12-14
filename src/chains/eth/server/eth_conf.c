#include "eth_conf.h"
#include "configure.h"
#include "logger.h"
#ifdef PROVER_CACHE
#include "../prover/logs_cache.h"
#endif

eth_config_t eth_config = {
    .stream_beacon_events        = 0,
    .period_store                = NULL,
    .period_backfill_delay_ms    = 100, // default 100ms to be gentle with public APIs,
    .period_backfill_max_periods = 2,   // default backfill up to 2 periods,
    .period_full_sync            = 0,
    .eth_logs_cache_blocks       = 0,

};

void eth_configure() {
  c4_configure_add_section("ETH");
  conf_int(&eth_config.stream_beacon_events, "BEACON_EVENTS", "beacon_events", 'e', "activates beacon event streaming", 0, 1);
  conf_int(&eth_config.period_backfill_delay_ms, "C4_PERIOD_BACKFILL_DELAY_MS", "period_backfill_delay_ms", 0, "delay between backfill requests (ms)", 0, 60000);
  conf_int(&eth_config.period_backfill_max_periods, "C4_PERIOD_BACKFILL_MAX_PERIODS", "period_backfill_max_periods", 0, "max number of periods to backfill at startup", 0, 10000);
  conf_int(&eth_config.eth_logs_cache_blocks, "ETH_LOGS_CACHE_BLOCKS", "eth_logs_cache_blocks", 0, "max number of contiguous blocks to cache logs for eth_getLogs", 0, 131072);
  conf_string(&eth_config.period_store, "DATA", "data", 'd', "path to the data-directory holding blockroots and light client updates");
  conf_string(&eth_config.period_master_url, "PERIOD_MASTER_URL", "period_master_url", 0, "URL of the master node to use. if set, the server will not write to the period-store but fetch it when needed.");
  conf_int(&eth_config.period_full_sync, "C4_PERIOD_FULL_SYNC", "period_full_sync", 0, "if enabled and period_master_url is set, periodically sync full period_store from master", 0, 1);
  conf_string(&eth_config.period_prover_key_file, "PERIOD_PROVER_KEY_FILE", "period_prover_key_file", 0, "Path to file containing SP1/Network private key");

#if defined(PROVER_CACHE) && defined(CHAIN_ETH)
  if (eth_config.stream_beacon_events && eth_config.eth_logs_cache_blocks > 0) {
    c4_eth_logs_cache_enable((uint32_t) eth_config.eth_logs_cache_blocks);
    log_info("eth_logs_cache enabled with capacity: %d blocks", (uint32_t) eth_config.eth_logs_cache_blocks);
  }
  else {
    c4_eth_logs_cache_disable();
    log_info("eth_logs_cache disabled (beacon_events=%d, capacity=%d)", (uint32_t) eth_config.stream_beacon_events, (uint32_t) eth_config.eth_logs_cache_blocks);
  }
#endif

  http_server.prover_flags |= (eth_config.period_store ? C4_PROVER_FLAG_CHAIN_STORE : 0) | C4_PROVER_FLAG_USE_ACCESSLIST;
}