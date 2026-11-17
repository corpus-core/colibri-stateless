/*
 * Copyright 2025,2026 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "arb_conf.h"
#include "configure.h"

arb_config_t arb_config = {
    .feed_url = "wss://arb1-feed.arbitrum.io/feed",
};

void arb_configure(void) {
  c4_configure_add_section("Arbitrum");
  conf_string(&arb_config.feed_url, "ARB_FEED_URL", "arb_feed_url", 0,
              "Arbitrum sequencer feed WebSocket URL (wss://)");
}
