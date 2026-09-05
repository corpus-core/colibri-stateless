/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "beacon.h"
#include "beacon_types.h"
#include "bootstrap_gloas.h"
#include "el_header.h"
#include "eth_conf.h"
#include "eth_req.h"
#include "handler.h"
#include "logger.h"
#include "prover/prover.h"
#ifdef PROVER_CACHE
#include "chains/eth/prover/logs_cache.h"
#endif
#include "chains/eth/server/period_store.h"
#include "server.h"
#include "tx_cache.h"
#include "util/json.h"
#include "util/state.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <uv.h>

// activate this in order to check for the latest block number from the execution node to make sure the exectuion node is cabable of handling the latest block.
bool c4_watcher_check_block_number = false;

static void prover_request_free(request_t* req) {
  c4_prover_free((prover_ctx_t*) req->ctx);
  safe_free(req);
}

#ifdef PROVER_CACHE
typedef struct {
  uint64_t block_number;
  uint8_t  logs_bloom[256];
  bool     bloom_fetched;
} prefetch_state_t;

static void logs_prefetch_cb(request_t* req) {
  if (c4_check_retry_request(req)) return;
  prover_ctx_t*     ctx     = (prover_ctx_t*) req->ctx;
  prefetch_state_t* s       = (prefetch_state_t*) ctx->proof.data;
  uint8_t           tmp[64] = {0};
  buffer_t          b       = stack_buffer(tmp);
  json_t            recs    = {0};

  if (!s->bloom_fetched) {
    json_t block_json = {0};
    switch (eth_get_block(ctx, json_parse(bprintf(&b, "\"0x%lx\"", s->block_number)), true, &block_json)) {
      case C4_SUCCESS:
        if (block_json.type == JSON_TYPE_OBJECT) {
          buffer_t bloom_buffer = stack_buffer(&s->logs_bloom);
          json_get_bytes(block_json, "logsBloom", &bloom_buffer);
          s->bloom_fetched = true;
        }
        else {
          // the block does not exist, so we can't prefetch the logs bloom
          log_error("failed to fetch block %l for log prefetch, since it could not be found", s->block_number);
          prover_request_free(req);
          return;
        }
        break;
      case C4_PENDING:
        if (c4_state_get_pending_request(&ctx->state)) {
          c4_start_curl_requests(req, &ctx->state);
          return;
        }
      case C4_ERROR:
        log_error("logs_cache prefetch failed for block %l: %s", s->block_number, ctx->state.error ? ctx->state.error : "(unknown)");
        prover_request_free(req);
        return;
    }
  }

  switch (eth_getBlockReceipts(ctx, json_parse(bprintf(&b, "\"0x%lx\"", s->block_number)), &recs)) {
    case C4_SUCCESS:
      c4_eth_logs_cache_add_block(s->block_number, s->logs_bloom, recs);
      prover_request_free(req);
      return;
    case C4_PENDING:
      if (c4_state_get_pending_request(&ctx->state)) {
        c4_start_curl_requests(req, &ctx->state);
        return;
      }
    // fallthrough to error if no pending request found
    case C4_ERROR:
      log_error("logs_cache prefetch failed for block %l: %s", s->block_number, ctx->state.error ? ctx->state.error : "(unknown)");
      prover_request_free(req);
      return;
  }
}

#endif

