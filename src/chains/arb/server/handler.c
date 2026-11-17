/*
 * Copyright 2025,2026 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "handler.h"
#include "feed_client.h"
#include "logger.h"

void arb_server_init(http_server_t* server) {
  ARB_HANDLER_CHECK(server);
  log_info("Initializing Arbitrum server handlers...");
  arb_feed_start();
}

void arb_server_shutdown(http_server_t* server) {
  ARB_HANDLER_CHECK(server);
  log_info("Shutting down Arbitrum server handler...");
  arb_feed_stop();
}
