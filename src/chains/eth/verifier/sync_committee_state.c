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

/**
 * @file sync_committee_state.c
 * @brief Ethereum Sync Committee State Management and Period Transition Handling
 *
 * This file manages the persistent storage and retrieval of Ethereum sync committee
 * validator keys across different periods. A critical feature is the handling of
 * period boundaries where finality may be delayed.
 *
 * ## The previous_pubkeys_hash Mechanism
 *
 * According to the Ethereum specification, sync committee transitions don't occur
 * at exact period boundaries. The old committee remains active until the FIRST
 * FINALIZED BLOCK of the new period. This creates an edge case:
 *
 * - A block at slot N (start of new period) might be signed by old committee
 * - We might only have stored the new period's committee
 * - Traditional solution requires proving when first finality occurred
 *
 * **Our Pragmatic Solution:**
 * When storing each period's committee, we also store SHA256(previous period's keys).
 * If signature verification fails, we can fetch the previous period's light_client_update,
 * hash its nextSyncCommittee, and verify against previous_pubkeys_hash to prove authenticity
 * without complex finality timing proofs.
 *
 * See c4_try_sync_from_next_period() for detailed implementation.
 */

#include "../util/compat.h"
#include "../util/crypto.h"
#include "../util/logger.h"
#include "../util/plugin.h"
#include "./sync_committee.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef C4_STATIC_MEMORY
#define C4_STATIC_STATE_SIZE   1024
#define C4_STATIC_SYNC_SIZE    49152
#define C4_STATIC_KEYS_48_SIZE 512 * 48
// Static buffers for embedded targets
static uint8_t state_buffer[C4_STATIC_STATE_SIZE];
#ifdef BLS_DESERIALIZE
static uint8_t sync_buffer[C4_STATIC_SYNC_SIZE];
static uint8_t keys_48_buffer[C4_STATIC_KEYS_48_SIZE];
#else
static uint8_t sync_buffer[C4_STATIC_KEYS_48_SIZE];
#endif
#endif
/**
 * Count the number of stored sync committee periods in the chain state.
 *
 * @param state Chain state containing period information
 * @return Number of periods stored (0 to MAX_SYNC_PERIODS)
 */
static inline uint32_t period_count(c4_chain_state_t* state) {
  if (state->status != C4_STATE_SYNC_PERIODS) return 0;
  for (int i = 0; i < MAX_SYNC_PERIODS; i++) {
    if (state->data.periods[i] == 0) return i;
  }
  return MAX_SYNC_PERIODS;
}

/**
 * Deserialize chain state from persistent storage bytes.
 * Supports two formats: period list (C4_STATE_SYNC_PERIODS) or checkpoint (C4_STATE_SYNC_CHECKPOINT).
 *
 * @param data Serialized state data
 * @return Deserialized chain state, or empty state if invalid
 */
c4_chain_state_t c4_state_deserialize(bytes_t data) {
  c4_chain_state_t state = {0};
  if (data.len == 0) return state;
  c4_state_sync_type_t status = (c4_state_sync_type_t) data.data[0];

  switch (status) {
    case C4_STATE_SYNC_PERIODS: {
      for (int i = 0; i < MAX_SYNC_PERIODS && data.len > i * 4 + 4; i++)
        state.data.periods[i] = uint32_from_le(data.data + 1 + i * 4);
      break;
    }
    case C4_STATE_SYNC_CHECKPOINT:
      if (data.len != 33) return (c4_chain_state_t) {0}; // invalid length
      memcpy(state.data.checkpoint, data.data + 1, 32);
      break;
    case C4_STATE_SYNC_BLOCKHASH_HEADER:
    case C4_STATE_SYNC_EXECUTION_PAYLOAD:
      // Layout: status(1) + uint64 LE block_number(8) + bytes32 blockhash(32) = 41
      if (data.len != 41) return (c4_chain_state_t) {0};
      state.data.block.block_number = uint64_from_le(data.data + 1);
      memcpy(state.data.block.blockhash, data.data + 9, 32);
      break;
    case C4_STATE_SYNC_EMPTY:
    default:
      return (c4_chain_state_t) {0}; // invalid status
  }
  state.status = status;
  if (status == C4_STATE_SYNC_PERIODS && state.data.periods[0] == 0)
    state.status = C4_STATE_SYNC_EMPTY; // empty Lists will always  use C4_STATE_SYNC_EMPTY, because init_state relies on it.
  return state;
}

INTERNAL c4_chain_state_t c4_get_chain_state(chain_id_t chain_id) {
  c4_chain_state_t state = {0};
  char             name[100];
  storage_plugin_t storage_conf = {0};

#ifdef C4_STATIC_MEMORY
  // Use static buffer with size limit
  buffer_t tmp = stack_buffer(state_buffer);
#else
  buffer_t tmp = {0};
#endif

  c4_get_storage_config(&storage_conf);

  sbprintf(name, "states_%l", (uint64_t) chain_id);
#ifndef C4_STATIC_MEMORY
  tmp.allocated = MAX_STATES_SIZE;
#endif

  if (storage_conf.get(name, &tmp) && tmp.data.data)
    state = c4_state_deserialize(tmp.data);

#ifndef C4_STATIC_MEMORY
  buffer_free(&tmp);
#endif

  return state;
}
INTERNAL void c4_set_chain_state(chain_id_t chain_id, c4_chain_state_t* state) {
  if (!state || chain_id == 0) return;
  char             name[100];
  storage_plugin_t storage_conf = {0};
  c4_get_storage_config(&storage_conf);
  uint8_t data[MAX_STATES_SIZE];
  bytes_t bytes = bytes(data, 1);
  data[0]       = state->status;
  switch (state->status) {
    case C4_STATE_SYNC_PERIODS:
      for (int i = 0; i < MAX_SYNC_PERIODS && state->data.periods[i] != 0; i++) {
        uint32_to_le(data + 1 + i * 4, state->data.periods[i]);
        bytes.len += 4;
      }
      break;
    case C4_STATE_SYNC_CHECKPOINT:
      memcpy(data + 1, state->data.checkpoint, 32);
      bytes.len += 32;
      break;
    case C4_STATE_SYNC_BLOCKHASH_HEADER:
    case C4_STATE_SYNC_EXECUTION_PAYLOAD:
      uint64_to_le(data + 1, state->data.block.block_number);
      memcpy(data + 9, state->data.block.blockhash, 32);
      bytes.len += 40;
      break;
    case C4_STATE_SYNC_EMPTY:
    default:
      break;
  }
  sbprintf(name, "states_%l", (uint64_t) chain_id);
  storage_conf.set(name, bytes);
}

void c4_eth_set_trusted_checkpoint(chain_id_t chain_id, bytes32_t checkpoint) {
  if (!checkpoint || chain_id == 0) return;
  c4_chain_state_t state = c4_get_chain_state(chain_id);
  if (state.status == C4_STATE_SYNC_EMPTY) {
    state.status = C4_STATE_SYNC_CHECKPOINT;
    memcpy(state.data.checkpoint, checkpoint, 32);
    c4_set_chain_state(chain_id, &state);
  }
}

