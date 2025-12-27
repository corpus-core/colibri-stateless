#ifndef OP_CONF_H
#define OP_CONF_H

#include "server.h"

typedef struct {
  char* preconf_storage_dir;
  int   preconf_ttl_minutes;
  int   preconf_cleanup_interval_minutes;
  // preconf_use_gossip removed - now using automatic HTTP fallback until gossip is active

} op_config_t;

extern op_config_t op_config;

void op_configure();

#endif