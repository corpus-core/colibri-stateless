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
  C4_FORK_GLOAS     = 7,

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
  ETH_SSZ_VERIFY_CALL_PROOF        = 13,
  ETH_SSZ_VERIFY_SYNC_PROOF        = 14,
  ETH_SSZ_VERIFY_BLOCK_PROOF       = 15,
  ETH_SSZ_VERIFY_WITNESS_PROOF     = 17,

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

  // (28 was ETH_SSZ_VERIFY_BLOCK_HEADER_PROOF: header-only proofs now use
  //  ETH_SSZ_VERIFY_BLOCK_PROOF with the NONE variant of ETH_BLOCK_BODY_UNION)
  ETH_SSZ_DATA_BLOCK_HEADER = 29,
  // 30 was ETH_SSZ_DATA_CALL_BLOCK_CONTEXT (compact EVM header via SSZ multi-proof;
  // eth_call now reads block context from the verified RLP EL header)

  ETH_SSZ_VERIFY_BLOCK_RECEIPTS_PROOF = 31,
  ETH_SSZ_DATA_BLOCK_RECEIPTS         = 32,

  // beacon container types (chain- and fork-aware, resolved via eth_ssz_type_for_fork)
  ETH_SSZ_EXECUTION_PAYLOAD_CONTAINER = 42,

  // Resolves to the `CheckpointProof` variant of `ETH_HEADER_PROOFS_UNION` (which is
  // structurally identical to the `CheckpointProof` variant of
  // `C4_ETH_SYNCDATA_BOOTSTRAP_UNION`). Single source of truth so the prover can build
  // a CheckpointProof SSZ blob using the same definition the verifier reads.
  ETH_SSZ_VERIFY_CHECKPOINT_PROOF = 43,

  // 44 was ETH_SSZ_DATA_STATE_BLOCK_TIMESTAMP (timestamp-only variant of the
  // removed ETH_STATE_BLOCK_UNION; freshness now reads timestamp from the RLP EL header)

  // `C4_ETH_REQUEST_SYNCDATA_UNION` variants (named to avoid raw pointer arithmetic
  // on the union array at the call sites).
  ETH_SSZ_VERIFY_LC_SYNCDATA    = 45, // `LCSyncData`   (union index 1): LightClient sync data
  ETH_SSZ_VERIFY_ZK_SYNCDATA_V6 = 46, // `ZKSyncDataV6` (union index 2): 356-byte Groth16 ZK sync data

  ETH_SSZ_VERIFY_LOGS_COMPLETENESS_PROOF = 48, // `LogsCompletenessProof` (proof union index 20): completeness proof for eth_getLogs

  ETH_SSZ_CL_BLOCK_PROOF   = 49, // ETH_CL_BLOCK_PROOF
  ETH_SSZ_EL_BLOCK_CONTENT = 50, // ETH_EL_BLOCK_CONTENT

  ETH_SSZ_SIGNED_EXECUTION_PAYLOAD_ENVELOPE_CONTAINER = 51, // ETH_SSZ_SIGNED_EXECUTION_PAYLOAD_ENVELOPE_CONTAINER
  ETH_SSZ_SEQUENCER_PROOF                             = 52, // ETH_SEQUENCER_PROOF (ETH_BLOCK_PROOF_UNION index 2)

} eth_ssz_type_t;

// functionpointer for a function calculating the fork version from chain_id, fork and target bytes
typedef void (*fork_version_func_t)(chain_id_t chain_id, fork_id_t fork, uint8_t* version);

// EIP-7892 (Blob Parameter Only) schedule entry. `activation_timestamp` is the
// EL block timestamp at which the fork's `BLOB_BASE_FEE_UPDATE_FRACTION` takes
// effect; per go-ethereum's `params/config.go` all post-Merge forks are
// timestamp-based (not block-based) because PoS aligns slots to real time.
// Tables are terminated by an entry with `activation_timestamp == 0`.
typedef struct {
  uint64_t activation_timestamp;
  uint64_t update_fraction;
} eth_blob_schedule_t;

/**
 * Consensus-layer `BLOB_SCHEDULE` entry (EIP-7892 / Fulu).
 *
 * Used by `compute_fork_digest` after `FULU_FORK_EPOCH`: the digest is
 * `xor(base_digest, sha256(epoch || max_blobs_per_block))[:4]`. Distinct
 * from `eth_blob_schedule_t`, which carries the execution-layer
 * `BLOB_BASE_FEE_UPDATE_FRACTION` keyed by timestamp.
 *
 * Tables are ASCENDING by epoch and terminated by `{0, 0}`.
 */
