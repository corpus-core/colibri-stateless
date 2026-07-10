#include "op_conf.h"
#include "configure.h"
#include "logger.h"

op_config_t op_config = {
    .preconf_storage_dir              = "./preconfs",
    .preconf_ttl_minutes              = 30,
    .preconf_cleanup_interval_minutes = 5,
    .master_kona_bridge_url           = NULL,
};

void op_configure() {

  c4_configure_add_section("OP Stack");
  conf_string(&op_config.preconf_storage_dir, "PRECONF_DIR", "preconf_dir", 'P', "directory for storing preconfirmations");
  conf_int(&op_config.preconf_ttl_minutes, "PRECONF_TTL", "preconf_ttl", 'T', "TTL for preconfirmations in minutes", 1, 1440);
  conf_int(&op_config.preconf_cleanup_interval_minutes, "PRECONF_CLEANUP_INTERVAL", "preconf_cleanup_interval", 'C', "cleanup interval in minutes", 1, 60);
  conf_string(&op_config.master_kona_bridge_url, "MASTER_KONA_BRIDGE_URL", "master_kona_bridge_url", 0, "kona-preconf-service base URL for remote fallback (e.g. http://kona-bridge:8080)");

  http_server.prover_flags |= C4_PROVER_FLAG_USE_ACCESSLIST;
}