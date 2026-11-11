/*
 * Copyright 2025,2026 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef eez_server_h__
#define eez_server_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "server/server.h"

/**
 * Registers the HTTP handlers of a prover server for an EEZ chain.
 *
 * An EEZ chain reuses the Ethereum proof types, so the only route it adds is the
 * cache-friendly block proof of hybrid mode,
 * `GET /proof/<method>/<block>/<version>/<zk|std>/<c4>`: a hybrid client gets the proof
 * of the L2 block from this server and everything else from the RPC of the L2.
 *
 * Does nothing unless `server->chain_id` is an EEZ chain.
 *
 * @param server the server configuration (chain id)
 */
void eez_server_init(http_server_t* server);

#ifdef __cplusplus
}
#endif

#endif
