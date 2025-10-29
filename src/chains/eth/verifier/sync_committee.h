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
#define MAX_STATES_SIZE  (MAX_SYNC_PERIODS * 4 + 1)

// Light client update format constants
#define SSZ_OFFSET_SIZE        4
#define SSZ_LENGTH_SIZE        8
#define MIN_UPDATE_SIZE        12
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
  C4_STATE_SYNC_EMPTY      = 0, // No states and no checkpoint yet
  C4_STATE_SYNC_PERIODS    = 1, // We do have at least one period stored
  C4_STATE_SYNC_CHECKPOINT = 2  // we only have a checkpoint stored
} c4_state_sync_type_t;

typedef struct {
  c4_state_sync_type_t status;
  union {
    uint32_t  periods[MAX_SYNC_PERIODS]; // max 8 periods (8*4 =32)
    bytes32_t checkpoint;                // 32 bytes
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
 * Processes light client updates to populate validator keys for required periods.
 *
 * @param ctx Verification context containing sync_data
 * @return true if updates processed successfully, false on error
 */
bool c4_update_from_sync_data(verify_ctx_t* ctx);

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
 * @param sync_committee SSZ object containing validator pubkeys
 * @param chain_id Chain identifier
 * @param previous_pubkeys_hash SHA256 hash of the previous period's validator keys
 * @return true on success, false on failure
 */
bool c4_set_sync_period(uint32_t period, ssz_ob_t sync_committee, chain_id_t chain_id, bytes32_t previous_pubkeys_hash);

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

/**
 * Get the oldest sync committee period stored for a chain.
 *
 * @param state Serialized chain state data
 * @return Oldest period number, or 0 if no periods stored
 */
uint32_t c4_eth_get_oldest_period(bytes_t state);

/**
 * Detect the fork (Deneb/Electra) for a light client update based on slot.
 * Reads the slot from the SSZ-encoded data and determines the fork.
 *
 * @param chain_id Chain identifier
 * @param data SSZ-encoded light client update data
 * @return Fork identifier (C4_FORK_DENEB or C4_FORK_ELECTRA)
 */
fork_id_t c4_eth_get_fork_for_lcu(chain_id_t chain_id, bytes_t data);

/**
 * Get the generalized index (gindex) for the current sync committee in the beacon state.
 * The gindex differs between forks (Deneb: 54, Electra: 86).
 *
 * @param chain_id Chain identifier
 * @param slot Slot number to determine the fork
 * @return Generalized index for current sync committee merkle proof
 */
uint64_t c4_current_sync_committee_gindex(chain_id_t chain_id, uint64_t slot);

#ifdef __cplusplus
}
#endif

#endif