typedef struct {
  uint64_t epoch;
  uint64_t max_blobs_per_block;
} eth_cl_blob_schedule_t;

typedef struct {
  chain_id_t                    chain_id;
  const uint64_t*               fork_epochs;
  const bytes32_t               genesis_validators_root;
  const bytes32_t               zk_sync_keys_root;        // initial zk sync keys root
  const int                     slots_per_epoch_bits;     // 5 = 32 slots per epoch
  const int                     epochs_per_period_bits;   // 8 = 256 epochs per period
  const uint64_t                weak_subjectivity_epochs; // max epochs before checkpoint validation required
  fork_version_func_t           fork_version_func;
  const eth_blob_schedule_t*    blob_schedule;            // EIP-7892 EL schedule, DESCENDING by timestamp, {0,0}-terminated; NULL uses Cancun default
  uint64_t                      min_blob_base_fee;           // MIN_BLOB_BASE_FEE (`minBlobGasPrice`); 0 uses Ethereum's default (1 wei). Gnosis / Chiado use 1e9.
  const eth_cl_blob_schedule_t* cl_blob_schedule;            // EIP-7892 CL BLOB_SCHEDULE, ASCENDING by epoch, {0,0}-terminated; NULL = no BPO entries
  uint64_t                      electra_max_blobs_per_block; // `MAX_BLOBS_PER_BLOCK_ELECTRA` used as the Fulu digest fallback when `BLOB_SCHEDULE` has no matching entry. 0 = Ethereum default (9). Gnosis / Chiado use 2.
} chain_spec_t;

bool      c4_chain_genesis_validators_root(chain_id_t chain_id, bytes32_t genesis_validators_root);
fork_id_t c4_chain_fork_id(chain_id_t chain_id, uint64_t epoch);
/**
 * Returns true if the chain has assigned an activation epoch to `fork`
 * (as opposed to leaving it unscheduled). Phase0 is genesis and always
 * returns false because it is not listed in `fork_epochs`.
 *
 * @param chain_id chain to inspect
 * @param fork fork id (Altair or later)
 * @return true if the fork is on the chain's schedule
 */
bool                c4_chain_schedules_fork(chain_id_t chain_id, fork_id_t fork);
/**
 * Activation epoch of `fork` on `chain_id`.
 *
 * @param chain_id chain to inspect
 * @param fork fork id (Altair or later; Phase0 returns 0)
 * @return the activation epoch, or `UINT64_MAX` if the fork is unscheduled
 *         or the chain is unknown
 */
uint64_t            c4_chain_fork_epoch(chain_id_t chain_id, fork_id_t fork);
const chain_spec_t* c4_eth_get_chain_spec(chain_id_t id);
const ssz_def_t*    eth_ssz_type_for_fork(eth_ssz_type_t type, fork_id_t fork, chain_id_t chain_id);

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

/**
 * Returns the generalized index of `current_sync_committee` within `BeaconState` for the fork active at `slot`.
 *
 * The gindex depends on the BeaconState layout, which changes with each fork:
 * - Deneb:   54
 * - Electra: 86 (Fulu keeps the Electra layout for these fields)
 * - Gloas:   2945 (BeaconState becomes a `ProgressiveContainer`)
 *
 * @param chain_id Chain identifier used to look up fork epochs
 * @param slot Beacon slot; used to derive the epoch and thus the active fork
 * @return Generalized index used to build/verify the sync-committee Merkle proof
 */
gindex_t c4_current_sync_committee_gindex(chain_id_t chain_id, uint64_t slot);

/**
 * Returns the generalized index of `next_sync_committee` (the SyncCommittee
 * container) within `BeaconState` for the fork active at `slot`.
 *
 * That leaf is `hash(pubkeys_root, aggregatePubkey_root)`, not the 512-key
 * vector. ZK sync proofs prove `.pubkeys` (left child, gindex `* 2`).
 *
 * The gindex depends on the BeaconState layout:
 * - Deneb:   55
 * - Electra: 87 (Fulu keeps the Electra layout for these fields)
 * - Gloas:   2946
 *
 * @param chain_id Chain identifier used to look up fork epochs
 * @param slot Beacon slot; used to derive the epoch and thus the active fork
 * @return Generalized index used to build/verify the next-sync-committee Merkle proof
 */
