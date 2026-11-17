/*
 * Copyright 2025,2026 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef arb_conf_h__
#define arb_conf_h__

/** Arbitrum HTTP-server settings (env / `server.conf` via `arb_configure`). */
typedef struct {
  char* feed_url; /** sequencer feed WebSocket URL (`wss://…`) */
} arb_config_t;

extern arb_config_t arb_config;

/**
 * Registers the Arbitrum server settings and applies env / CLI overrides.
 */
void arb_configure(void);

#endif
