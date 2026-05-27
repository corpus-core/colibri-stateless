/*
 * Copyright (c) 2025 corpus.core
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include "../server/eth_clients.h"
#include "../zk_verifier/zk_verifier_constants.h"
#include "beacon.h"
#include "beacon_types.h"
#include "eth_req.h"
#include "eth_tools.h"
#include "historic_proof.h"
#include "json.h"
#include "logger.h"
#include "prover.h"
#include "ssz.h"
#include "sync_committee.h"
#include "version.h"
#include <inttypes.h> // Include this header for PRIu64 and PRIx64
#include <stdlib.h>
#include <string.h>
#define MAX_SIGNATURES   5
#define SIGNATURE_LENGTH 65

// Try to choose a "checkpoint" snapshot slot from the server-side `snapshots.idx`
// index file. The index lists slots for which the server has built a
// `zk_proof_checkpoint_${slot}.ssz` snapshot (historic_proof variant). We
// pick the largest slot that is already finalized (<= fin_slot) to maximize
// the chance that checkpointz has the corresponding canonical root.
//
// Witness-key setups always fall back to the legacy `zk_proof.ssz` because the
// current verifier requires `header_proof` for witness BLS verification.
//
// Returns:
//   - C4_PENDING: a request was enqueued, caller should propagate.
//   - C4_SUCCESS: out_slot set to chosen snapshot slot, or 0 for legacy fallback.
//   - C4_ERROR is never returned; index-not-found is treated as fallback.
static c4_status_t choose_snapshot_slot(prover_ctx_t* ctx, uint64_t period, uint64_t fin_slot, uint64_t* out_slot) {
  *out_slot = 0;
  if (ctx->witness_key.len) return C4_SUCCESS; // witness deployments stay on legacy header_proof

  char     path_buf[200] = {0};
  buffer_t path          = stack_buffer(path_buf);
  bytes_t  idx_bytes     = {0};

  // Suppress index-not-found errors: an absent snapshots.idx simply means the
  // server has not built any historic_proof snapshots yet (cold start, witness
  // setup, no historical_summaries) and we should serve legacy `zk_proof.ssz`.
  char*       saved_err  = ctx->state.error;
  ctx->state.error       = NULL;
  c4_status_t idx_status = c4_send_internal_request(ctx, bprintf(&path, "period_store/%l/snapshots.idx", period), NULL, 0, &idx_bytes);

  if (idx_status == C4_PENDING) {
    ctx->state.error = saved_err;
    return C4_PENDING;
  }

  if (idx_status == C4_SUCCESS && idx_bytes.data && idx_bytes.len >= 4) {
    uint32_t count = uint32_from_le(idx_bytes.data);
    // Defensive: declared count must fit into the actual response payload.
    if ((size_t) 4 + (size_t) count * 8 <= idx_bytes.len) {
      for (uint32_t i = 0; i < count; i++) {
        uint64_t s = uint64_from_le(idx_bytes.data + 4 + (size_t) i * 8);
        if (s <= fin_slot && s > *out_slot) *out_slot = s;
      }
    }
  }

  // Clear any error from the index lookup; absence is not fatal.
  if (ctx->state.error) {
    safe_free(ctx->state.error);
    ctx->state.error = NULL;
  }
  ctx->state.error = saved_err;
  return C4_SUCCESS;
}

c4_status_t c4_fetch_zk_proof_data(prover_ctx_t* ctx, zk_proof_data_t* zk_proof, uint64_t period) {
  c4_status_t status                                        = C4_SUCCESS;
  char        sig_buffer[MAX_SIGNATURES * SIGNATURE_LENGTH] = {0};
  buffer_t    signatures                                    = stack_buffer(sig_buffer);
  char        buffer[1000]                                  = {0};
  buffer_t    buf                                           = stack_buffer(buffer);
  zk_proof->sync_proof.def                                  = C4_ETH_REQUEST_SYNCDATA_UNION + 2;

  // Determine the latest finalized slot via the well-tested cache/fallback path.
  // Survives restarts: if the cache is cold, this falls back to a beacon API request.
  beacon_block_t fin = {0};
  TRY_ASYNC(c4_beacon_get_block_for_eth(ctx, json_parse("\"finalized\""), &fin));

  // Pick a snapshot slot from snapshots.idx (server-built historic_proof anchors)
  // or fall back to the legacy header_proof snapshot when none is available.
  uint64_t chosen_slot = 0;
  TRY_ASYNC(choose_snapshot_slot(ctx, period, fin.slot, &chosen_slot));

  buffer_reset(&buf);
  if (chosen_slot)
    bprintf(&buf, "period_store/%l/zk_proof_checkpoint_%l.ssz", period, chosen_slot);
  else
    bprintf(&buf, "period_store/%l/zk_proof.ssz", period);
  TRY_ADD_ASYNC(status, c4_send_internal_request(ctx, (char*) buf.data.data, NULL, 0, &zk_proof->sync_proof.bytes));

  if (ctx->witness_key.len && ctx->witness_key.len % 20 == 0) {
    for (int i = 0; i < (int) ctx->witness_key.len; i += 20) {
      buffer_reset(&buf);
      bytes_t     sig_data   = {0};
      c4_status_t sig_status = c4_send_internal_request(ctx, bprintf(&buf, "period_store/%l/sig_%x", period, bytes(ctx->witness_key.data + i, 20)), NULL, 0, &sig_data);
      if (sig_status == C4_ERROR) THROW_ERROR_WITH("There is no signature from 0x%x for period %l", bytes(ctx->witness_key.data + i, 20), period);
      TRY_ADD_ASYNC(status, sig_status);
      buffer_append(&signatures, sig_data);
    }
  }
  if (status == C4_SUCCESS)
    zk_proof->signatures = signatures.data.len ? bytes_dup(signatures.data) : NULL_BYTES;

  return status;
}