static bool req_client_update(c4_state_t* state, uint32_t period, uint32_t count, chain_id_t chain_id, bytes_t* data) {
  buffer_t tmp = {0};
  bprintf(&tmp, "eth/v1/beacon/light_client/updates?start_period=%d&count=%d", period, count);

  data_request_t* req = c4_state_get_data_request_by_url(state, (char*) tmp.data.data);
  if (req) {
    buffer_free(&tmp);
    if (req->response.data) {
      *data = req->response;
      return true;
    }
    else if (req->error) {
      c4_state_add_error(state, req->error);
      return false;
    }
    return false; // still pending
  }
  data_request_t* new_req = safe_calloc(1, sizeof(data_request_t));
  new_req->chain_id       = chain_id;
  new_req->url            = (char*) tmp.data.data;
  new_req->encoding       = C4_DATA_ENCODING_SSZ;
  new_req->type           = C4_DATA_TYPE_BEACON_API;
  c4_state_add_request(state, new_req);
  return false;
}

#ifdef USE_CHECKPOINTZ
/**
 * Request checkpoint status from checkpointz (beacon node).
 * Used during bootstrap to fetch the current finalized checkpoint when no initial checkpoint is provided.
 *
 * ⚠️ This function requires HTTP access to a beacon node and is disabled when USE_CHECKPOINTZ=OFF.
 * For embedded devices without HTTP, an initial checkpoint must be provided in the configuration.
 */
bool c4_req_checkpointz_status(c4_state_t* state, chain_id_t chain_id, uint64_t* checkpoint_epoch, bytes32_t checkpoint_root) {
  buffer_t tmp = {0};
  bprintf(&tmp, "eth/v1/beacon/states/head/finality_checkpoints");

  data_request_t* req = c4_state_get_data_request_by_url(state, (char*) tmp.data.data);
  if (req) {
    buffer_free(&tmp);
    if (req->response.data) {
      json_t res = json_parse((char*) req->response.data);

      // Validate JSON structure (Beacon API compatible format). `root` is checked as a
      // generic `string` (not `bytes32`) because some Beacon API providers emit block
      // roots WITHOUT the `0x` prefix; `json_as_bytes` -> `hex_to_bytes` accepts both,
      // we just enforce the 32-byte length after decoding.
      if (!req->validated) {
        const char* err = json_validate(res, "{data:{finalized:{epoch:suint,root:string}}}", "finality checkpoints");
        if (err) {
          c4_state_add_error(state, err);
          return false;
        }
        req->validated = true;
      }

      json_t data      = json_get(res, "data");
      json_t finalized = json_get(data, "finalized");
      json_t epoch     = json_get(finalized, "epoch");
      json_t root      = json_get(finalized, "root");

      *checkpoint_epoch = json_as_uint64(epoch);
      buffer_t root_buf = {.data = bytes(checkpoint_root, 32), .allocated = -32};
      bytes_t  decoded  = json_as_bytes(root, &root_buf);
      if (decoded.len != 32) {
        c4_state_add_error(state, "finality checkpoints data.finalized.root: expected 32-byte hex string");
        return false;
      }
      return true;
    }
    else if (req->error) {
      c4_state_add_error(state, req->error);
      return false;
    }
    else
      return false; // still pending, but weird
  }
  data_request_t* new_req = safe_calloc(1, sizeof(data_request_t));
  new_req->chain_id       = chain_id;
  new_req->url            = (char*) tmp.data.data;
  new_req->encoding       = C4_DATA_ENCODING_JSON;
  new_req->type           = C4_DATA_TYPE_CHECKPOINTZ;
  c4_state_add_request(state, new_req);
  return false;
}
#else
/**
 * Stub for req_checkpointz_status when USE_CHECKPOINTZ is disabled.
 * Returns an error indicating that an initial checkpoint must be provided in the configuration.
 */
bool c4_req_checkpointz_status(c4_state_t* state, chain_id_t chain_id, uint64_t* checkpoint_epoch, bytes32_t checkpoint_root) {
  (void) chain_id;
  (void) checkpoint_epoch;
  (void) checkpoint_root;
  c4_state_add_error(state, "Checkpointz disabled (USE_CHECKPOINTZ=OFF). Initial checkpoint must be provided in configuration for bootstrap.");
  return false;
}
#endif // USE_CHECKPOINTZ

static bool req_bootstrap(c4_state_t* state, bytes32_t block_root, chain_id_t chain_id, bytes_t* data) {
  buffer_t tmp = {0};
  bprintf(&tmp, "eth/v1/beacon/light_client/bootstrap/0x%x", bytes(block_root, 32));

  data_request_t* req = c4_state_get_data_request_by_url(state, (char*) tmp.data.data);
  if (req) {
    buffer_free(&tmp);
    if (req->response.data) {
      *data = req->response;
      return true;
    }
    else if (req->error) {
      c4_state_add_error(state, req->error);
      return false;
    }
    return false; // still pending
  }
  data_request_t* new_req = safe_calloc(1, sizeof(data_request_t));
  new_req->chain_id       = chain_id;
  new_req->url            = (char*) tmp.data.data;
  new_req->encoding       = C4_DATA_ENCODING_SSZ;
  new_req->type           = C4_DATA_TYPE_BEACON_API;
  c4_state_add_request(state, new_req);
  return false;
}

c4_status_t c4_handle_bootstrap(verify_ctx_t* ctx, bytes_t bootstrap_data, bytes32_t trusted_checkpoint) {
  // Parse bootstrap data as SSZ. The bootstrap container layout differs by fork
  // (branch depth changes), so we resolve it via the central helper.
  fork_id_t        fork          = c4_eth_get_fork_for_lcu(ctx->chain_id, bootstrap_data);
  const ssz_def_t* bootstrap_def = eth_get_light_client_bootstrap(fork);
  if (!bootstrap_def) THROW_ERROR("Bootstrap data: unsupported fork");
  ssz_ob_t  bootstrap             = {.bytes = bootstrap_data, .def = bootstrap_def};
  bytes32_t previous_pubkeys_hash = {0}; // in case of a bootstrap, there is no previous pubkey hash, so we set it to 0

  data_request_t* src_req = c4_state_get_data_request_by_response(&ctx->state, bootstrap_data);
  if (!(src_req && src_req->validated)) {
    if (!ssz_is_valid(bootstrap, true, &ctx->state))
      THROW_ERROR("Invalid SSZ structure in bootstrap data");
    if (src_req) src_req->validated = true;
  }

  // Extract components (no need for ssz_is_error checks after validation)
  ssz_ob_t header                 = ssz_get(&bootstrap, "header");
  ssz_ob_t beacon                 = ssz_get(&header, "beacon");
  ssz_ob_t current_sync_committee = ssz_get(&bootstrap, "currentSyncCommittee");
  ssz_ob_t sync_committee_branch  = ssz_get(&bootstrap, "currentSyncCommitteeBranch");
  ssz_ob_t state_root             = ssz_get(&beacon, "stateRoot");
  uint64_t slot                   = ssz_get_uint64(&beacon, "slot");

  // Calculate blockhash
  bytes32_t blockhash = {0};
  ssz_hash_tree_root(beacon, blockhash);

  // Verify blockhash matches trusted checkpoint
  if (memcmp(blockhash, trusted_checkpoint, 32))
    THROW_ERROR("Bootstrap header blockhash does not match trusted checkpoint");

  // Verify merkle proof for current sync committee
  bytes32_t sync_root   = {0};
  bytes32_t merkle_root = {0};
  ssz_hash_tree_root(current_sync_committee, sync_root);

  // Get current sync committee gindex based on fork
  uint64_t gindex = c4_current_sync_committee_gindex(ctx->chain_id, slot);

  ssz_verify_single_merkle_proof(sync_committee_branch.bytes, sync_root, gindex, merkle_root);

  if (memcmp(merkle_root, state_root.bytes.data, 32))
    THROW_ERROR("Invalid merkle proof in bootstrap");

  // Calculate current period from slot (no +1 because this is currentSyncCommittee)
  const chain_spec_t* spec   = c4_eth_get_chain_spec(ctx->chain_id);
  uint32_t            period = (slot >> (spec->slots_per_epoch_bits + spec->epochs_per_period_bits));

  // Save sync committee for current period
  return c4_set_sync_period(period, ssz_get(&current_sync_committee, "pubkeys").bytes, ctx->chain_id, previous_pubkeys_hash) ? C4_SUCCESS : C4_ERROR;
}

