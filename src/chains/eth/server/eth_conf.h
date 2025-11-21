#ifndef ETH_CONF_H
#define ETH_CONF_H

#include "server.h"

typedef struct {
  int stream_beacon_events;
  int eth_logs_cache_blocks;
} eth_config_t;

extern eth_config_t eth_config;

void eth_configure();

#endif