static c4_status_t handle_head(prover_ctx_t* ctx, beacon_head_t* b) {
  c4_status_t status       = C4_SUCCESS;
  char        tmp[300]     = {0};
  char        tmp2[300]    = {0};
  bytes_t     block_roots  = {0};
  bytes_t     lcu          = {0};
  uint64_t    latest_block = 0;
  ssz_ob_t    sig_block    = {0};
  ssz_ob_t    data_block   = {0};
  bytes32_t   data_root    = {0};

  // fetch the requests
  TRY_ADD_ASYNC(status, c4_eth_get_signblock_and_parent(ctx, b->root, NULL, &sig_block, &data_block, data_root));

  if (c4_watcher_check_block_number) {
    // run request to fetch the blocknumber from the execution node to make sure the execution node is cabable of handling the latest block.
    c4_status_t latest_status = eth_block_number(ctx, &latest_block);
    if (latest_status == C4_PENDING && ctx->state.requests->type == C4_DATA_TYPE_ETH_RPC) ctx->state.requests->node_exclude_mask = (uint16_t) (0xFFFF - 1); // exclude all, but the first node, because we always wnat to get the latest from the first.
    TRY_ADD_ASYNC(status, latest_status);
  }
  TRY_ASYNC(status);

  eth_block_t beacon_block = {0};
  TRY_ASYNC(c4_beacon_fill_becaon_block_from_eth(ctx, &beacon_block, data_root, data_block, sig_block));
  uint64_t timestamp           = eth_el_header_get_uint64(beacon_block.el_header, EL_TIMESTAMP);
  uint64_t beacon_block_number = eth_el_header_get_uint64(beacon_block.el_header, EL_BLOCK_NUMBER);

  c4_beacon_cache_update_blockdata(ctx, &beacon_block, c4_watcher_check_block_number ? 0 : timestamp, beacon_block.beacon.sign_parent_root);

  // now set the latest block number
  uint64_t latest_block_number = min64(beacon_block_number, c4_watcher_check_block_number ? latest_block : beacon_block_number);
  if (latest_block_number && c4_watcher_check_block_number)
    TRY_ASYNC(c4_set_latest_block(ctx, latest_block_number));

#ifdef PROVER_CACHE
  // Proactive receipts prefetch and logs-cache population (non-blocking)
  if (c4_eth_logs_cache_is_enabled()) {
    // Prefer prefetching the signed block's execution payload (latest head), not the parent data_block.
    // If not available, fall back to data_block execution payload.
    uint64_t prefetch_block_number = beacon_block_number + 1;
    if (prefetch_block_number) {
      prefetch_state_t* st = (prefetch_state_t*) safe_calloc(1, sizeof(prefetch_state_t));
      st->block_number     = prefetch_block_number;

      request_t*    preq = (request_t*) safe_calloc(1, sizeof(request_t));
      prover_ctx_t* pctx = (prover_ctx_t*) safe_calloc(1, sizeof(prover_ctx_t));
      pctx->flags        = http_server.prover_flags;
      preq->client       = NULL;
      preq->ctx          = pctx;
      pctx->chain_id     = http_server.chain_id;
      pctx->proof        = bytes(st, sizeof(prefetch_state_t)); // carry state in proof buffer
      preq->cb           = logs_prefetch_cb;
      preq->cb(preq);
    }
    else
      log_warn("No logs bloom or block number for prefetching: %l (%l)", prefetch_block_number, beacon_block_number);
  }
#endif
  // Persist current head for period store (if enabled)
  if (eth_config.period_store && !eth_config.period_master_url) {
    ssz_ob_t data_body      = ssz_get(&data_block, "body");
    uint8_t  header112[112] = {0};
    // Direkt 80 Bytes der fixen Container-Felder kopieren (slot, proposerIndex, parentRoot, stateRoot)
    memcpy(header112, ssz_get(&data_block, "slot").bytes.data, 80);
    // body_root = hash_tree_root(body)
    ssz_hash_tree_root(data_body, header112 + 80);
    c4_period_sync_on_head(beacon_block.slot, data_root, header112);
  }
  return C4_SUCCESS;
}

static void handle_new_head_cb(request_t* req) {
  if (c4_check_retry_request(req)) return; // if there are data_request in the req, we either clean it up or retry in case of an error (if possible.)
  prover_ctx_t* ctx = (prover_ctx_t*) req->ctx;

  switch (handle_head(ctx, (beacon_head_t*) ctx->proof.data)) {
    case C4_SUCCESS: {
      prover_request_free(req);
      return;
    }
    case C4_ERROR: {
      log_error("Error fetching sigblock and parent: %s", ctx->state.error);
      prover_request_free(req);
      return;
    }
    case C4_PENDING:
      if (c4_state_get_pending_request(&ctx->state)) // there are pending requests, let's take care of them first
        c4_start_curl_requests(req, &ctx->state);
      else {
        log_error("Error fetching sigblock and parent: %s", ctx->state.error);
        prover_request_free(req);
      }

      return;
  }
}