/**
 * Cleanup old sync periods when storage limit is reached.
 * Keeps the oldest and latest periods, removes intermediate ones.
 *
 * @param state Chain state containing period information
 * @param chain_id Chain identifier
 * @param max_states Maximum number of sync states to keep
 * @return Updated number of periods after cleanup
 */
static uint32_t cleanup_old_periods(c4_chain_state_t* state, chain_id_t chain_id, uint32_t max_states) {
  storage_plugin_t storage_conf = {0};
  c4_get_storage_config(&storage_conf);
  uint32_t periods = period_count(state);
  char     name[100];

  while (periods >= max_states) {
    uint32_t oldest       = 0;
    uint32_t latest       = 0;
    int      oldest_index = 0;

    // Find the oldest and latest period
    for (int i = 0; state->data.periods[i]; i++) {
      uint32_t p = state->data.periods[i];
      if (p > latest || latest == 0)
        latest = p;
      if (p < oldest || oldest == 0) {
        oldest       = p;
        oldest_index = i;
      }
    }

    if (periods > 2) {
      // Keep the oldest and the latest, but remove the second oldest
      uint32_t oldest_2nd       = 0;
      int      oldest_2nd_index = 0;
      for (int i = 0; i < periods; i++) {
        uint32_t p = state->data.periods[i];
        if (p > oldest && p < latest && (p < oldest_2nd || oldest_2nd == 0)) {
          oldest_2nd       = p;
          oldest_2nd_index = i;
        }
      }
      oldest_index = oldest_2nd_index;
      oldest       = oldest_2nd;
    }

    // Delete from storage and remove from periods array
    sbprintf(name, "sync_%l_%d", (uint64_t) chain_id, oldest);
    storage_conf.del(name);
    if (oldest_index < periods - 1)
      memmove(state->data.periods + oldest_index, state->data.periods + oldest_index + 1, (periods - oldest_index - 1) * sizeof(uint32_t));
    periods--;
  }

  return periods;
}

/**
 * Store a sync committee period in persistent storage.
 * Stores validators and previous pubkeys hash.
 *
 * @param period Period number to store
 * @param validators Validator public keys (512 * 48 bytes)
 * @param previous_pubkeys_hash Hash of previous sync committee
 * @param chain_id Chain identifier
 * @return true on success, false on failure
 */
static bool store_sync_period(uint32_t period, bytes_t validators, bytes32_t previous_pubkeys_hash, chain_id_t chain_id) {
  storage_plugin_t storage_conf = {0};
  c4_get_storage_config(&storage_conf);
  char name[100];

  sbprintf(name, "sync_%l_%d", (uint64_t) chain_id, period);

#ifdef C4_STATIC_MEMORY
  // Use static buffer on embedded devices
  uint8_t storage_buffer[512 * 48 + 32];
  if (validators.len + 32 > sizeof(storage_buffer)) return false;
  memcpy(storage_buffer, validators.data, validators.len);
  memcpy(storage_buffer + validators.len, previous_pubkeys_hash, 32);
  storage_conf.set(name, bytes(storage_buffer, validators.len + 32));
#else
  // Use dynamic buffer for non-embedded
  buffer_t storage_data = {0};
  buffer_append(&storage_data, validators);
  buffer_append(&storage_data, bytes(previous_pubkeys_hash, 32));
  storage_conf.set(name, storage_data.data);
  buffer_free(&storage_data);
#endif

  return true;
}

INTERNAL bool c4_set_sync_period(uint32_t period, bytes_t validators, chain_id_t chain_id, bytes32_t previous_pubkeys_hash) {
  const chain_spec_t* spec         = c4_eth_get_chain_spec(chain_id);
  storage_plugin_t    storage_conf = {0};
  c4_chain_state_t    state        = c4_get_chain_state(chain_id);

  if (!spec) return false;
  // Initialize period tracking if needed
  if (state.status != C4_STATE_SYNC_PERIODS) {
    state.status = C4_STATE_SYNC_PERIODS;
    memset(state.data.periods, 0, MAX_SYNC_PERIODS * 4);
  }

  c4_get_storage_config(&storage_conf);

  // Cleanup old periods if storage limit reached
  uint32_t periods = cleanup_old_periods(&state, chain_id, storage_conf.max_sync_states);

  // Add new period to tracking
  state.data.periods[periods] = period;

  // Store the sync committee data
  if (!store_sync_period(period, validators, previous_pubkeys_hash, chain_id))
    return false;

  // Update chain state
  c4_set_chain_state(chain_id, &state);
  return true;
}

static c4_status_t init_sync_state(verify_ctx_t* ctx) {
  c4_chain_state_t    chain_state      = c4_get_chain_state(ctx->chain_id);
  c4_state_t*         state            = &ctx->state;
  bytes_t             bootstrap_data   = {0};
  bytes_t             client_update    = {0};
  bytes32_t           checkpoint_root  = {0};
  uint64_t            checkpoint_epoch = 0;
  const chain_spec_t* spec             = c4_eth_get_chain_spec(ctx->chain_id);

  if (!spec) THROW_ERROR("unsupported chain id!");

  switch (chain_state.status) {
    case C4_STATE_SYNC_EMPTY:

      // No state exists - fetch checkpoint from checkpointz server
      if (c4_req_checkpointz_status(state, ctx->chain_id, &checkpoint_epoch, checkpoint_root)) {
        // Set the checkpoint as trusted blockhash
        c4_eth_set_trusted_checkpoint(ctx->chain_id, checkpoint_root);
        // Verify the checkpoint was actually persisted before recursing
        c4_chain_state_t updated = c4_get_chain_state(ctx->chain_id);
        if (updated.status == C4_STATE_SYNC_EMPTY)
          THROW_ERROR("Failed to persist checkpoint - storage not available");
        // Recursively call init_sync_state to process the bootstrap with the new trusted checkpoint
        return init_sync_state(ctx);
      }
      else
        // Request is either pending or failed
        return state->error ? C4_ERROR : C4_PENDING;

    case C4_STATE_SYNC_CHECKPOINT: {
      // We have a trusted checkpoint - use bootstrap
      if (req_bootstrap(state, chain_state.data.checkpoint, ctx->chain_id, &bootstrap_data))
        TRY_ASYNC(c4_handle_bootstrap(ctx, bootstrap_data, chain_state.data.checkpoint));

      return state->error ? C4_ERROR : (c4_state_get_pending_request(state) ? C4_PENDING : C4_SUCCESS);
    }

    case C4_STATE_SYNC_PERIODS:
      THROW_ERROR("init_sync_state called with existing sync committee state");

    case C4_STATE_SYNC_BLOCKHASH_HEADER:
    case C4_STATE_SYNC_EXECUTION_PAYLOAD:
      THROW_ERROR("unexpected OP-Stack chain state for ETH chain");

    default:
      THROW_ERROR("unknown sync state");
  }
}

