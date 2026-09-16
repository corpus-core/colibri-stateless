#ifndef OP_CONF_H
#define OP_CONF_H

#include "server.h"

/** OP-Stack-specific HTTP server settings (env / `server.conf` via `op_configure`). */
typedef struct {
  char* preconf_storage_dir;              /** directory for cached preconfirmation payloads */
  int   preconf_ttl_minutes;              /** TTL for stored preconfs before eviction */
  int   preconf_cleanup_interval_minutes; /** interval between cleanup sweeps */
  /** When set, missing preconfs are fetched from this kona-preconf-service URL and cached locally. */
  char* master_kona_bridge_url;
} op_config_t;

extern op_config_t op_config;

/** Registers OP-Stack server env/CLI parameters and applies them to `op_config`. */
void op_configure();

#endif
