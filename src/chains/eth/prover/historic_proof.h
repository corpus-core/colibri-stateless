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
#include "version.h"

// First client version that consumes SP1 v6 ("Hypercube") proofs. Clients below this
// version keep receiving the legacy v5 `zk_proof.ssz` (`ZKSyncData`, 260-byte proof);
// from this version on the prover serves the `zk_proof_v6.ssz` (`ZKSyncDataV6`,
// 356-byte proof). Keep the numeric gate and the string in sync (gate vs. message text).
#define C4_ZK_FIRST_V6_VERSION     c4_version_number(2, 0, 0)
#define C4_ZK_FIRST_V6_VERSION_STR "2.0.0"

/**
 * The `eth_ssz_verification_type` selector for the ZK sync data variant matching the
 * given client version: `ETH_SSZ_VERIFY_ZK_SYNCDATA_V6` (356-byte proof) for clients
 * `>= 2.0.0`, the legacy `ETH_SSZ_VERIFY_ZK_SYNCDATA` (260-byte proof) otherwise. A
 * version of `0` (unknown / pre-versioning) counts as legacy.
 *
 * @param version The client version as returned by `c4_version_number`.
 * @return The `eth_ssz_type_t` selector for the matching union variant.
 */
static inline eth_ssz_type_t c4_zk_syncdata_type(uint32_t version) {
  return version >= C4_ZK_FIRST_V6_VERSION ? ETH_SSZ_VERIFY_ZK_SYNCDATA_V6 : ETH_SSZ_VERIFY_ZK_SYNCDATA;
}

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
  uint64_t             block_period;      // the period of the target block
  uint64_t             post_sync_period;  // the period of the target block after the sync period
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
c4_status_t c4_check_blockroot_proof(prover_ctx_t* ctx, blockroot_proof_t* block_proof, eth_block_t* block);

c4_status_t c4_get_syncdata_proof(prover_ctx_t* ctx, syncdata_state_t* sync_data, ssz_builder_t* builder);
void        ssz_add_header_proof(ssz_builder_t* builder, eth_block_t* block_data, blockroot_proof_t block_proof);
void        c4_free_block_proof(blockroot_proof_t* block_proof);
c4_status_t c4_fetch_zk_proof_data(prover_ctx_t* ctx, zk_proof_data_t* zk_proof, uint64_t period);

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