/*
 * Copyright 2025,2026 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "handler.h"
#include "arb_prover.h" // arb_register_signer_lookup
#include "arb_verify.h"
#include "bytes.h"
#include "feed_client.h"
#include "logger.h"
#include <string.h>

// Server-side precheck: if the feed has a current signed head and the client
// did not already announce the matching batch poster, raise the prover flag.
// The prover itself pulls the actual address via `arb_register_signer_lookup`,
// which is wired to the feed's cached signer below.
static void arb_batch_poster_precheck(prover_ctx_t* ctx, const uint8_t* batch_poster, uint32_t len) {
  uint8_t signer[20] = {0};
  if (!ctx || c4_chain_type(ctx->chain_id) != C4_CHAIN_TYPE_ARBITRUM) return;
  if (!arb_feed_get_head_signer(c4_chain_specific_id(ctx->chain_id), signer)) return;
  if (batch_poster && len == 20 && memcmp(batch_poster, signer, 20) == 0) return;
  ctx->flags |= C4_PROVER_FLAG_ARB_BATCH_POSTER;
}

void arb_server_init(http_server_t* server) {
  ARB_HANDLER_CHECK(server);
  log_info("Initializing Arbitrum server handlers...");
  c4_register_internal_handler(c4_handle_arb_preconf);
  c4_set_batch_poster_precheck(arb_batch_poster_precheck);
  arb_register_signer_lookup(arb_feed_get_head_signer);
  arb_feed_start();
}

void arb_server_shutdown(http_server_t* server) {
  ARB_HANDLER_CHECK(server);
  log_info("Shutting down Arbitrum server handler...");
  arb_register_signer_lookup(NULL);
  arb_feed_stop();
}