/**
 * Clear all sync committee state for a chain on critical errors.
 * Called when weak subjectivity validation fails or corruption is detected.
 * Forces re-initialization from a trusted checkpoint on next verification.
 *
 * @param chain_id Chain identifier for which to clear all state
 */
static void clear_sync_state(chain_id_t chain_id) {
  storage_plugin_t storage_conf = {0};
  c4_get_storage_config(&storage_conf);

  // A host may register a storage plugin without a delete hook; nothing to clear then.
  if (!storage_conf.del) return;

  // Delete all sync states for this chain
  c4_chain_state_t chain_state = c4_get_chain_state(chain_id);
  for (uint32_t i = 0; chain_state.status == C4_STATE_SYNC_PERIODS && i < MAX_SYNC_PERIODS && chain_state.data.periods[i] != 0; i++) {
    uint32_t p = chain_state.data.periods[i];
    char     name[100];
    sbprintf(name, "sync_%l_%d", (uint64_t) chain_id, p);
    storage_conf.del(name);
  }

  // Delete chain state
  char name[100];
  sbprintf(name, "states_%l", (uint64_t) chain_id);
  storage_conf.del(name);
}

/**
 * Retrieve sync committee validators from persistent storage cache.
 * Handles BLS deserialization if needed and strips the 32-byte previous_pubkeys_hash suffix.
 *
 * Also determines the lowest and highest periods available to guide sync strategy.
 * On C4_STATIC_MEMORY systems, uses pre-allocated buffers to avoid dynamic allocation.
 *
 * @param ctx Verification context
 * @param period Period number to retrieve
 * @return Validator state with keys if found, or empty validators if not cached
 */
static c4_sync_validators_t get_validators_from_cache(verify_ctx_t* ctx, uint32_t period) {
  storage_plugin_t storage_conf   = {0};
  c4_chain_state_t chain_state    = c4_get_chain_state(ctx->chain_id);
  uint32_t         lowest_period  = 0;
  uint32_t         highest_period = 0;
  bytes32_t        previous_root  = {0};
#ifdef C4_STATIC_MEMORY
  buffer_t validators = stack_buffer(sync_buffer);
  buffer_t prev_data  = {0};
#else
  buffer_t validators = {0};
  buffer_t prev_data  = {0};
#ifdef BLS_DESERIALIZE
  validators.allocated = 512 * 48 * 2;
#else
  validators.allocated = 512 * 48;
#endif

#endif
  char name[100];
  bool found = false;
  c4_get_storage_config(&storage_conf);
  sbprintf(name, "sync_%l_%d", (uint64_t) ctx->chain_id, period);

  for (uint32_t i = 0; chain_state.status == C4_STATE_SYNC_PERIODS && i < MAX_SYNC_PERIODS && chain_state.data.periods[i] != 0; i++) {
    uint32_t p = chain_state.data.periods[i];
    if (p == period) found = true;
    lowest_period  = p > lowest_period && p <= period ? p : lowest_period;
    highest_period = p > highest_period ? p : highest_period;
  }

  if (found && storage_conf.get) storage_conf.get(name, &validators);
  if (found && validators.data.len == 0) {
    log_debug("sync cache corrupted. The data for %s is missing, so we re init the sync state", name);

    // this is a corrupted state, we need to clear it
    clear_sync_state(ctx->chain_id);
    return (c4_sync_validators_t) {.current_period = period};
  }
  log_debug("fetch from cache %s : %d bytes)", name, validators.data.len);
  if (validators.data.len % 48 == 32)
    memcpy(previous_root, validators.data.data + validators.data.len - 32, 32);

#ifdef BLS_DESERIALIZE
  // Check if validators need deserialization (only pubkeys, not including the 32-byte root)
  size_t expected_serialized_size = 512 * 48 + 32;
  if (validators.data.data && (validators.data.len == expected_serialized_size || validators.data.len == 512 * 48)) {
    log_debug("deserializing validators (len=%d)", validators.data.len);
#ifdef C4_STATIC_MEMORY
    memcpy(keys_48_buffer, validators.data.data, 512 * 48);
    bytes_t b = blst_deserialize_p1_affine(keys_48_buffer, 512, sync_buffer);
#else
    bytes_t b = blst_deserialize_p1_affine(validators.data.data, 512, NULL);
    buffer_free(&validators);
#endif
    validators.data = b;
    storage_conf.set(name, b);
    //    log_debug("storing deserialized validators: %d bytes", b.len);
  }
  else if (validators.data.data && validators.data.len == 512 * 48 + 32) {
    // Old format without deserialization - just strip the root
    validators.data.len = 512 * 48;
    log_debug("stripping root from validators (len=%d)", validators.data.len);
  }
  else
    log_debug("found validators (len=%d)", validators.data.len);

#else
  // Strip the 32-byte root from validators data if present
  if (validators.data.len >= 32) {
    validators.data.len -= 32;
  }
  log_debug("found validators (NO DESERIALIZATION) (len=%d)", validators.data.len);
#endif

  if (validators.data.len == 0) validators.data.data = NULL; // just to make sure we mark it as not found, even if we are using static memory

  c4_sync_validators_t result = {
      .deserialized   = validators.data.data && validators.data.len > 512 * 48,
      .current_period = period,
      .lowest_period  = lowest_period,
      .highest_period = highest_period,
      .validators     = validators.data};
  memcpy(result.previous_pubkeys_hash, previous_root, 32);
  return result;
}

#ifdef USE_CHECKPOINTZ
/**
 * Anchor a locally derived finalized header root against an external `checkpointz` / Beacon API
 * endpoint by fetching `eth/v1/beacon/blocks/{slot}/root` and comparing the response.
 *
 * This is the low-level building block of the Weak Subjectivity Period (WSP) check. It is used
 * by both the verifier-driven sync path (`c4_check_weak_subjectivity`) and the prover-supplied
 * sync paths (`update_from_zk_sync_data`, `update_from_lc_sync_data`).
 *
 * The caller is responsible for any local cleanup (e.g. clearing the sync state) on `C4_ERROR`.
 * When `VERIFY_FLAG_SKIP_WSP_CHECK` is set, the function short-circuits without emitting a request.
 *
 * @param ctx           Verification context (provides state and flags)
 * @param slot          Slot number of the finalized header to anchor
 * @param expected_root Locally derived block root (32 bytes) to compare against the checkpointz answer
 * @return C4_SUCCESS if the roots match (or the check was skipped), C4_ERROR on mismatch or
 *         invalid response, C4_PENDING while the request is in flight.
 */