void c4_handle_new_head(json_t head) {

  beacon_head_t* b      = (beacon_head_t*) safe_calloc(1, sizeof(beacon_head_t));
  buffer_t       buffer = stack_buffer(b->root);
  b->slot               = json_get_uint64(head, "slot");
  request_t*    req     = (request_t*) safe_calloc(1, sizeof(request_t));
  prover_ctx_t* ctx     = (prover_ctx_t*) safe_calloc(1, sizeof(prover_ctx_t));
  req->client           = NULL;
  req->cb               = handle_new_head_cb;
  req->ctx              = ctx;
  ctx->chain_id         = http_server.chain_id;
  ctx->proof            = bytes(b, sizeof(beacon_head_t)); // we are misusing the proof.data for our custom pointer, to our beacon_head_t.
  ctx->client_type      = BEACON_CLIENT_EVENT_SERVER;      // make sure we use the same beacon client that actually gave us the event.
  ctx->flags            = http_server.prover_flags;
  json_get_bytes(head, "block", &buffer); // write the block root to the beacon_head_t
  handle_new_head_cb(req);
}

static void c4_handle_finalized_checkpoint_cb(request_t* req) {
  if (c4_check_retry_request(req)) return;
  prover_ctx_t* ctx        = (prover_ctx_t*) req->ctx;
  bytes32_t     checkpoint = {0};
  uint64_t      slot       = 0;

  switch (c4_eth_update_finality(ctx, checkpoint, &slot)) {
    case C4_SUCCESS: {
      if (eth_config.period_store)
        c4_period_sync_on_checkpoint(checkpoint, slot);

      prover_request_free(req);
      return;
    }
    case C4_ERROR: {
      log_error("Error fetching sigblock and parent: %s", ctx->state.error);
      prover_request_free(req);
      return;
    }
    case C4_PENDING:
      if (c4_state_get_pending_request(&ctx->state)) // there are pending requests, let's take care of them first
        c4_start_curl_requests(req, &ctx->state);
      else {
        log_error("Error fetching sigblock and parent: %s", ctx->state.error);
        prover_request_free(req);
      }
  }
}

#ifdef PROVER_CACHE
// Async callback that precomputes the Gloas LightClientBootstrap for the
// just-finalized checkpoint and stores it in the global prover cache.
//
// Runs in parallel to `c4_handle_finalized_checkpoint_cb` so a slow
// beacon/lodestar round-trip on either side does not stall the other.
//
// The `expected_block_root` anchor is carried in `ctx->proof.data`
// (repurposing the buffer that only the client-facing prover pipeline
// uses -- see `handle_new_head_cb` for the same pattern). Ownership of
// that buffer lives in `ctx->proof`, so `c4_prover_free` reclaims it.
static void c4_precompute_finalized_bootstrap_cb(request_t* req) {
  if (c4_check_retry_request(req)) return;
  prover_ctx_t* ctx        = (prover_ctx_t*) req->ctx;
  bytes32_t     block_root = {0};
  if (ctx->proof.data && ctx->proof.len == 32)
    memcpy(block_root, ctx->proof.data, 32);

  switch (c4_precompute_finalized_gloas_bootstrap(ctx, block_root)) {
    case C4_SUCCESS:
      log_info("Precomputed Gloas LightClientBootstrap cached (block=0x%b)",
               bytes(block_root, 32));
      prover_request_free(req);
      return;
    case C4_ERROR:
      // Post fork-gate, any error here is a real Lodestar/beacon issue
      // (state regen failure, event/anchor race, malformed proof). The
      // client-facing proxy still falls back to a live request, so this
      // is not fatal -- but worth flagging.
      log_warn("Bootstrap precompute failed: %s",
               ctx->state.error ? ctx->state.error : "(unknown)");
      prover_request_free(req);
      return;
    case C4_PENDING:
      if (c4_state_get_pending_request(&ctx->state)) {
        c4_start_curl_requests(req, &ctx->state);
        return;
      }
      log_warn("Bootstrap precompute stalled without pending requests: %s",
               ctx->state.error ? ctx->state.error : "(unknown)");
      prover_request_free(req);
      return;
  }
}
#endif

