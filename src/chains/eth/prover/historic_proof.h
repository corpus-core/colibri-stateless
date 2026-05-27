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
#include "prover.h"
#include "ssz.h"
#include "sync_committee.h"

typedef enum {
  HISTORIC_PROOF_NONE   = 0,
  HISTORIC_PROOF_DIRECT = 1,
  HISTORIC_PROOF_HEADER = 2,
} historic_proof_type_t;

typedef struct {
  uint8_t*             checkpoint;        // if no ZERO, the checkpoint used by the verifier
  uint64_t             checkpoint_period; // the period extracted from the bootstrap
  uint64_t             required_period;   // latest_period  required
  uint64_t             oldest_period;     // current period used by the the verifier
  uint64_t             newest_period;     // current period used by the the verifier
  c4_state_sync_type_t status;            // the status of the

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
 * - historic_proof, because the client's oldest period is still newer than the required period
 * - header_proof, because the sync_committee did not reach the 2/3 majority and we need to add headers in between.
 *
 * This function only fetches the data and sets it in the blockroot_proof_t if needed.
 *
 * @brief Check the blockroot proof for the given block
 * @param ctx The context of the prover
 * @param block_proof The blockroot proof holding the state
 * @param block The block to check the proof for
 * @return The status of the check
 */
c4_status_t c4_check_blockroot_proof(prover_ctx_t* ctx, blockroot_proof_t* block_proof, beacon_block_t* block);

c4_status_t c4_get_syncdata_proof(prover_ctx_t* ctx, syncdata_state_t* sync_data, ssz_builder_t* builder);
void        ssz_add_header_proof(ssz_builder_t* builder, beacon_block_t* block_data, blockroot_proof_t block_proof);
void        c4_free_block_proof(blockroot_proof_t* block_proof);
c4_status_t c4_fetch_zk_proof_data(prover_ctx_t* ctx, zk_proof_data_t* zk_proof, uint64_t period);

/**
 * Builds the concatenated merkle proof from a block root in `block_period` to
 * the `state_root` of a recent beacon state, using `historical_summaries` of
 * that recent state.
 *
 * The proof concatenates three single-leaf proofs:
 *   1. blocks_roots[block_idx]              -> hash_tree_root(blocks_roots)
 *   2. summaries[summary_idx].block_summary_root -> hash_tree_root(summaries)
 *   3. summaries_root                        -> recent state_root (provided by Lodestar)
 *
 * The combined gindex is `summaries_field_gidx ++ period_summary_gidx ++ block_idx_gidx`.
 *
 * Caller owns `out_proof->data` after success. On error, no allocation is leaked.
 *
 * Designed to be callable from both the prover (sync historic proof building)
 * and the server (snapshot pre-building). The `state` parameter is optional;
 * when `NULL`, errors are logged via `log_warn` instead of being stored.
 *
 * @param chain_id          Chain identifier (used for fork epoch lookup).
 * @param state             Optional state for error reporting (may be `NULL`).
 * @param block_period      Period containing the block whose root we anchor.
 * @param block_idx         Slot index within the period (slot % 8192).
 * @param blocks_roots      SSZ `Vector[bytes32, 8192]` of block roots for `block_period`.
 * @param history_proof     JSON from Lodestar `/eth/v1/lodestar/states/{state_id}/historical_summaries`.
 * @param recent_state_slot Slot of the recent state we anchor against (determines fork id).
 * @param out_proof         OUT: concatenated merkle proof bytes (heap allocated, caller frees `.data`).
 * @param out_gindex        OUT: combined generalized index.
 * @return `C4_SUCCESS` on success, `C4_ERROR` on failure.
 */
c4_status_t c4_build_historic_merkle_proof(
    chain_id_t  chain_id,
    c4_state_t* state,
    uint64_t    block_period,
    uint64_t    block_idx,
    bytes_t     blocks_roots,
    json_t      history_proof,
    uint64_t    recent_state_slot,
    bytes_t*    out_proof,
    gindex_t*   out_gindex);
#ifdef __cplusplus
}
#endif

#endif