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

#ifndef C4_HISTORIC_PROOF_H
#define C4_HISTORIC_PROOF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "beacon.h"
#include "beacon_types.h"
#include "prover.h"
#include "ssz.h"
#include "sync_committee.h"
#include <stdbool.h>

typedef enum {
  HISTORIC_PROOF_NONE   = 0,
  HISTORIC_PROOF_DIRECT = 1,
  HISTORIC_PROOF_HEADER = 2,
} historic_proof_type_t;

typedef enum {
  C4_HISTORIC_PLAN_SYNC_AGGREGATE = 0, /**< client already has the block period */
  C4_HISTORIC_PLAN_HISTORIC       = 1, /**< historical_summaries from head; LCUs only newest → head */
  C4_HISTORIC_PLAN_LCU_FORWARD    = 2, /**< block in/after head period: LCU newest → block */
  C4_HISTORIC_PLAN_LCU_GAP        = 3, /**< no verified marker: LCU fallback from an older client period */
  C4_HISTORIC_PLAN_ERROR          = 4  /**< block older than every client period and historic is not ready */
} c4_historic_plan_t;

typedef struct {
  uint8_t*             checkpoint;        // if no ZERO, the checkpoint used by the verifier
  uint64_t             checkpoint_period; // the period extracted from the bootstrap
  uint64_t             required_period;   // period the client must reach (head for historic, else block)
  uint64_t             oldest_period;     // oldest stored client sync-committee period
  uint64_t             newest_period;     // newest stored client sync-committee period
  uint64_t             block_period;      // period of the target block (never overwritten by required)
  uint64_t             post_sync_period;  // committee the client will have after attached LCUs / ZK
  uint64_t             head_period;       // period of beacon `latest` (0 if unknown)
  uint64_t             lcu_start_period;  // if set, fetch LCUs from this period instead of newest
  uint32_t             periods[MAX_SYNC_PERIODS];
  bool                 marker_ready; // `blocks_root.bin` present for `block_period`
  c4_state_sync_type_t status;

} syncdata_state_t;

typedef struct {
  historic_proof_type_t type;
  ssz_ob_t              sync_aggregate;
  bytes_t               historic_proof;
  gindex_t              gindex;
  bytes_t               proof_header;
  syncdata_state_t      sync;
} blockroot_proof_t;

typedef struct {
  ssz_ob_t sync_proof;
  bytes_t  signatures;
} zk_proof_data_t;
/**
 * checks whether additional data is needed in order to proof the blockroot.asm
 *
 * Additional data would be:
 *
 * - light_client_bootstrap, because the client is only having the checkpoint.asm
 * - light_client_updates, because the client's last period is older than the required period
 * - historic_proof (C field / `historic` variant), because the client's oldest period is still newer than the required period
 * - headerChain, because the sync_committee did not reach the 2/3 majority and we need to add headers in between.
 *
 * This function only fetches the data and sets it in the blockroot_proof_t if needed.
 *
 * @brief Check the blockroot proof for the given block
 * @param ctx The context of the prover
 * @param block_proof The blockroot proof holding the state
 * @param block The block to check the proof for
 * @return The status of the check
 */
c4_status_t c4_check_blockroot_proof(prover_ctx_t* ctx, blockroot_proof_t* block_proof, eth_block_t* block);

/**
 * Returns whether the client already stores `period` in `sync->periods`.
 *
 * When `periods[]` is empty, `oldest_period` and `newest_period` are treated
 * as the only known members (partially filled structs in tests / prefetch).
 *
 * @param sync populated sync-data state
 * @param period sync-committee period to look up
 * @return true if the client has keys for `period`
 */
bool c4_sync_has_period(const syncdata_state_t* sync, uint64_t period);

/**
 * Returns `max { p ∈ client periods | p ≤ block_period }`, or `0` if none.
 *
 * Used as the LCU start when historic-direct is not ready (`blocks_root.bin`
 * missing) and the client still has an older committee that can walk forward
 * to the target block.
 *
 * @param sync populated sync-data state
 * @param block_period target block's sync-committee period
 * @return best client period `≤ block_period`, or `0`
 */
uint64_t c4_sync_best_anchor(const syncdata_state_t* sync, uint64_t block_period);

/**
 * Returns whether a historic-direct (`historical_summaries`) proof should be built.
 *
 * Historic-direct is used when the client does **not** have the block's own
 * committee, the block sits in a **completed** period (`block < head`), and
 * `period_store/<B>/blocks_root.bin` is present so `blocks.ssz` matches
 * `hash_tree_root(state.block_roots)`.
 *
 * A client that already stores the block period (e.g. `{1852, 1853}` proving
 * a finalized block in 1852) must use that committee's SyncAggregate. A hole
 * in the middle (`{1850, 1853}` proving 1852) **does** use historic when the
 * marker is ready -- that is cheaper than LCUs for the gap, and the proof
 * anchors at `latest` (head committee).
 *
 * `head_period == 0` falls back to `oldest_period ?: post_sync_period` so an
 * empty client about to install a ZK/bootstrap committee still requests
 * historical_summaries for a strictly older block.
 *
 * @param sync populated sync-data state (periods + target block period)
 * @param head_period period of beacon `latest`, or `0` if not fetched yet
 * @param marker_ready true if `blocks_root.bin` for the target period exists
 * @return true if `check_historic_proof_direct` should build a proof
 */