INTERNAL c4_status_t c4_verify_checkpointz_root(verify_ctx_t* ctx, uint64_t slot, bytes32_t expected_root) {
  if (!ctx) return C4_ERROR;
  if (ctx->flags & VERIFY_FLAG_SKIP_WSP_CHECK) {
    log_warn("Skipping Weak Subjectivity Period check due to VERIFY_FLAG_SKIP_WSP_CHECK");
    return C4_SUCCESS;
  }
  if (!slot) return c4_state_add_error(&ctx->state, "Checkpoint slot not provided for WSP check");

  // NOTE: `sbprintf(url, ...)` was wrong here -- sbprintf expects a stack-allocated array
  // (uses sizeof(var) to size the backing buffer), not a `char*` pointer. With a `char*`
  // it produced an 8-byte fixed buffer pointing at NULL, leaving `url` empty/NULL. That
  // turned every checkpointz request into a fresh request with empty URL, so the lookup
  // by URL never deduplicated and the verifier looped forever on the WSP anchor request
  // (caught by the emscripten integration test's `before` hook). Use bprintf with NULL
  // buf to get an owned, heap-allocated formatted string instead.
  char* url = bprintf(NULL, "eth/v1/beacon/blocks/%l/root", slot);

  data_request_t* req = c4_state_get_data_request_by_url(&ctx->state, url);
  if (!req) {
    // Issue a fresh request; ownership of `url` transfers to the request.
    //
    // The request is routed as `C4_DATA_TYPE_BEACON_API` (not CHECKPOINTZ), even
    // though the surrounding feature is named the "checkpointz WSP anchor". Two
    // reasons:
    //
    //   1. Ethpandaops/checkpointz servers reject arbitrary historic slots. They
    //      keep `BlockIDSlot` semantically (see BlockRoot in pkg/service/eth/eth.go)
    //      but only retain the last ~30 finalized snapshots (~6 h on mainnet, see
    //      `/checkpointz/v1/beacon/slots`). The slots we anchor against are
    //      typically much older:
    //        - LC sync:       `finalizedHeader.beacon.slot`, can be many periods old
    //                         after long offline gaps.
    //        - ZK sync:       `checkpoint.header.slot` from `ZKSyncData.checkpoint`
    //                         (header_proof variant). The prover sets this in
    //                         `period_store_zk_ssz.c` to the next epoch boundary
    //                         after the attested slot, which can be ~1-2 days old.
    //        - `c4_check_weak_subjectivity`: last verified `finalizedHeader` slot.
    //
    //   2. Routing through CHECKPOINTZ caused a cascade of HTTP 500s on every WSP
    //      check before the beacon_api fallback finally answered (verified live
    //      against `https://sync-mainnet.beaconcha.in` and others -- see PR #276).
    //
    // The "current finalized snapshot" lookup in `c4_req_checkpointz_status`
    // (`eth/v1/beacon/states/head/finality_checkpoints`) intentionally stays on
    // CHECKPOINTZ because that is exactly the endpoint checkpointz is designed for.
    //
    // Callers SHOULD still pass an epoch-boundary slot whenever possible: beacon-API
    // nodes always have arbitrary slots, but an epoch boundary makes the anchor a
    // canonical finality checkpoint and matches what the prover assembles into
    // `checkpoint.header` (slots_per_epoch + slot - slot % slots_per_epoch).
    data_request_t* new_req = safe_calloc(1, sizeof(data_request_t));
    new_req->chain_id       = ctx->chain_id;
    new_req->url            = url;
    new_req->encoding       = C4_DATA_ENCODING_JSON;
    new_req->type           = C4_DATA_TYPE_CHECKPOINTZ;
    c4_state_add_request(&ctx->state, new_req);
    return C4_PENDING;
  }

  safe_free(url);

  if (req->error)
    return c4_state_add_error(&ctx->state, req->error);

  if (!req->response.data) return C4_PENDING;

  // Validate the checkpointz response shape: {"data":{"root":"<64-hex-chars>"}}.
  // We validate `root` as a generic `string` (not `bytes32`) because some Beacon API
  // endpoints (e.g. `sync-mainnet.beaconcha.in`) emit the block root WITHOUT the `0x`
  // prefix, while others (e.g. Lodestar) emit it WITH the prefix; the strict
  // `bytes32` schema check rejects the un-prefixed form. `hex_to_bytes` (used inside
  // `json_as_bytes`) accepts both variants, so we just need to enforce the 32-byte
  // length after decoding.
  json_t res = json_parse((char*) req->response.data);
  if (!req->validated) {
    CHECK_JSON(res, "{data:{root:string}}", "checkpointz response");
    req->validated = true;
  }

  bytes32_t checkpointz_root = {0};
  buffer_t  root_buf         = stack_buffer(checkpointz_root);
  bytes_t   decoded          = json_as_bytes(json_get(json_get(res, "data"), "root"), &root_buf);
  if (decoded.len != 32)
    return c4_state_add_error(&ctx->state, "checkpointz response.data.root: expected 32-byte hex string");

  if (memcmp(checkpointz_root, expected_root, 32) != 0)
    return c4_state_add_error(&ctx->state, "Weak subjectivity check failed: checkpoint mismatch");

  return C4_SUCCESS;
}

/**
 * Weak Subjectivity Period (WSP) Validation for the verifier-driven sync path.
 *
 * Protects against long-range attacks when a client syncs after being offline for longer
 * than the weak subjectivity period (typically ~2 weeks / 256 epochs).
 *
 * ## Security Model -- Double-Trust
 *
 * Two independent trust paths must agree on the same `currentSyncCommittee` for the
 * checkpointz-anchored finalized slot:
 *
 *   - **Chain-of-trust** (LCU chain): the apply loop above followed signatures from the
 *     last cached period up to the bootstrap period; this populated the local cache with
 *     `nextSyncCommittee.pubkeys` for every period along the way. The LCU for period
 *     `bootstrap_period - 1` carries the pubkeys vector that should match `bootstrap_period`.
 *
 *   - **Anchor-to-canonical** (checkpointz Bootstrap): we fetch the current finalized
 *     checkpoint via `head/finality_checkpoints`, then a `LightClientBootstrap` for that
 *     checkpoint root. The bootstrap's `currentSyncCommittee.pubkeys` are the canonical
 *     truth for that period (the bootstrap is itself a Merkle proof against the
 *     checkpointz-anchored beacon header).
 *
 * An attacker has to compromise BOTH layers (forged LCU/ZK keys AND the checkpointz
 * provider). Either alone is insufficient.
 *
 * ## Implementation
 *
 *   1. `c4_req_checkpointz_status` -- current finalized (`slot`, `root`)
 *   2. `req_bootstrap` for that root -- SSZ-validated `LightClientBootstrap`
 *   3. Verify the bootstrap header's `hash_tree_root` matches the checkpointz root
 *   4. Extract `bootstrap_pubkeys_root = hash_tree_root(currentSyncCommittee.pubkeys)`
 *   5. Scan the cached LCU responses for the update with `attested_slot` in period
 *      `bootstrap_period - 1`; its `nextSyncCommittee.pubkeys` are the chain-of-trust
 *      pubkeys for `bootstrap_period`. Extract `lcu_pubkeys_root`.
 *   6. Compare roots. Match → keep state. Mismatch → `clear_sync_state` + error.
 *
 * ## When to Disable
 *
 * Either at build time via `USE_CHECKPOINTZ=OFF` (the entire function is compiled out, see
 * `c4_get_validators`), or at runtime via `VERIFY_FLAG_SKIP_WSP_CHECK`. Both are explicit
 * security trade-offs intended for environments with an alternative trust anchor.
 *
 * ⚠️ Disabling increases long-range attack risk during extended offline periods.
 *
 * @param ctx Verification context (with already-applied LCU responses on `ctx->state.requests`)
 * @param sync_state Current sync committee state (`highest_period` reflects the cached state)
 * @param target_period Target period to sync to
 * @return `C4_SUCCESS` if both paths agree (or no WSP gap), `C4_ERROR` on mismatch /
 *         malformed bootstrap, `C4_PENDING` while checkpointz or bootstrap requests are
 *         in flight.
 */
