#ifndef OP_CONF_H
#define OP_CONF_H

#include "server.h"

typedef struct {
  char* preconf_storage_dir;
  int   preconf_ttl_minutes;
  int   preconf_cleanup_interval_minutes;
  // When set, preconfs missing locally are fetched from this kona-preconf-service URL
  // and cached to preconf_storage_dir. Example: "http://kona-bridge:8080"
  char* master_kona_bridge_url;
} op_config_t;

extern op_config_t op_config;

void op_configure();

#endif