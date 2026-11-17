/*
 * Copyright 2025,2026 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef arb_feed_client_h__
#define arb_feed_client_h__

/**
 * Opens the sequencer feed configured in `arb_config.feed_url` and logs each
 * message's sequence number and block hash.
 *
 * The connection lives on the libuv default loop. A second call while the
 * client is already running does nothing. An empty URL is logged and ignored.
 */
void arb_feed_start(void);

/**
 * Closes the feed socket and stops reconnects.
 *
 * Safe to call when the client was never started.
 */
void arb_feed_stop(void);

#endif