static c4_status_t c4_check_weak_subjectivity(verify_ctx_t* ctx, c4_sync_validators_t* sync_state, uint32_t target_period) {
  const chain_spec_t* spec = c4_eth_get_chain_spec(ctx->chain_id);
  if (!spec) return C4_SUCCESS; // Cannot validate without spec

  if (target_period <= sync_state->highest_period) return C4_SUCCESS; // No gap, no need to check

  uint32_t period_diff = target_period - sync_state->highest_period;
  uint64_t epoch_diff  = ((uint64_t) period_diff) << spec->epochs_per_period_bits;

  if (epoch_diff <= spec->weak_subjectivity_epochs) return C4_SUCCESS; // Within WSP

  if (ctx->flags & VERIFY_FLAG_SKIP_WSP_CHECK) {
    log_warn("Skipping WSP cross-check due to VERIFY_FLAG_SKIP_WSP_CHECK");
    return C4_SUCCESS;
  }

  // 1. Anchor-to-canonical: get the current finalized checkpoint from checkpointz.
  bytes32_t checkpoint_root  = {0};
  uint64_t  checkpoint_epoch = 0;
  if (!c4_req_checkpointz_status(&ctx->state, ctx->chain_id, &checkpoint_epoch, checkpoint_root))
    return ctx->state.error ? C4_ERROR : C4_PENDING;

  // 2. Fetch the LightClientBootstrap for that checkpoint root.
  bytes_t bootstrap_data = {0};
  if (!req_bootstrap(&ctx->state, checkpoint_root, ctx->chain_id, &bootstrap_data))
    return ctx->state.error ? C4_ERROR : C4_PENDING;

  // 3. Parse the bootstrap SSZ and bind the header to the checkpointz root.
  fork_id_t        fork          = c4_eth_get_fork_for_lcu(ctx->chain_id, bootstrap_data);
  const ssz_def_t* bootstrap_def = eth_get_light_client_bootstrap(fork);
  if (!bootstrap_def) {
    clear_sync_state(ctx->chain_id);
    return c4_state_add_error(&ctx->state, "WSP bootstrap: unsupported fork");
  }
  ssz_ob_t        bootstrap = {.bytes = bootstrap_data, .def = bootstrap_def};
  data_request_t* src_req   = c4_state_get_data_request_by_response(&ctx->state, bootstrap_data);
  if (!(src_req && src_req->validated)) {
    if (!ssz_is_valid(bootstrap, true, &ctx->state)) {
      clear_sync_state(ctx->chain_id);
      return c4_state_add_error(&ctx->state, "WSP bootstrap: invalid SSZ structure");
    }
    if (src_req) src_req->validated = true;
  }

  ssz_ob_t header                 = ssz_get(&bootstrap, "header");
  ssz_ob_t beacon                 = ssz_get(&header, "beacon");
  uint64_t bootstrap_slot         = ssz_get_uint64(&beacon, "slot");
  ssz_ob_t current_sync_committee = ssz_get(&bootstrap, "currentSyncCommittee");
  ssz_ob_t bootstrap_pubkeys      = ssz_get(&current_sync_committee, "pubkeys");

  bytes32_t computed_blockhash = {0};
  ssz_hash_tree_root(beacon, computed_blockhash);
  if (memcmp(computed_blockhash, checkpoint_root, 32) != 0) {
    clear_sync_state(ctx->chain_id);
    return c4_state_add_error(&ctx->state, "WSP bootstrap: header root does not match checkpointz");
  }

  // 4. Compute the canonical pubkeys root for `bootstrap_period`.
  bytes32_t bootstrap_pubkeys_root = {0};
  ssz_hash_tree_root(bootstrap_pubkeys, bootstrap_pubkeys_root);

  uint32_t bootstrap_period = (uint32_t) (bootstrap_slot >> (spec->slots_per_epoch_bits + spec->epochs_per_period_bits));
  if (bootstrap_period == 0) {
    clear_sync_state(ctx->chain_id);
    return c4_state_add_error(&ctx->state, "WSP bootstrap: bootstrap period is zero");
  }

  // 5. Chain-of-trust: find the LCU whose `nextSyncCommittee.pubkeys` are the keys for
  //    `bootstrap_period` -- i.e. the LCU with `attested_slot` in `bootstrap_period - 1`.
  uint32_t  target_lcu_period = bootstrap_period - 1;
  bytes32_t lcu_pubkeys_root  = {0};
  bool      found_lcu         = false;
  for (data_request_t* req = ctx->state.requests; req && !found_lcu; req = req->next) {
    if (req->type != C4_DATA_TYPE_BEACON_API ||
        strncmp(req->url, "eth/v1/beacon/light_client/updates", 34) != 0 ||
        !req->response.data || req->response.len == 0 ||
        req->encoding != C4_DATA_ENCODING_SSZ)
      continue;

    bytes_t  client_updates = req->response;
    uint64_t length         = 0;
    for (uint32_t pos = 0; pos + UPDATE_PREFIX_SIZE < client_updates.len && !found_lcu; pos += length + SSZ_LENGTH_SIZE) {
      uint32_t data_offset        = pos + SSZ_LENGTH_SIZE + SSZ_OFFSET_SIZE;
      uint32_t data_length_offset = SSZ_OFFSET_SIZE;
      length                      = uint64_from_le(client_updates.data + pos);
      if (pos + SSZ_LENGTH_SIZE + length > client_updates.len && length > UPDATE_PREFIX_SIZE) break;

      bytes_t          client_update_bytes = bytes(client_updates.data + data_offset, length - data_length_offset);
      fork_id_t        lcu_fork            = c4_eth_get_fork_for_lcu(ctx->chain_id, client_update_bytes);
      const ssz_def_t* lcu_def             = eth_get_light_client_update(lcu_fork);
      if (!lcu_def) break;

      ssz_ob_t update          = {.bytes = client_update_bytes, .def = lcu_def};
      ssz_ob_t attested        = ssz_get(&update, "attestedHeader");
      ssz_ob_t attested_beacon = ssz_get(&attested, "beacon");
      uint64_t attested_slot   = ssz_get_uint64(&attested_beacon, "slot");
      uint32_t attested_period = (uint32_t) (attested_slot >> (spec->slots_per_epoch_bits + spec->epochs_per_period_bits));

      if (attested_period == target_lcu_period) {
        ssz_ob_t next_sync_committee = ssz_get(&update, "nextSyncCommittee");
        ssz_ob_t next_pubkeys        = ssz_get(&next_sync_committee, "pubkeys");
        ssz_hash_tree_root(next_pubkeys, lcu_pubkeys_root);
        found_lcu = true;
      }
    }
  }

  if (!found_lcu) {
    // The verifier did sync into `sync_state->highest_period` via LCUs but none of the
    // delivered updates straddles `bootstrap_period - 1`. This is the long-offline gap:
    // the LCU chain reached up to the latest fetched period but the bootstrap-anchor
    // points further ahead. Clear and force a fresh bootstrap on the next round.
    clear_sync_state(ctx->chain_id);
    return c4_state_add_error(&ctx->state, "WSP cross-check: no LCU covers the bootstrap period");
  }

  // 6. Cross-check the two trust paths.
  if (memcmp(lcu_pubkeys_root, bootstrap_pubkeys_root, 32) != 0) {
    clear_sync_state(ctx->chain_id);
    return c4_state_add_error(&ctx->state, "WSP cross-check failed: LCU and bootstrap pubkeys disagree");
  }

  return C4_SUCCESS;
}
#endif // USE_CHECKPOINTZ

