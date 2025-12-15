#ifndef ETH_CONF_H
#define ETH_CONF_H

#include "server.h"

typedef struct {
  int   stream_beacon_events;
  char* period_store;
  int   period_backfill_delay_ms;    // delay between backfill requests (ms) to avoid public API rate limits
  int   period_backfill_max_periods; // how many periods to backfill at startup (default 2)
  char* period_master_url;           // URL of the master node to use. if set, the server will not write to the period-store but fetch it when needed.
  int   period_full_sync;            // if set and period_master_url is configured, periodically sync full period_store from master
  char* period_prover_key_file;      // Path to file containing SP1/Network private key
  int   eth_logs_cache_blocks;

} eth_config_t;

extern eth_config_t eth_config;

void eth_configure();

#endif