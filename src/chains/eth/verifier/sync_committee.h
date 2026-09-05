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

#ifndef sync_committee_h__
#define sync_committee_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "beacon_types.h"
#include "bytes.h"
#include "ssz.h"
#include "state.h"
#include "verify.h"
#include <stdint.h>

#define MAX_SYNC_PERIODS 8
// max(MAX_SYNC_PERIODS*4=32, 32, uint64+bytes32=40) + 1 status byte = 41
#define MAX_STATES_SIZE 41

// Light client update format constants
#define SSZ_OFFSET_SIZE        4
#define SSZ_LENGTH_SIZE        8
#define UPDATE_PREFIX_SIZE     (SSZ_OFFSET_SIZE + SSZ_LENGTH_SIZE)
#define LIGHTHOUSE_HEADER_SIZE 4
#define LIGHTHOUSE_OFFSET_SIZE 16

/**
 * Sync committee validators state for a specific period.
 * Contains validator public keys and metadata for period tracking.
 *
 * The previous_pubkeys_hash is critical for handling the edge case where
 * finality is delayed at period boundaries. According to the Ethereum spec,
 * if the first slot of a new period doesn't produce a finalized block,
 * the old sync committee keys remain valid until the first finalized block.
 *
 * By storing the hash of the previous period's keys, we can verify signatures
 * that were created during the transition without requiring additional proofs
 * about when the first finalized block occurred in the new period.
 */
typedef struct {
  uint32_t  lowest_period;         ///< The lowest period available, closest before the target
  uint32_t  current_period;        ///< The target period being searched for
  uint32_t  highest_period;        ///< The highest period for which we have keys
  bytes_t   validators;            ///< Validator public keys (512 * 48 bytes) or NULL_BYTES if not found
  bool      deserialized;          ///< True if validators are BLS-deserialized (96 bytes each)
  bytes32_t previous_pubkeys_hash; ///< SHA256 hash of previous period's keys (for transition verification)
} c4_sync_validators_t;

typedef enum {
  C4_STATE_SYNC_EMPTY             = 0, // No states and no checkpoint yet
  C4_STATE_SYNC_PERIODS           = 1, // We do have at least one period stored
  C4_STATE_SYNC_CHECKPOINT        = 2, // we only have a checkpoint stored
  C4_STATE_SYNC_BLOCKHASH_HEADER  = 3, // OP-Stack: cached verified execution-block header (number+hash); body not stored
  C4_STATE_SYNC_EXECUTION_PAYLOAD = 4  // OP-Stack: cached full execution payload; data here holds (number+hash), payload stored separately via storage_plugin
} c4_state_sync_type_t;

typedef struct {
  c4_state_sync_type_t status;
  union {
    uint32_t  periods[MAX_SYNC_PERIODS]; // max 8 periods (8*4 =32)
    bytes32_t checkpoint;                // 32 bytes
    struct {
      uint64_t  block_number; // execution block number
      bytes32_t blockhash;    // execution block hash
    } block;                  // BLOCKHASH_HEADER / EXECUTION_PAYLOAD (40 bytes)
  } data;
} c4_chain_state_t;

/**
 * Retrieve sync committee validators for a given period.
 * Implements automatic initialization, caching, and edge-case fallback for period transitions.
 * Uses previous_pubkeys_hash to handle delayed finality at period boundaries.
 *
 * @param ctx Verification context
 * @param period Sync committee period number
 * @param state Output parameter for validator state
 * @param pubkey_hash Optional output for SHA256 hash of validator keys (can be NULL)
 * @return C4_SUCCESS on success, C4_ERROR on failure, C4_PENDING if waiting for network requests
 */
const c4_status_t c4_get_validators(verify_ctx_t* ctx, uint32_t period, c4_sync_validators_t* state, bytes32_t pubkey_hash);

/**
 * Update sync committee state from provided sync_data in verification context.
 * Processes light client updates or ZK proofs to populate validator keys for the required periods.
 * When the resulting sync gap exceeds the Weak Subjectivity Period (WSP), the function will
 * additionally anchor the finalized header against a `checkpointz` endpoint, unless
 * `VERIFY_FLAG_SKIP_WSP_CHECK` is set on the context or `USE_CHECKPOINTZ` is disabled.
 *
 * @param ctx Verification context containing sync_data
 * @return C4_SUCCESS on success, C4_ERROR on failure, C4_PENDING if waiting for network requests
 */
c4_status_t c4_update_from_sync_data(verify_ctx_t* ctx);

/**
 * Handle and process raw light client updates from Beacon API.
 * Supports both standard SSZ format and Lighthouse variant.
 * Validates and stores sync committees for each period found in the updates.
 *
 * @param ctx Verification context
 * @param client_updates Raw SSZ-encoded light client updates (may contain multiple updates)
 * @return true if all updates processed successfully, false on error
 */
bool c4_handle_client_updates(verify_ctx_t* ctx, bytes_t client_updates);

/**
 * Generic iterator for processing light client updates with a callback.
 * Handles both standard SSZ and Lighthouse formats, validates structure,
 * and calls process_update for each individual update.
 *
 * @param ctx Verification context
 * @param light_client_updates Raw SSZ-encoded updates
 * @param process_update Callback function invoked for each update
 * @return true if all updates processed successfully, false on error
 */
bool c4_process_light_client_updates(verify_ctx_t* ctx, bytes_t light_client_updates, bool (*process_update)(verify_ctx_t*, ssz_ob_t*));

