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

#ifndef ETH_SSZ_TYPES_H
#define ETH_SSZ_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "chain_spec.h"
#include "ssz.h"

typedef enum {
  // beacon (fork-resolved via eth_ssz_type_for_fork)
  ETH_SSZ_SIGNED_BEACON_BLOCK_CONTAINER               = 1,
  ETH_SSZ_BEACON_BLOCK_BODY_CONTAINER                 = 2,
  ETH_SSZ_BEACON_BLOCK_HEADER                         = 3,
  ETH_SSZ_EXECUTION_PAYLOAD_CONTAINER                 = 4,
  ETH_SSZ_SIGNED_EXECUTION_PAYLOAD_ENVELOPE_CONTAINER = 5,

  // C4Request
  ETH_SSZ_VERIFY_REQUEST = 6,

  // C4_REQUEST_PROOFS_UNION (indices 1..9)
  ETH_SSZ_VERIFY_ACCOUNT_PROOF           = 7,  // union 1
  ETH_SSZ_VERIFY_TRANSACTION_PROOF       = 8,  // union 2
  ETH_SSZ_VERIFY_RECEIPT_PROOF           = 9,  // union 3
  ETH_SSZ_VERIFY_LOGS_PROOF              = 10, // union 4
  ETH_SSZ_VERIFY_LOGS_COMPLETENESS_PROOF = 11, // union 5
  ETH_SSZ_VERIFY_CALL_PROOF              = 12, // union 6
  ETH_SSZ_VERIFY_BLOCK_PROOF             = 13, // union 7
  ETH_SSZ_VERIFY_BLOCK_RECEIPTS_PROOF    = 14, // union 8
  ETH_SSZ_VERIFY_SYNC_PROOF              = 15, // union 9

  // C4_ETH_REQUEST_DATA_UNION
  ETH_SSZ_DATA_NONE           = 16,
  ETH_SSZ_DATA_HASH32         = 17,
  ETH_SSZ_DATA_BYTES          = 18,
  ETH_SSZ_DATA_UINT256        = 19,
  ETH_SSZ_DATA_TX             = 20,
  ETH_SSZ_DATA_RECEIPT        = 21,
  ETH_SSZ_DATA_LOGS           = 22,
  ETH_SSZ_DATA_BLOCK          = 23,
  ETH_SSZ_DATA_PROOF          = 24,
  ETH_SSZ_DATA_SIMULATION     = 25,
  ETH_SSZ_DATA_BLOCK_HEADER   = 26,
  ETH_SSZ_DATA_BLOCK_RECEIPTS = 27,

  // C4_ETH_REQUEST_SYNCDATA_UNION
  ETH_SSZ_VERIFY_LC_SYNCDATA = 28, // `LCSyncData`   (union index 1)
  ETH_SSZ_VERIFY_ZK_SYNCDATA = 29, // `ZKSyncDataV6` (union index 2)

  // Resolves to the `checkpoint` variant of `ETH_HEADER_PROOFS_UNION`
  // (structurally identical to the bootstrap union's CheckpointProof).
  ETH_SSZ_VERIFY_CHECKPOINT_PROOF = 30,

  // ETH_EL_PROOF_UNION / ETH_BLOCK_BODY_UNION
  ETH_SSZ_CL_HEADER_PROOF      = 31, // ETH_EL_PROOF_UNION index 1
  ETH_SSZ_SEQUENCER_PROOF      = 32, // ETH_EL_PROOF_UNION index 2
  ETH_SSZ_WITNESS_HEADER_PROOF = 33, // ETH_EL_PROOF_UNION index 3
  ETH_SSZ_EL_BLOCK_CONTENT     = 34, // ETH_BLOCK_BODY_UNION content variant

} eth_ssz_type_t;

const ssz_def_t* eth_ssz_type_for_fork(eth_ssz_type_t type, fork_id_t fork, chain_id_t chain_id);

// forks
const ssz_def_t* eth_ssz_type_for_denep(eth_ssz_type_t type, chain_id_t chain_id);
const ssz_def_t* eth_ssz_type_for_electra(eth_ssz_type_t type, chain_id_t chain_id);
const ssz_def_t* eth_ssz_type_for_gloas(eth_ssz_type_t type, chain_id_t chain_id);

#ifdef PROVER
/**
 * Returns the SSZ container definition for the execution payload of the given chain.
 * The returned pointer references the `executionPayload` entry inside the
 * `BeaconBlockBody` container, so it carries the correct container name and child layout.
 *
 * @param chain_id the chain to resolve (Gnosis chains get `GNOSIS_EXECUTION_PAYLOAD`)
 * @return pointer to the `ssz_def_t` container (never NULL for known chains)
 */
const ssz_def_t* c4_eth_execution_payload_def(chain_id_t chain_id);
#endif
const ssz_def_t* eth_get_light_client_update(fork_id_t fork);
const ssz_def_t* eth_get_light_client_bootstrap(fork_id_t fork);
//  c4 specific
const ssz_def_t*       eth_ssz_verification_type(eth_ssz_type_t type);
extern const ssz_def_t ssz_transactions_bytes;
extern const ssz_def_t BEACON_BLOCK_HEADER[5];
extern const ssz_def_t LIGHT_CLIENT_HEADER[3];
extern const ssz_def_t SYNC_COMMITTEE[2];
extern const ssz_def_t SYNC_AGGREGATE[2];
extern const ssz_def_t DENEP_LIGHT_CLIENT_BOOTSTRAP[3];
extern const ssz_def_t ELECTRA_LIGHT_CLIENT_BOOTSTRAP[3];
extern const ssz_def_t DENEP_LIGHT_CLIENT_UPDATE[7];
extern const ssz_def_t ELECTRA_LIGHT_CLIENT_UPDATE[7];
extern const ssz_def_t GLOAS_LIGHT_CLIENT_HEADER[3];
extern const ssz_def_t GLOAS_LIGHT_CLIENT_BOOTSTRAP[3];
extern const ssz_def_t GLOAS_LIGHT_CLIENT_UPDATE[7];
extern const ssz_def_t DENEP_EXECUTION_PAYLOAD[17];
extern const ssz_def_t GNOSIS_EXECUTION_PAYLOAD[17];
extern const ssz_def_t DENEP_WITHDRAWAL_CONTAINER;
extern const ssz_def_t C4_ETH_REQUEST_DATA_UNION[12];
extern const ssz_def_t C4_ETH_REQUEST_SYNCDATA_UNION[4];

#define ssz_builder_for_type(typename) \
  (ssz_builder_t) { .def = eth_ssz_verification_type(typename), .fixed = (buffer_t) {.data = (bytes_t) {.data = NULL, .len = 0}, .allocated = 0}, .dynamic = (buffer_t) {.data = (bytes_t) {.data = NULL, .len = 0}, .allocated = 0} }

#define BLOCK_HEADER_FIELD_COUNT 14

#ifdef __cplusplus
}
#endif

#endif
