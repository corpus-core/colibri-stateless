/*
 * Copyright 2025,2026 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "eez_conf.h"
#include "../verifier/eez_chains_conf.h"
#include "configure.h"
#include "logger.h"
#include "server.h"
#include "util/chains.h"

eez_config_t eez_config = {
    .l1_rpc          = NULL,
    .blockhash_store = NULL,
    .watch_interval  = 12,
};

void eez_configure() {
  c4_configure_add_section("EEZ (L2)");
  conf_string(&eez_config.l1_rpc, "EEZ_L1_RPC", "eez_l1_rpc", 0,
              "comma-separated RPC URLs of the L1 chain; required for the EIP-2935 blockhash watcher");
  conf_string(&eez_config.blockhash_store, "EEZ_BLOCKHASH_STORE", "eez_blockhash_store", 0,
              "directory the EIP-2935 history snapshots are written to; required for remote proof mode");
  conf_int(&eez_config.watch_interval, "EEZ_WATCH_INTERVAL", "eez_watch_interval", 0,
           "polling interval in seconds for new L2ExecutionPerformed events on L1", 1, 3600);

  // Register the L1 RPC list so `c4_get_server_list(C4_DATA_TYPE_ETH_RPC, l1_chain_id)`
  // returns it, enabling the watcher and the prover path to call L1 via the normal
  // `c4_add_request` routing.
  if (eez_config.l1_rpc && *eez_config.l1_rpc) {
    const eez_chain_config_t* conf = eez_get_chain_config(http_server.chain_id);
    if (conf) {
      c4_register_eth_rpc_list(conf->l1_chain_id, eez_config.l1_rpc);
      log_info("EEZ L1 RPC registered for chain %l", (uint64_t) conf->l1_chain_id);
    }
    else {
      log_warn("EEZ_L1_RPC set but server chain %l is not a known EEZ rollup", (uint64_t) http_server.chain_id);
    }
  }
}