static void c4_precompute_finalized_bootstrap(prover_ctx_t* ctx, json_t checkpoint) {

#ifdef PROVER_CACHE
  // Fork-gate up front: the SSE payload carries the finalized epoch, and
  // `c4_chain_fork_id` is a pure lookup. Skipping the request on pre-Gloas
  // eras avoids a wasted Lodestar round-trip per finalization.
  fork_id_t fork = c4_chain_fork_id(http_server.chain_id, json_get_uint64(checkpoint, "epoch"));
  if (fork != C4_FORK_GLOAS) return;

  // The precompute relies on Lodestar's unofficial CompactMultiProof
  // endpoint (see `c4_create_gloas_bootstrap`). Skip on non-Lodestar
  // setups; the client-facing proxy already falls back to a live
  // `light_client/bootstrap` request in that case.
  if (!(http_server.prover_flags & C4_PROVER_FLAG_LODESTAR)) return;

  // Parallel precompute: pull the just-finalized bootstrap while Lodestar
  // still has the state in the fork-choice, so client light_client/bootstrap
  // requests for that root can be served from the cache instead of racing
  // Lodestar's state eviction. Best-effort: any error is logged and swallowed
  // (see `c4_precompute_finalized_bootstrap_cb`).
  request_t*    preq = (request_t*) safe_calloc(1, sizeof(request_t));
  prover_ctx_t* pctx = (prover_ctx_t*) safe_calloc(1, sizeof(prover_ctx_t));
  pctx->chain_id     = http_server.chain_id;
  pctx->client_type  = BEACON_CLIENT_EVENT_SERVER;
  pctx->flags        = http_server.prover_flags;

  // Stash the anchor from the SSE payload so the precompute can reject a
  // mid-race Lodestar reply. Uses the same "proof-as-scratchpad" trick as
  // `handle_new_head_cb`. `json_get_bytes` writes at most `allocated`
  // bytes into a fixed-size buffer, and ownership of `anchor` transfers
  // into `pctx->proof` where `c4_prover_free` reclaims it.
  uint8_t* anchor    = (uint8_t*) safe_calloc(1, 32);
  buffer_t anchor_bf = {.data = bytes(anchor, 0), .allocated = -32};
  json_get_bytes(checkpoint, "block", &anchor_bf);
  pctx->proof = bytes(anchor, 32);
  // If the SSE payload is missing/mistyped, the race defense inside
  // `c4_precompute_finalized_gloas_bootstrap` degrades to a no-op. Not
  // fatal (the cache key is derived from the fetched bootstrap header),
  // but the operator should notice repeated skips.
  if (bytes_all_zero(bytes(anchor, 32)))
    log_warn("finalized_checkpoint SSE payload missing/invalid `block` field; precompute anchor check disabled");

  preq->client = NULL;
  preq->ctx    = pctx;
  preq->cb     = c4_precompute_finalized_bootstrap_cb;
  preq->cb(preq);
#endif
}
void c4_handle_finalized_checkpoint(json_t checkpoint) {
  request_t* req                          = (request_t*) safe_calloc(1, sizeof(request_t));
  req->cb                                 = c4_handle_finalized_checkpoint_cb;
  req->ctx                                = safe_calloc(1, sizeof(prover_ctx_t));
  ((prover_ctx_t*) req->ctx)->chain_id    = http_server.chain_id;
  ((prover_ctx_t*) req->ctx)->client_type = BEACON_CLIENT_EVENT_SERVER;
  ((prover_ctx_t*) req->ctx)->flags       = http_server.prover_flags;
  req->cb(req);

  // run in parallel
  c4_precompute_finalized_bootstrap(req->ctx, checkpoint);
}
