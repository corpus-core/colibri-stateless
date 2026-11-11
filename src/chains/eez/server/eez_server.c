/*
 * Copyright 2025,2026 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "eez_server.h"
#include "blockhash_store.h"
#include "chains/eth/server/handler.h"
#include "logger.h"
#include "util/chains.h"

void eez_server_init(http_server_t* server) {
  if (!server || c4_chain_type(server->chain_id) != C4_CHAIN_TYPE_EEZ) return;

  log_info("Initializing EEZ server handlers...");
  // The handler is the one of the Ethereum module: it only rebuilds the proof request
  // from the path, and the prover of the chain of this server answers it.
  c4_register_http_handler(c4_handle_proof_get_request);
  // EIP-2935 blockhash store watcher (optional; only runs when configured).
  eez_bh_init(server);
  // Internal-call handler that serves cached EIP-2935 proofs to the prover without
  // touching the L2 RPC. Always registered so the prover can route unconditionally and
  // get a clean error ("no snapshot available") when the watcher is not configured.
  c4_register_internal_handler(c4_handle_eez_blockhash);
}

void eez_server_shutdown(http_server_t* server) {
  if (!server || c4_chain_type(server->chain_id) != C4_CHAIN_TYPE_EEZ) return;
  eez_bh_shutdown(server);
}
