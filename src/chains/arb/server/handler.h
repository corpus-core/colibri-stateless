/*
 * Copyright 2025,2026 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef arb_server_handler_h__
#define arb_server_handler_h__

#include "server.h"

/** No-op return unless `server->chain_id` is Arbitrum. */
#define ARB_HANDLER_CHECK(server)                                                \
  do {                                                                           \
    if (!(server) || c4_chain_type((server)->chain_id) != C4_CHAIN_TYPE_ARBITRUM) \
      return;                                                                    \
  } while (0)

/**
 * Starts the sequencer-feed client when this server is running Arbitrum.
 *
 * @param server HTTP server instance
 */
void arb_server_init(http_server_t* server);

/**
 * Stops the sequencer-feed client.
 *
 * @param server HTTP server instance
 */
void arb_server_shutdown(http_server_t* server);

/**
 * Answers an internal request for the newest signed Arbitrum block.
 *
 * The only path is `arb_preconf/latest`. The response is an SSZ `arbFeedProof`
 * (`elHeader`, uncompressed `signedTail`, `signature`). The header is fetched
 * when the request arrives; the tail is the last feed message that had a block
 * hash and a signature.
 *
 * @param r internal request
 * @return true when the path starts with `arb_preconf/`
 */
bool c4_handle_arb_preconf(single_request_t* r);

#endif