/**
 * Pragmatic fallback to sync a period using the next period's previous_pubkeys_hash.
 *
 * ## The Period Transition Edge Case
 *
 * According to the Ethereum specification, a sync committee period change doesn't happen
 * at an exact slot boundary. Instead, the old sync committee remains active until the
 * FIRST FINALIZED BLOCK of the new period is produced. This creates a transition window
 * where blocks in the new period may still be signed by the old committee.
 *
 * ### The Problem
 * If we have stored period N+1 but need to verify a block at the START of period N+1
 * (before the first finality), we cannot use period N+1's keys because they weren't
 * active yet. We need period N's keys, but we might not have them cached.
 *
 * ### The Traditional Solution
 * We would need to:
 * 1. Fetch light_client_updates for period N
 * 2. Verify the entire chain of updates
 * 3. Prove WHEN the first finalized block occurred
 * 4. Determine which keys were actually active
 *
 * This is complex and requires additional proofs.
 *
 * ### Our Pragmatic Solution
 * When storing period N+1, we also store SHA256(period N's keys) as previous_pubkeys_hash.
 *
 * If signature verification fails with period N+1's keys:
 * 1. Check if we have period N+1 cached (highest_period == period + 1)
 * 2. Fetch the light_client_update for period N
 * 3. Extract nextSyncCommittee (which represents period N+1's keys)
 * 4. Hash these keys and compare against the stored previous_pubkeys_hash
 * 5. If match: use the keys from step 3 for verification
 *
 * This avoids needing explicit proofs about finality timing while maintaining security
 * through cryptographic verification against a trusted hash.
 *
 * @param ctx Verification context
 * @param period The period we're trying to sync
 * @param sync_state Current validator state (must have highest_period == period + 1)
 * @return C4_SUCCESS if sync successful or not applicable, C4_ERROR on failure, C4_PENDING if waiting
 */
static c4_status_t c4_try_sync_from_next_period(verify_ctx_t* ctx, uint32_t period, c4_sync_validators_t* sync_state) {
  // Check if this edge case applies: we have period+1 but not period
  if (sync_state->highest_period != period + 1)
    return C4_SUCCESS; // Not applicable - this is not an error, just means edge case doesn't apply

  // Verify we have an anchor point (either lowest period or the next period)
  if (sync_state->lowest_period == 0 || sync_state->lowest_period > period)
    THROW_ERROR("Failed to get previous validators, because there is no anchor like the following period.");

  // Step 1: Retrieve period N+1 from cache to get its previous_pubkeys_hash
  c4_sync_validators_t next_sync_state = get_validators_from_cache(ctx, period + 1);
  if (next_sync_state.validators.data == NULL)
    THROW_ERROR("Failed to get previous validators, because there is no anchor like the following period.");

  // Step 2: Extract the stored hash of period N's keys from period N+1's metadata
  bytes32_t previous_hash = {0}; // This is SHA256(period N's validator keys)
  bytes32_t no_prev       = {0}; // Zero hash for the keys we're about to fetch (they have no predecessor in our use)
  memcpy(previous_hash, next_sync_state.previous_pubkeys_hash, 32);
#ifndef C4_STATIC_MEMORY
  // Free next_sync_state validators as we only needed the previous_pubkeys_hash
  safe_free(next_sync_state.validators.data);
  safe_free(sync_state->validators.data);
#endif

  // Step 3: Fetch the light_client_update for period N from Beacon API
  bytes_t   light_client_update = {0};
  bytes32_t computed_root       = {0};
  if (req_client_update(&ctx->state, period, 1, ctx->chain_id, &light_client_update)) {
    // Parse the SSZ-encoded update
    fork_id_t        fork              = c4_eth_get_fork_for_lcu(ctx->chain_id, light_client_update);
    const ssz_def_t* client_update_def = eth_get_light_client_update(fork);

    if (!client_update_def || light_client_update.len < UPDATE_PREFIX_SIZE)
      THROW_ERROR("Invalid light client update format in edge case sync");

    // Navigate to the first update in the list
    uint32_t offset = uint32_from_le(light_client_update.data);
    if (offset + UPDATE_PREFIX_SIZE > light_client_update.len)
      THROW_ERROR("Invalid offset in light client update list");

    uint64_t length                    = uint64_from_le(light_client_update.data + offset);
    bytes_t  light_client_update_bytes = bytes(light_client_update.data + offset + UPDATE_PREFIX_SIZE, length - SSZ_OFFSET_SIZE);
    ssz_ob_t        update_ob = {.bytes = light_client_update_bytes, .def = client_update_def};
    data_request_t* src_req   = c4_state_get_data_request_by_response(&ctx->state, light_client_update);
    if (!(src_req && src_req->validated)) {
      if (!ssz_is_valid(update_ob, true, &ctx->state))
        THROW_ERROR("Invalid SSZ structure in light client update");
      if (src_req) src_req->validated = true;
    }

    // Step 4: Extract nextSyncCommittee from the update (this is period N+1's committee in period N's update)
    ssz_ob_t next_sync_committee = ssz_get(&update_ob, "nextSyncCommittee");
    if (ssz_is_error(next_sync_committee))
      THROW_ERROR("Failed to extract nextSyncCommittee from light client update");

    // Step 5: Compute the hash of these keys (optionally deserialize for efficiency)
#ifdef BLS_DESERIALIZE
    bytes_t b = blst_deserialize_p1_affine(ssz_get(&next_sync_committee, "pubkeys").bytes.data, 512, NULL);
#else
    bytes_t b = bytes_dup(ssz_get(&next_sync_committee, "pubkeys").bytes);
#endif
    sha256(b, computed_root);

    // Step 6: Verify computed hash matches the stored previous_pubkeys_hash
    // This proves the keys we fetched are indeed period N's keys
    bool valid = memcmp(previous_hash, computed_root, 32) == 0;
    if (valid)
      sync_state->validators = b; // Success! Use these keys for verification
    else {
      safe_free(b.data);
      THROW_ERROR("Sync committee root mismatch in period transition edge case");
    }

    // Step 7: Store period N's keys for future use (with no previous hash since we're using fallback)
    if (!c4_set_sync_period(period, ssz_get(&next_sync_committee, "pubkeys").bytes, ctx->chain_id, no_prev)) {
      safe_free(b.data);
      THROW_ERROR("Failed to store sync committee for period transition");
    }

    return C4_SUCCESS;
  }
  else
    return ctx->state.error ? C4_ERROR : C4_PENDING;
}