gindex_t c4_next_sync_committee_gindex(chain_id_t chain_id, uint64_t slot);

/**
 * Returns the generalized index of `finalized_checkpoint.root` within `BeaconState` for the fork active at `slot`.
 *
 * The gindex depends on the BeaconState layout:
 * - Deneb:   105
 * - Electra: 169 (Fulu keeps the Electra layout for these fields)
 * - Gloas:   735
 *
 * @param chain_id Chain identifier used to look up fork epochs
 * @param slot Beacon slot; used to derive the epoch and thus the active fork
 * @return Generalized index used to build/verify the finality Merkle proof
 */
gindex_t c4_finalized_root_gindex(chain_id_t chain_id, uint64_t slot);

/**
 * Returns the generalized index of the `historical_summaries` field within `BeaconState`
 * for the fork active at `slot`.
 *
 * `historical_summaries` is field 27 of `BeaconState` (unchanged since Capella). EIP-7688
 * deliberately keeps it as a classical `List[HistoricalSummary, HISTORICAL_ROOTS_LIMIT]`
 * so existing verifiers can continue to prove against the same list `hash_tree_root`. Only
 * the outer embedding changes with Gloas, where `BeaconState` becomes a `ProgressiveContainer`:
 * - Capella/Deneb: 32 + 27 = 59
 * - Electra/Fulu:  64 + 27 = 91
 * - Gloas:         2950 (progressive chunk gindex of field 27, mixed under active_fields)
 *
 * @param chain_id Chain identifier used to look up fork epochs
 * @param slot Beacon slot; used to derive the epoch and thus the active fork
 * @return Generalized index of the `historical_summaries` list root within `BeaconState`
 */
gindex_t c4_historical_summaries_gindex(chain_id_t chain_id, uint64_t slot);

/**
 * Returns the generalized index within `BeaconBlockBody` of the leaf that the
 * CL block-hash proof (`ETH_CL_BLOCK_PROOF`) anchors against for the fork active
 * at `slot`. Both the prover (when building the branch) and the verifier (when
 * checking it) resolve the gindex through this helper, so the leaf position is
 * bound and cannot be swapped out by a crafted proof.
 *
 * The leaf differs by fork -- both anchors are "safe" in the sense that they
 * require the signed head to be canonical, but they identify different EL blocks:
 * - Deneb / Electra / Fulu: `execution_payload.block_hash` (gindex 812).
 *   Proves the EL block of the CURRENT beacon slot.
 * - Gloas (EIP-7732): `signed_execution_payload_bid.message.parent_block_hash`
 *   (gindex 2856). Under ePBS the current-slot payload is not yet executed at
 *   proposal time; the bid instead commits to the PARENT (head-1) EL block.
 *
 * @param chain_id chain identifier (drives chain spec + fork lookup)
 * @param slot beacon slot; drives the active fork
 * @return generalized index of the EL block-hash leaf inside `BeaconBlockBody`,
 *         or 0 if the fork is unknown / no CL block proof is defined
 */
gindex_t c4_execution_block_hash_gindex(chain_id_t chain_id, uint64_t slot);

/**
 * Computes the expected combined generalized index for a historic-direct block
 * inclusion proof: target `block_root` -> `HistoricalSummary.block_summary_root`
 * -> `historical_summaries` list root -> `BeaconState` root.
 *
 * This is the single source of truth for what a well-formed
 * `HISTORIC_PROOF_DIRECT` MUST hash against. Both the prover (when building the
 * proof) and the verifier (when validating it) resolve the gindex through this
 * helper -- so a proof cannot smuggle in a chosen gindex that happens to point
 * at some other `bytes32` position in the `BeaconState` tree (e.g. `block_roots`,
 * `state_roots`, `latest_block_header.parent_root`, ...).
 *
 * @param chain_id chain identifier (drives chain spec + fork lookup)
 * @param block_slot slot of the block being proven; drives `summary_idx` and `block_idx`
 * @param state_slot slot of the state whose root the proof terminates in; drives
 *                   the fork-dependent `summaries_gidx` (91 pre-Gloas, 2950 from Gloas)
 * @return combined gindex, or 0 if no historic-direct proof is possible for
 *         `block_slot` (chain unknown, Capella not scheduled, or block predates Capella)
 */
gindex_t c4_historic_block_gindex(chain_id_t chain_id, uint64_t block_slot, uint64_t state_slot);

#endif
