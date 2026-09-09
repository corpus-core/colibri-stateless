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

#ifndef chain_spec_h__
#define chain_spec_h__

#ifdef __cplusplus
extern "C" {
#endif

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
  C4_FORK_MAX       = C4_FORK_GLOAS, // last regular fork; bump when adding the next

  C4_FORK_INVALID = -1
} fork_id_t;

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

// Consensus-layer BLOB_SCHEDULE entry (Fulu `get_blob_parameters` / `compute_fork_digest`).
// Distinct from `eth_blob_schedule_t`, which is the EL blob-base-fee table
// (timestamp + update_fraction). Terminated by `max_blobs_per_block == 0`
// because epoch 0 is a valid activation on genesis-at-Fulu devnets.
typedef struct {
  uint64_t epoch;
  uint64_t max_blobs_per_block;
} eth_blob_params_t;

typedef struct {
  chain_id_t                 chain_id;
  const uint64_t*            fork_epochs;
  const bytes32_t            genesis_validators_root;
  const bytes32_t            zk_sync_keys_root;        // initial zk sync keys root
  const int                  slots_per_epoch_bits;     // 5 = 32 slots per epoch
  const int                  epochs_per_period_bits;   // 8 = 256 epochs per period
  const uint64_t             weak_subjectivity_epochs; // max epochs before checkpoint validation required
  fork_version_func_t        fork_version_func;
  const eth_blob_schedule_t* blob_schedule;               // EIP-7892 blob schedule, DESCENDING by timestamp, {0,0}-terminated; NULL uses Cancun default
  uint64_t                   min_blob_base_fee;           // MIN_BLOB_BASE_FEE (`minBlobGasPrice`); 0 uses Ethereum's default (1 wei). Gnosis / Chiado use 1e9.
  const eth_blob_params_t*   blob_params;                 // CL BLOB_SCHEDULE, DESCENDING by epoch, {*,0}-terminated; NULL = empty
  uint64_t                   max_blobs_per_block_electra; // MAX_BLOBS_PER_BLOCK_ELECTRA; 0 uses Ethereum's default (9). Gnosis / Chiado use 2.
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
const chain_spec_t* c4_eth_get_chain_spec(chain_id_t id);

/**
 * Computes `hash_tree_root(ForkData(fork_version, genesis_validators_root))`
 * for `fork` on `spec`. Shared by domain calculation and `compute_fork_digest`.
 *
 * @param spec chain spec (genesis validators root and fork-version function)
 * @param fork fork whose 4-byte version is mixed into `ForkData`
 * @param out 32-byte fork-data root
 * @return true on success, false if `spec` is NULL or `fork` is out of range
 */
bool c4_eth_fork_data_root(const chain_spec_t* spec, fork_id_t fork, bytes32_t out);

/**
 * Computes the 4-byte `ForkDigest` for `fork` at its activation epoch
 * (`compute_fork_digest` from the Fulu consensus spec, including the
 * blob-parameter XOR once `epoch >= FULU_FORK_EPOCH`).
 *
 * @param chain_id chain whose spec, genesis validators root and blob params to use
 * @param fork fork whose version and activation epoch drive the digest
 * @param out 4-byte fork digest
 * @return true on success, false if the chain is unknown or `fork` is out of range
 */
bool c4_eth_compute_fork_digest(chain_id_t chain_id, fork_id_t fork, uint8_t out[4]);

/**
 * Resolves a 4-byte Beacon-API `ForkDigest` to the `fork_id_t` whose SSZ
 * type should be used to decode the following LightClientUpdate. BPO
 * digests map to the regular fork they sit on (Fulu / Gloas), not a
 * separate enum value. Unknown digests return `C4_FORK_INVALID`.
 *
 * @param chain_id chain whose cached digest table to search
 * @param digest 4-byte fork digest from the wire
 * @return matching fork, or `C4_FORK_INVALID`
 */
fork_id_t c4_eth_fork_from_digest(chain_id_t chain_id, const uint8_t digest[4]);

#define epoch_for_slot(slot, chain_spec)  ((slot) >> (chain_spec ? chain_spec->slots_per_epoch_bits : 5))
#define period_for_slot(slot, chain_spec) ((slot) >> (chain_spec ? (chain_spec->epochs_per_period_bits + chain_spec->slots_per_epoch_bits) : 13))

#define slot_for_epoch(epoch, chain_spec)   ((epoch) << (chain_spec ? chain_spec->slots_per_epoch_bits : 5))
#define slot_for_period(period, chain_spec) ((period) << (chain_spec ? (chain_spec->epochs_per_period_bits + chain_spec->slots_per_epoch_bits) : 13))

inline static bool is_gnosis_chain(chain_id_t chain_id) {
  return chain_id == C4_CHAIN_GNOSIS || chain_id == C4_CHAIN_GNOSIS_CHIADO;
}

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
 * CL block-hash proof (`ETH_CL_HEADER_PROOF`) anchors against for the fork active
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

#ifdef __cplusplus
}
#endif

#endif