/**
 * Main entry point to retrieve sync committee validators for a given period.
 * Implements a multi-strategy approach: cache lookup → initialization → edge case fallback → normal sync.
 *
 * The workflow ensures we can always obtain validators even for periods we haven't cached yet,
 * handling the special case of delayed finality at period boundaries via previous_pubkeys_hash.
 *
 * @param ctx Verification context
 * @param period Period number to retrieve validators for
 * @param target_state Output parameter for the validator state
 * @param pubkey_hash Optional output parameter for SHA256 hash of the pubkeys
 * @return C4_SUCCESS if validators obtained, C4_ERROR on failure, C4_PENDING if waiting for requests
 */
INTERNAL const c4_status_t c4_get_validators(verify_ctx_t* ctx, uint32_t period, c4_sync_validators_t* target_state, bytes32_t pubkey_hash) {
  // Try to retrieve from persistent cache first
  c4_sync_validators_t sync_state = get_validators_from_cache(ctx, period);

  if (sync_state.validators.data == NULL) {
    // Strategy 1: No cached periods exist - initialize from trusted checkpoint
    if (sync_state.lowest_period == 0) {
      if (sync_state.highest_period) {
        // The cache only holds periods *newer* than the requested one, so there is no
        // period we could sync forward from. A forward-only light client cannot obtain an
        // older committee, hence the "cannot sync backwards" error.
        //
        // We attempt a *one-time*, *evidence-gated* self-healing recovery for the single
        // legitimate case that produces this state: the cache advanced ahead of the real
        // finalized head (e.g. via an earlier head-based update near a period boundary) and
        // a later request needs a finalized block from a slightly older period.
        //
        // SECURITY: `period` is derived from the slot of an untrusted proof header and is
        // signature-verified only *after* this call (see beacon_header.c). We must therefore
        // NOT clear the validated sync-committee cache based on `period` alone -- a malicious
        // prover could otherwise supply an artificially old slot to force a repeated cache
        // wipe + re-bootstrap (DoS). Instead we require independent evidence from the trusted
        // checkpointz finalized head that the cache is genuinely ahead of the chain tip.
        if (ctx->flags & VERIFY_FLAG_SYNC_REINIT_TRIED)
          THROW_ERROR("the last sync state is higher than the required period, but we cannot sync backwards");

#ifdef USE_CHECKPOINTZ
        // Fail closed: never wipe a validated cache without a valid chain spec.
        const chain_spec_t* spec = c4_eth_get_chain_spec(ctx->chain_id);
        if (!spec) THROW_ERROR("unsupported chain id!");

        uint64_t  checkpoint_epoch = 0;
        bytes32_t checkpoint_root  = {0};
        if (!c4_req_checkpointz_status(&ctx->state, ctx->chain_id, &checkpoint_epoch, checkpoint_root))
          return ctx->state.error ? C4_ERROR : C4_PENDING;
        uint32_t checkpoint_period = (uint32_t) (checkpoint_epoch >> spec->epochs_per_period_bits);

        // Only self-heal the narrow legitimate case: the cache is genuinely ahead of the
        // finalized head (`checkpoint_period < highest_period`) AND the requested period is
        // reachable by re-bootstrapping to the finalized checkpoint and syncing forward
        // (`period >= checkpoint_period`). Anything older than the finalized head is a true
        // "cannot sync backwards" case that a forward-only client cannot serve -- surface the
        // error without touching the validated cache.
        if (checkpoint_period >= sync_state.highest_period || period < checkpoint_period)
          THROW_ERROR("the last sync state is higher than the required period, but we cannot sync backwards");

        // Discard the stale (ahead-of-finality) state and re-bootstrap from the trusted
        // checkpoint. The guard flag (persisted on the ctx across C4_PENDING rounds) ensures
        // this is attempted at most once, so a genuinely-older `period` cannot loop.
        ctx->flags |= VERIFY_FLAG_SYNC_REINIT_TRIED;
        clear_sync_state(ctx->chain_id);
        TRY_ASYNC(init_sync_state(ctx));
        return c4_get_validators(ctx, period, target_state, pubkey_hash);
#else
        THROW_ERROR("the last sync state is higher than the required period, but we cannot sync backwards");
#endif
      }
      TRY_ASYNC(init_sync_state(ctx));
      // Recursively call to retrieve the period after initialization
      return c4_get_validators(ctx, period, target_state, NULL);
    }

    // Strategy 2: Edge case - we have period+1 but not period (delayed finality at boundary)
    // Uses previous_pubkeys_hash to verify period N's keys without full chain proof
    TRY_ASYNC(c4_try_sync_from_next_period(ctx, period, &sync_state));

    // Check if edge case fallback succeeded
    if (sync_state.validators.data) {
      *target_state = sync_state;
      return C4_SUCCESS;
    }

    // Strategy 3: Normal sync path - fetch light_client_updates from lowest to target period
    bytes_t light_client_updates = {0};
    if (req_client_update(&ctx->state, sync_state.lowest_period, sync_state.current_period - sync_state.lowest_period, ctx->chain_id, &light_client_updates)) {
      if (!c4_handle_client_updates(ctx, light_client_updates))
        return c4_state_get_pending_request(&ctx->state) ? C4_PENDING : c4_state_add_error(&ctx->state, "Failed to handle light client updates");
    }
    else
      return ctx->state.error ? C4_ERROR : C4_PENDING;

    // Check weak subjectivity period BEFORE loading new sync state
    // If this fails, clear_sync_state() is called inside to force re-initialization
#ifdef USE_CHECKPOINTZ
    TRY_ASYNC(c4_check_weak_subjectivity(ctx, &sync_state, period));
#endif

    // Load new sync state after successful WSP check
    sync_state = get_validators_from_cache(ctx, period);
    if (sync_state.validators.data == NULL) return c4_state_add_error(&ctx->state, "Failed to get validators");
  }

  *target_state = sync_state;
  if (pubkey_hash)
    sha256(sync_state.validators, pubkey_hash);

  return C4_SUCCESS;
}
