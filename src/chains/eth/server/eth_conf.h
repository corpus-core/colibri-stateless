#ifndef ETH_CONF_H
#define ETH_CONF_H

#include "server.h"

typedef struct {
  int   stream_beacon_events;
  char* period_store;
  int   period_backfill_delay_ms;    // delay between backfill requests (ms) to avoid public API rate limits
  int   period_backfill_max_periods; // how many periods to backfill at startup (default 2)
  int   eth_logs_cache_blocks;
  char* zk_proofs_dir; // directory to store zk proofs

} eth_config_t;

extern eth_config_t eth_config;

void eth_configure();

#endif