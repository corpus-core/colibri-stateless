/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "beacon.h"
#include "beacon_types.h"
#include "eth_req.h"
#include "handler.h"
#include "logger.h"
#include "prover/prover.h"
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

static c4_status_t handle_head(prover_ctx_t* ctx, beacon_head_t* b) {
  c4_status_t status       = C4_SUCCESS;
  char        tmp[300]     = {0};
  char        tmp2[300]    = {0};
  bytes_t     block_roots  = {0};
  bytes_t     lcu          = {0};
  json_t      latest_block = {0};
  ssz_ob_t    sig_block    = {0};
  ssz_ob_t    data_block   = {0};
  bytes32_t   data_root    = {0};

  // fetch the requests
  TRY_ADD_ASYNC(status, c4_eth_get_signblock_and_parent(ctx, b->root, NULL, &sig_block, &data_block, data_root));

  if (c4_watcher_check_block_number) {
    // run request to fetch the blocknumber from the execution node to make sure the execution node is cabable of handling the latest block.
    c4_status_t latest_status = c4_send_eth_rpc(ctx, "eth_blockNumber", "[]", 0, &latest_block);
    if (latest_status == C4_PENDING && ctx->state.requests->type == C4_DATA_TYPE_ETH_RPC) ctx->state.requests->node_exclude_mask = (uint16_t) (0xFFFF - 1); // exclude all, but the first node, because we always wnat to get the latest from the first.
    TRY_ADD_ASYNC(status, latest_status);
  }
  TRY_ASYNC(status);

  // all requests are done, let's update the latest block number
  ssz_ob_t       sig_body     = ssz_get(&sig_block, "body");
  ssz_ob_t       data_body    = ssz_get(&data_block, "body");
  beacon_block_t beacon_block = {
      .slot           = ssz_get_uint64(&data_block, "slot"),
      .header         = data_block,
      .body           = data_body,
      .execution      = ssz_get(&data_body, "executionPayload"),
      .sync_aggregate = ssz_get(&sig_body, "syncAggregate")};
  memcpy(beacon_block.data_block_root, data_root, 32);
  memcpy(beacon_block.sign_parent_root, ssz_get(&sig_block, "parentRoot").bytes.data, 32);

  c4_beacon_cache_update_blockdata(ctx, &beacon_block, c4_watcher_check_block_number ? 0 : ssz_get_uint64(&beacon_block.execution, "timestamp"), beacon_block.sign_parent_root);
  uint64_t beacon_block_number = ssz_get_uint64(&beacon_block.execution, "blockNumber");

  // now set the latest block number
  uint64_t latest_block_number = min64(beacon_block_number, c4_watcher_check_block_number ? json_as_uint64(latest_block) : beacon_block_number);
  if (latest_block_number && c4_watcher_check_block_number)
    TRY_ASYNC(c4_set_latest_block(ctx, latest_block_number));

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
  json_get_bytes(head, "block", &buffer);                  // write the block root to the beacon_head_t
  handle_new_head_cb(req);
}

static void c4_handle_finalized_checkpoint_cb(request_t* req) {
  if (c4_check_retry_request(req)) return;
  prover_ctx_t* ctx = (prover_ctx_t*) req->ctx;

  switch (c4_eth_update_finality(ctx)) {
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
  }
}

void c4_handle_finalized_checkpoint(json_t checkpoint) {
  request_t* req                          = (request_t*) safe_calloc(1, sizeof(request_t));
  req->cb                                 = c4_handle_finalized_checkpoint_cb;
  req->ctx                                = safe_calloc(1, sizeof(prover_ctx_t));
  ((prover_ctx_t*) req->ctx)->client_type = BEACON_CLIENT_EVENT_SERVER;
  req->cb(req);
}
