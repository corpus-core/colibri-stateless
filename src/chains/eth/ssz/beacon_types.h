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

#include "chains.h"
#include "common.h"
#include "ssz.h"

typedef enum {
  C4_FORK_PHASE0    = 0,
  C4_FORK_ALTAIR    = 1,
  C4_FORK_BELLATRIX = 2,
  C4_FORK_CAPELLA   = 3,
  C4_FORK_DENEB     = 4,
  C4_FORK_ELECTRA   = 5,
  C4_FORK_FULU      = 6,

  C4_FORK_INVALID = -1
} fork_id_t;

typedef enum {
  // beacon
  ETH_SSZ_SIGNED_BEACON_BLOCK_CONTAINER = 1,
  ETH_SSZ_BEACON_BLOCK_BODY_CONTAINER   = 2,
  ETH_SSZ_BEACON_BLOCK_HEADER           = 3,
  // verify
  ETH_SSZ_VERIFY_REQUEST           = 4,
  ETH_SSZ_VERIFY_BLOCK_HASH_PROOF  = 5,
  ETH_SSZ_VERIFY_ACCOUNT_PROOF     = 6,
  ETH_SSZ_VERIFY_TRANSACTION_PROOF = 7,
  ETH_SSZ_VERIFY_RECEIPT_PROOF     = 8,
  ETH_SSZ_VERIFY_LOGS_PROOF        = 9,
  //  ETH_SSZ_VERIFY_LIGHT_CLIENT_UPDATE_LIST = 10,
  //  ETH_SSZ_VERIFY_LIGHT_CLIENT_UPDATE      = 11,
  ETH_SSZ_VERIFY_STATE_PROOF        = 12,
  ETH_SSZ_VERIFY_CALL_PROOF         = 13,
  ETH_SSZ_VERIFY_SYNC_PROOF         = 14,
  ETH_SSZ_VERIFY_BLOCK_PROOF        = 15,
  ETH_SSZ_VERIFY_BLOCK_NUMBER_PROOF = 16,
  ETH_SSZ_VERIFY_WITNESS_PROOF      = 17,
  // data types
  ETH_SSZ_DATA_NONE       = 18,
  ETH_SSZ_DATA_HASH32     = 19,
  ETH_SSZ_DATA_BYTES      = 20,
  ETH_SSZ_DATA_UINT256    = 21,
  ETH_SSZ_DATA_TX         = 22,
  ETH_SSZ_DATA_RECEIPT    = 23,
  ETH_SSZ_DATA_LOGS       = 24,
  ETH_SSZ_DATA_BLOCK      = 25,
  ETH_SSZ_DATA_PROOF      = 26,
  ETH_SSZ_DATA_SIMULATION = 27,

  ETH_SSZ_VERIFY_BLOCK_HEADER_PROOF = 28,
  ETH_SSZ_DATA_BLOCK_HEADER         = 29,
  ETH_SSZ_DATA_CALL_BLOCK_CONTEXT   = 30,

  ETH_SSZ_VERIFY_BLOCK_RECEIPTS_PROOF = 31,
  ETH_SSZ_DATA_BLOCK_RECEIPTS         = 32,

  // hybrid proof types (header_data embedded, no consensus proof needed)
  ETH_SSZ_VERIFY_HYBRID_ACCOUNT_PROOF      = 33,
  ETH_SSZ_VERIFY_HYBRID_TRANSACTION_PROOF  = 34,
  ETH_SSZ_VERIFY_HYBRID_RECEIPT_PROOF      = 35,
  ETH_SSZ_VERIFY_HYBRID_LOGS_PROOF         = 36,
  ETH_SSZ_VERIFY_HYBRID_CALL_PROOF         = 37,
  ETH_SSZ_VERIFY_HYBRID_BLOCK_PROOF        = 38,
  ETH_SSZ_VERIFY_HYBRID_BLOCK_HEADER_PROOF  = 39,
  ETH_SSZ_VERIFY_HYBRID_BLOCK_RECEIPTS_PROOF = 40,

  // beacon container types (chain- and fork-aware, resolved via eth_ssz_type_for_fork)
  ETH_SSZ_EXECUTION_PAYLOAD_CONTAINER = 42,

  // Resolves to the `CheckpointProof` variant of `ETH_HEADER_PROOFS_UNION` (which is
  // structurally identical to the `CheckpointProof` variant of
  // `C4_ETH_SYNCDATA_BOOTSTRAP_UNION`). Single source of truth so the prover can build
  // a CheckpointProof SSZ blob using the same definition the verifier reads.
  ETH_SSZ_VERIFY_CHECKPOINT_PROOF = 43,

  // Resolves to the `timestamp` variant of `ETH_STATE_BLOCK_UNION` (UINT64). Used
  // by the verifier to distinguish the new account-`latest` freshness leaf from
  // the `blockNumber` variant (both are 8 bytes long).
  ETH_SSZ_DATA_STATE_BLOCK_TIMESTAMP = 44,

  // `C4_ETH_REQUEST_SYNCDATA_UNION` variants (named to avoid raw pointer arithmetic
  // on the union array at the call sites).
  ETH_SSZ_VERIFY_LC_SYNCDATA    = 45, // `LCSyncData`   (union index 1): LightClient sync data
  ETH_SSZ_VERIFY_ZK_SYNCDATA    = 46, // `ZKSyncData`   (union index 2): legacy SP1 v5 ZK sync data, 260-byte proof
  ETH_SSZ_VERIFY_ZK_SYNCDATA_V6 = 47  // `ZKSyncDataV6` (union index 3): SP1 v6 ZK sync data, 356-byte proof

} eth_ssz_type_t;

