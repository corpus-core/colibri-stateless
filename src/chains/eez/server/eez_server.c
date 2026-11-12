/*
 * Copyright 2025,2026 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "eez_server.h"
#include "chains/eth/server/handler.h"
#include "logger.h"
#include "util/chains.h"

void eez_server_init(http_server_t* server) {
  if (!server || c4_chain_type(server->chain_id) != C4_CHAIN_TYPE_EEZ) return;

  log_info("Initializing EEZ server handlers...");
  // The handler is the one of the Ethereum module: it only rebuilds the proof request
  // from the path, and the prover of the chain of this server answers it.
  c4_register_http_handler(c4_handle_proof_get_request);
}
