/*
 * Copyright 2025,2026 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef eez_conf_h__
#define eez_conf_h__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * EEZ-specific HTTP server settings (env / `server.conf` / CLI via `eez_configure`).
 *
 * All fields are optional. The blockhash watcher only runs when both `l1_rpc` and
 * `blockhash_store` are set and the server is configured for an EEZ chain.
 */
typedef struct {
  char* l1_rpc;          /** comma-separated RPC URLs of the L1 chain the rollup settles on */
  char* blockhash_store; /** directory holding the recorded EIP-2935 history snapshots (one directory per L1 event) */
  int   watch_interval;  /** polling interval in seconds for new L2ExecutionPerformed events on L1 (default 12) */
} eez_config_t;

extern eez_config_t eez_config;

/**
 * Registers EEZ server env/CLI parameters and applies them to `eez_config`.
 *
 * Also registers the L1 RPC list of the configured EEZ chain with the HTTP client so
 * `c4_get_server_list(C4_DATA_TYPE_ETH_RPC, l1_chain_id)` returns it for any request
 * that carries the L1 chain id.
 */
void eez_configure();

#ifdef __cplusplus
}
#endif

#endif // eez_conf_h__
