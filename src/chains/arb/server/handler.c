/*
 * Copyright 2025,2026 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "handler.h"
#include "arb_verify.h"
#include "bytes.h"
#include "feed_client.h"
#include "logger.h"
#include <string.h>

static void arb_batch_poster_precheck(prover_ctx_t* ctx, const uint8_t* batch_poster, uint32_t len) {
  uint8_t  hash[32]  = {0};
  uint8_t  sig[65]   = {0};
  uint8_t  signer[20] = {0};
  uint8_t* tail      = NULL;
  uint32_t tail_len  = 0;
  uint64_t sequence  = 0;
  if (!ctx || c4_chain_type(ctx->chain_id) != C4_CHAIN_TYPE_ARBITRUM) return;
  if (!arb_feed_copy_head(hash, sig, &tail, &tail_len, &sequence)) return;
  bool recovered = arb_feed_recover_signer(c4_chain_specific_id(ctx->chain_id), sequence, hash,
                                            bytes(tail, tail_len), bytes(sig, 65), signer);
  safe_free(tail);
  if (!recovered) return;
  if (batch_poster && len == 20 && memcmp(batch_poster, signer, 20) == 0) return;
  memcpy(ctx->batch_poster, signer, 20);
  ctx->flags |= C4_PROVER_FLAG_ARB_BATCH_POSTER;
}

void arb_server_init(http_server_t* server) {
  ARB_HANDLER_CHECK(server);
  log_info("Initializing Arbitrum server handlers...");
  c4_register_internal_handler(c4_handle_arb_preconf);
  c4_set_batch_poster_precheck(arb_batch_poster_precheck);
  arb_feed_start();
}

void arb_server_shutdown(http_server_t* server) {
  ARB_HANDLER_CHECK(server);
  log_info("Shutting down Arbitrum server handler...");
  arb_feed_stop();
}