// functionpointer for a function calculating the fork version from chain_id, fork and target bytes
typedef void (*fork_version_func_t)(chain_id_t chain_id, fork_id_t fork, uint8_t* version);

typedef struct {
  chain_id_t          chain_id;
  const uint64_t*     fork_epochs;
  const bytes32_t     genesis_validators_root;
  const bytes32_t     zk_sync_keys_root;        // initial zk sync keys root
  const int           slots_per_epoch_bits;     // 5 = 32 slots per epoch
  const int           epochs_per_period_bits;   // 8 = 256 epochs per period
  const uint64_t      weak_subjectivity_epochs; // max epochs before checkpoint validation required
  fork_version_func_t fork_version_func;
} chain_spec_t;

bool                c4_chain_genesis_validators_root(chain_id_t chain_id, bytes32_t genesis_validators_root);
fork_id_t           c4_chain_fork_id(chain_id_t chain_id, uint64_t epoch);
const chain_spec_t* c4_eth_get_chain_spec(chain_id_t id);
const ssz_def_t*    eth_ssz_type_for_fork(eth_ssz_type_t type, fork_id_t fork, chain_id_t chain_id);

// forks
const ssz_def_t* eth_ssz_type_for_denep(eth_ssz_type_t type, chain_id_t chain_id);
const ssz_def_t* eth_ssz_type_for_electra(eth_ssz_type_t type, chain_id_t chain_id);

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
extern const ssz_def_t DENEP_EXECUTION_PAYLOAD[17];
extern const ssz_def_t GNOSIS_EXECUTION_PAYLOAD[17];
extern const ssz_def_t DENEP_WITHDRAWAL_CONTAINER;
extern const ssz_def_t ELECTRA_EXECUTION_PAYLOAD[17];
extern const ssz_def_t ELECTRA_WITHDRAWAL_CONTAINER;
extern const ssz_def_t C4_ETH_REQUEST_DATA_UNION[12];
extern const ssz_def_t C4_ETH_REQUEST_SYNCDATA_UNION[4];

#define epoch_for_slot(slot, chain_spec)  ((slot) >> (chain_spec ? chain_spec->slots_per_epoch_bits : 5))
#define period_for_slot(slot, chain_spec) ((slot) >> (chain_spec ? (chain_spec->epochs_per_period_bits + chain_spec->slots_per_epoch_bits) : 13))

#define slot_for_epoch(epoch, chain_spec)   ((epoch) << (chain_spec ? chain_spec->slots_per_epoch_bits : 5))
#define slot_for_period(period, chain_spec) ((period) << (chain_spec ? (chain_spec->epochs_per_period_bits + chain_spec->slots_per_epoch_bits) : 13))

#define ssz_builder_for_type(typename) \
  (ssz_builder_t) { .def = eth_ssz_verification_type(typename), .fixed = (buffer_t) {.data = (bytes_t) {.data = NULL, .len = 0}, .allocated = 0}, .dynamic = (buffer_t) {.data = (bytes_t) {.data = NULL, .len = 0}, .allocated = 0} }

inline static bool is_gnosis_chain(chain_id_t chain_id) {
  return chain_id == C4_CHAIN_GNOSIS || chain_id == C4_CHAIN_GNOSIS_CHIADO;
}

#define BLOCK_HEADER_FIELD_COUNT 14
const gindex_t* c4_block_header_gindexes(chain_id_t chain_id, uint64_t slot);

/** Number of leaves in the call state proof when block context is included (stateRoot + 8 execution payload fields). */
#define CALL_BLOCK_CONTEXT_FIELD_COUNT 9
/** Gindexes for state proof + block context: stateRoot, blockNumber, timestamp, feeRecipient, prevRandao, baseFeePerGas, blockHash, gasLimit, excessBlobGas. */
const gindex_t* c4_call_block_context_gindexes(void);

#endif