bool c4_needs_historic_direct(const syncdata_state_t* sync, uint64_t head_period, bool marker_ready);

/**
 * Decides how to prove the target block root and which LCUs to attach.
 *
 * | Plan | When | `required` | LCU start |
 * |------|------|------------|-----------|
 * | `SYNC_AGGREGATE` | client has `B` | `B` | none |
 * | `HISTORIC` | `B ∉ P`, `B < H`, marker ready | `H` | `newest` (if `newest < H`) |
 * | `LCU_FORWARD` | `B ∉ P`, `B ≥ H` | `B` | `newest` |
 * | `LCU_GAP` | `B ∉ P`, `B < H`, no marker, anchor `≤ B` | `B` | `anchor` |
 * | `ERROR` | `B` older than every client period and no historic | — | — |
 *
 * @param sync populated sync-data state
 * @param head_period period of beacon `latest`, or `0` if unknown
 * @param marker_ready true if `blocks_root.bin` for the target period exists
 * @param out_required period the client must reach (may be NULL)
 * @param out_lcu_start if non-zero, fetch LCUs from this period instead of `newest` (may be NULL)
 * @return selected plan
 */
c4_historic_plan_t c4_plan_blockroot_proof(const syncdata_state_t* sync, uint64_t head_period, bool marker_ready,
                                           uint64_t* out_required, uint64_t* out_lcu_start);

c4_status_t c4_get_syncdata_proof(prover_ctx_t* ctx, syncdata_state_t* sync_data, ssz_builder_t* builder);
void        ssz_add_header_proof(ssz_builder_t* builder, eth_block_t* block_data, blockroot_proof_t block_proof);
void        c4_free_block_proof(blockroot_proof_t* block_proof);
c4_status_t c4_fetch_zk_proof_data(prover_ctx_t* ctx, zk_proof_data_t* zk_proof, uint64_t period);

/**
 * Fetches the raw wire bytes of `[start_period .. start_period + count - 1]`
 * `LightClientUpdate`s in Beacon-API list format (each entry is prefixed with
 * an 8-byte LE length + 4-byte ForkDigest).
 *
 * Two lookup paths are tried in order:
 *
 * 1. **Preferred (server context, `C4_PROVER_FLAG_CHAIN_STORE` set)**: read
 *    from the local `period_store` via the internal `lcu_updates` handler.
 *    That handler concatenates the per-period `lcu.ssz` files (already in
 *    Beacon-API wire format) and transparently backfills missing periods from
 *    a beacon node. It's noticeably cheaper than a dedicated beacon roundtrip
 *    when the cache is warm.
 * 2. **Fallback (always available)**: direct
 *    `GET eth/v1/beacon/light_client/updates?start_period=X&count=Y`.
 *
 * The preferred path uses `_no_throw` semantics and a per-ctx state-cache
 * marker ("LCU_FALLBACK_LOGGED") so a fallback warning is emitted **exactly
 * once** across all async re-entries of the same prover run. Short internal
 * responses (`< UPDATE_PREFIX_SIZE`) are treated the same as a transport
 * error and mark the internal `data_request_t.error` sticky, so subsequent
 * `_no_throw` calls short-circuit without re-probing the response.
 *
 * The returned `out_data->data` points into the underlying `data_request_t`
 * response buffer and is owned by `ctx->state`; the caller **must not** free
 * it.
 *
 * @param ctx prover context (must not be NULL)
 * @param start_period first period to fetch (inclusive)
 * @param count number of consecutive periods to fetch; forwarded verbatim
 *              into the beacon-API `count` query parameter. A value of `0`
 *              is not rejected but typically yields an empty list.
 * @param out_data on success: raw wire bytes referencing the request
 *                 response. Ownership stays with `ctx->state`; do **not**
 *                 free. Must not be NULL.
 * @return `C4_PENDING` while any underlying request is in flight,
 *         `C4_SUCCESS` when `out_data` is ready, `C4_ERROR` on transport
 *         failure (both paths). On error, `ctx->state.error` is set.
 */
c4_status_t c4_fetch_client_updates(prover_ctx_t* ctx, uint64_t start_period, uint32_t count, bytes_t* out_data);

#ifdef TEST
/**
 * Test helper: enqueue the historical_summaries Beacon request for `block`.
 * Default path is Lodestar; `C4_PROVER_FLAG_NIMBUS` selects the Nimbus URL.
 *
 * @param ctx prover context
 * @param block beacon block whose `beacon.cl_header.stateRoot` is used in the path
 * @param history_proof output JSON, filled after the request is fulfilled
 * @return `C4_PENDING` until the request is fulfilled, then the send status
 */
c4_status_t c4_test_get_historical_summaries(prover_ctx_t* ctx, eth_block_t* block, json_t* history_proof);
#endif

#ifdef __cplusplus
}
#endif

#endif