/**
 * Handle and process raw light client bootstrap data from Beacon API.
 * Validates and stores sync committees for the bootstrap data.
 *
 * @param ctx Verification context
 * @param bootstrap_data Raw SSZ-encoded light client bootstrap data
 * @param trusted_checkpoint Trusted block root (32 bytes)
 * @return C4_SUCCESS on success, C4_ERROR on failure, C4_PENDING if waiting for network requests
 */

c4_status_t c4_handle_bootstrap(verify_ctx_t* ctx, bytes_t bootstrap_data, bytes32_t trusted_checkpoint);
/**
 * Store a sync committee period in persistent storage.
 * Also stores SHA256(previous period's keys) as previous_pubkeys_hash for edge-case handling.
 * Automatically manages storage limits by removing old periods when necessary.
 *
 * @param period Period number to store
 * @param validatores  validator pubkeys
 * @param chain_id Chain identifier
 * @param previous_pubkeys_hash SHA256 hash of the previous period's validator keys
 * @return true on success, false on failure
 */
bool c4_set_sync_period(uint32_t period, bytes_t validators, chain_id_t chain_id, bytes32_t previous_pubkeys_hash);

/**
 * Retrieve chain state metadata from persistent storage.
 * Contains information about stored sync periods and trusted checkpoints.
 *
 * @param chain_id Chain identifier
 * @return Chain state structure (caller does not need to free)
 */
c4_chain_state_t c4_get_chain_state(chain_id_t chain_id);

/**
 * Set a trusted checkpoint for chain initialization.
 * Used when no sync committee state exists yet.
 * The checkpoint is used to bootstrap from via light_client/bootstrap endpoint.
 *
 * @param chain_id Chain identifier
 * @param checkpoint Trusted block root (32 bytes)
 */
void c4_eth_set_trusted_checkpoint(chain_id_t chain_id, bytes32_t checkpoint);

// `c4_current_sync_committee_gindex`, `c4_next_sync_committee_gindex` and
// `c4_finalized_root_gindex` are declared in `beacon_types.h` (which is
// already included via `#include "beacon_types.h"` above).

c4_chain_state_t c4_state_deserialize(bytes_t data);

/**
 * Persist the chain state in storage.
 * Used by both ETH (sync committee state) and OP (cached execution payload reference).
 *
 * @param chain_id Chain identifier
 * @param state Chain state to persist
 */
void c4_set_chain_state(chain_id_t chain_id, c4_chain_state_t* state);

bool c4_req_checkpointz_status(c4_state_t* state, chain_id_t chain_id, uint64_t* checkpoint_epoch, bytes32_t checkpoint_root);

#ifdef USE_CHECKPOINTZ
/**
 * Anchor a locally derived finalized header root against an external `checkpointz` / Beacon API
 * endpoint by fetching `eth/v1/beacon/blocks/{slot}/root` and comparing the response to
 * `expected_root`. This is the low-level building block used by the Weak Subjectivity check
 * for both verifier-driven and prover-supplied sync data.
 *
 * The caller is responsible for any local cleanup (e.g. `clear_sync_state`) when the result
 * is `C4_ERROR`. When `VERIFY_FLAG_SKIP_WSP_CHECK` is set on the context, the function
 * short-circuits with `C4_SUCCESS` without emitting a request.
 *
 * @param ctx           Verification context (state and flags)
 * @param slot          Slot number of the finalized header to anchor
 * @param expected_root Locally derived block root (32 bytes) to compare against
 * @return C4_SUCCESS if the roots match (or check skipped), C4_ERROR on mismatch or invalid
 *         response, C4_PENDING while the request is in flight.
 */
c4_status_t c4_verify_checkpointz_root(verify_ctx_t* ctx, uint64_t slot, bytes32_t expected_root);

/**
 * Verify a `ETH_CHECKPOINT_PROOF` against the canonical chain.
 *
 * The CheckpointProof is the WSP anchor for both `ZKSyncData.checkpoint` and
 * `LCSyncData.bootstrap` (when delivered as the `checkpoint_proof` union variant).
 * It binds the locally derived pubkeys (chain-of-trust: ZK proof public output or
 * LCU chain tail) to an anchor `BeaconBlockHeader` whose root is independently
 * confirmed via an external `checkpointz` endpoint.
 *
 * Steps performed:
 *   1. Reconstruct `sync_committee_root = SHA256(pubkeys_root || hash_tree_root(aggregate_pubkey))`.
 *   2. Walk `proof` up to a computed `state_root` using the fork-specific
 *      `currentSyncCommittee` gindex (Deneb 54, Electra/Fulu 86, Gloas 2945).
 *   3. Compare against `header.stateRoot`.
 *   4. Anchor `header` against `checkpointz` via `c4_verify_checkpointz_root`.
 *
 * Both trust paths (chain-of-trust + canonical anchor) must agree; an attacker has
 * to compromise BOTH layers (forged LCU/ZK keys AND the checkpointz provider).
 *
 * @param ctx              Verification context (state, flags, chain_id)
 * @param checkpoint_proof SSZ object pointing at an `ETH_CHECKPOINT_PROOF` container
 * @param pubkeys_root     `hash_tree_root` of the chain-of-trust pubkeys vector
 * @return `C4_SUCCESS` if both paths agree, `C4_ERROR` on mismatch or malformed
 *         input, `C4_PENDING` while the checkpointz request is in flight.
 */
c4_status_t c4_verify_checkpoint_proof(verify_ctx_t* ctx, ssz_ob_t checkpoint_proof, bytes32_t pubkeys_root);
#endif

#ifdef __cplusplus
}
#endif

#endif