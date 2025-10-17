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

#include "../util/compat.h"
#include "../util/crypto.h"
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

uint32_t c4_eth_get_last_period(bytes_t state) {
  c4_trusted_block_t* blocks      = (c4_trusted_block_t*) state.data;
  uint32_t            len         = state.len / sizeof(c4_trusted_block_t);
  uint32_t            last_period = 0;
  for (int i = 0; i < len; i++) {
    uint32_t p = uint32_from_le(blocks[i].period_bytes);
    if (!last_period || last_period > p) last_period = p;
  }
  return last_period;
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

  sprintf(name, "states_%" PRIu64, (uint64_t) chain_id);
#ifndef C4_STATIC_MEMORY
  tmp.allocated = sizeof(c4_trusted_block_t) * storage_conf.max_sync_states;
#endif

  if (storage_conf.get(name, &tmp) && tmp.data.data) {
    state.blocks          = (c4_trusted_block_t*) tmp.data.data;
    state.len             = tmp.data.len / sizeof(c4_trusted_block_t);
    state.last_checkpoint = (tmp.data.len % sizeof(c4_trusted_block_t) == 8) ? uint64_from_le(tmp.data.data + tmp.data.len - 8) : 0;
  }

  return state;
}

void c4_eth_set_trusted_blockhashes(chain_id_t chain_id, bytes_t blockhashes) {
  if (blockhashes.len == 0 || blockhashes.data == NULL || blockhashes.len % 32 != 0 || chain_id == 0) return;
  c4_chain_state_t state = c4_get_chain_state(chain_id);
  if (state.len == 0) {
    char             name[100];
    storage_plugin_t storage_conf = {0};
    c4_get_storage_config(&storage_conf);
    state.blocks = safe_calloc(blockhashes.len / 32, sizeof(c4_trusted_block_t));
    state.len    = blockhashes.len / 32;
    for (int i = 0; i < state.len; i++)
      memcpy(state.blocks[i].blockhash, blockhashes.data + i * 32, 32);
    sprintf(name, "states_%" PRIu64, (uint64_t) chain_id);
    storage_conf.set(name, bytes(state.blocks, state.len * sizeof(c4_trusted_block_t)));
  }
#ifndef C4_STATIC_MEMORY
  safe_free(state.blocks);
#endif
}

static bool req_header(c4_state_t* state, json_t slot, chain_id_t chain_id, json_t* data) {
  buffer_t tmp = {0};
  if (slot.type == JSON_TYPE_STRING)
    bprintf(&tmp, "eth/v1/beacon/headers/%j", slot);
  else
    bprintf(&tmp, "eth/v1/beacon/headers/head");

  data_request_t* req = c4_state_get_data_request_by_url(state, (char*) tmp.data.data);
  if (req) buffer_free(&tmp);
  if (req && req->response.data) {
    json_t res = json_parse((char*) req->response.data);
    if (res.type == JSON_TYPE_OBJECT) res = json_get(res, "data");
    if (res.type == JSON_TYPE_OBJECT) res = json_get(res, "header");
    if (res.type == JSON_TYPE_OBJECT) res = json_get(res, "message");
    if (res.type == JSON_TYPE_OBJECT) {
      *data = res;
      return true;
    }
    else {
      state->error = strdup("Invalid response for header");
      return false;
    }
  }
  else if (req && req->error) {
    state->error = strdup(req->error);
    return false;
  }
  data_request_t* new_req = safe_calloc(1, sizeof(data_request_t));
  new_req->chain_id       = chain_id;
  new_req->url            = (char*) tmp.data.data;
  new_req->encoding       = C4_DATA_ENCODING_JSON;
  new_req->type           = C4_DATA_TYPE_BEACON_API;

  c4_state_add_request(state, new_req);

  return false;
}

static inline int trusted_blocks_len(c4_chain_state_t chain_state) {
  int len = 0;
  for (int i = 0; i < chain_state.len; i++)
    len += uint64_from_le(chain_state.blocks[i].slot_bytes) ? 0 : 1;
  return len;
}

static bool req_client_update(c4_state_t* state, uint32_t period, uint32_t count, chain_id_t chain_id, bytes_t* data) {
  buffer_t tmp = {0};
  bprintf(&tmp, "eth/v1/beacon/light_client/updates?start_period=%d&count=%d", period, count);

  data_request_t* req = c4_state_get_data_request_by_url(state, (char*) tmp.data.data);
  if (req) buffer_free(&tmp);
  if (req && req->response.data) {
    *data = req->response;
    return true;
  }
  else if (req && req->error) {
    state->error = strdup(req->error);
    return false;
  }
  data_request_t* new_req = safe_calloc(1, sizeof(data_request_t));
  new_req->chain_id       = chain_id;
  new_req->url            = (char*) tmp.data.data;
  new_req->encoding       = C4_DATA_ENCODING_SSZ;
  new_req->type           = C4_DATA_TYPE_BEACON_API;
  c4_state_add_request(state, new_req);
  return false;
}

static bool req_checkpointz_status(c4_state_t* state, chain_id_t chain_id, uint64_t* checkpoint_epoch, bytes32_t checkpoint_root) {
  buffer_t tmp = {0};
  bprintf(&tmp, "eth/v1/beacon/states/head/finality_checkpoints");

  data_request_t* req = c4_state_get_data_request_by_url(state, (char*) tmp.data.data);
  if (req) buffer_free(&tmp);
  if (req && req->response.data) {
    json_t res = json_parse((char*) req->response.data);

    // Validate JSON structure (Beacon API compatible format)
    const char* err = json_validate(res, "{data:{finalized:{epoch:suint,root:bytes32}}}", "finality checkpoints");
    if (err) {
      c4_state_add_error(state, err);
      return false;
    }

    json_t data      = json_get(res, "data");
    json_t finalized = json_get(data, "finalized");
    json_t epoch     = json_get(finalized, "epoch");
    json_t root      = json_get(finalized, "root");

    *checkpoint_epoch = json_as_uint64(epoch);
    buffer_t root_buf = {.data = bytes(checkpoint_root, 32), .allocated = -32};
    json_as_bytes(root, &root_buf);
    return true;
  }
  else if (req && req->error) {
    state->error = strdup(req->error);
    return false;
  }
  data_request_t* new_req = safe_calloc(1, sizeof(data_request_t));
  new_req->chain_id       = chain_id;
  new_req->url            = (char*) tmp.data.data;
  new_req->encoding       = C4_DATA_ENCODING_JSON;
  new_req->type           = C4_DATA_TYPE_CHECKPOINTZ;
  c4_state_add_request(state, new_req);
  return false;
}

static bool req_bootstrap(c4_state_t* state, bytes32_t block_root, chain_id_t chain_id, bytes_t* data) {
  buffer_t tmp = {0};
  bprintf(&tmp, "eth/v1/beacon/light_client/bootstrap/0x%x", bytes(block_root, 32));

  data_request_t* req = c4_state_get_data_request_by_url(state, (char*) tmp.data.data);
  if (req) buffer_free(&tmp);
  if (req && req->response.data) {
    *data = req->response;
    return true;
  }
  else if (req && req->error) {
    c4_state_add_error(state, req->error);
    return false;
  }
  data_request_t* new_req = safe_calloc(1, sizeof(data_request_t));
  new_req->chain_id       = chain_id;
  new_req->url            = (char*) tmp.data.data;
  new_req->encoding       = C4_DATA_ENCODING_SSZ;
  new_req->type           = C4_DATA_TYPE_BEACON_API;
  c4_state_add_request(state, new_req);
  return false;
}

static c4_status_t c4_handle_bootstrap(verify_ctx_t* ctx, bytes_t bootstrap_data, bytes32_t trusted_checkpoint) {
  // Parse bootstrap data as SSZ
  fork_id_t        fork                 = c4_eth_get_fork_for_lcu(ctx->chain_id, bootstrap_data);
  const ssz_def_t* bootstrap_def_list   = (fork <= C4_FORK_DENEB) ? DENEP_LIGHT_CLIENT_BOOTSTRAP : ELECTRA_LIGHT_CLIENT_BOOTSTRAP;
  const ssz_def_t  bootstrap_def        = {.name = "LightClientBootstrap", .type = SSZ_TYPE_CONTAINER, .def.container = {.elements = bootstrap_def_list, .len = 3}};
  ssz_ob_t         bootstrap            = {.bytes = bootstrap_data, .def = &bootstrap_def};
  bytes32_t        previous_pubkey_hash = {0}; // in case of a bootstrap, there is no previous pubkey hash, so we set it to 0

  // Validate SSZ structure (checks offsets and ensures all properties exist)
  if (!ssz_is_valid(bootstrap, true, &ctx->state))
    THROW_ERROR("Invalid SSZ structure in bootstrap data");

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

  // Current sync committee gindex depends on fork
  const chain_spec_t* spec                          = c4_eth_get_chain_spec(ctx->chain_id);
  fork_id_t           slot_fork                     = c4_chain_fork_id(ctx->chain_id, epoch_for_slot(slot, spec));
  uint64_t            current_sync_committee_gindex = (slot_fork == C4_FORK_DENEB) ? 54 : 86; // DENEB: 54, ELECTRA: 86

  ssz_verify_single_merkle_proof(sync_committee_branch.bytes, sync_root, current_sync_committee_gindex, merkle_root);

  if (memcmp(merkle_root, state_root.bytes.data, 32))
    THROW_ERROR("Invalid merkle proof in bootstrap");

  // Calculate current period from slot (no +1 because this is currentSyncCommittee)
  uint32_t period = (slot >> (spec->slots_per_epoch_bits + spec->epochs_per_period_bits));

  // Save sync committee for current period
  return c4_set_sync_period(period, slot, blockhash, current_sync_committee, ctx->chain_id, previous_pubkey_hash) ? C4_SUCCESS : C4_ERROR;
}

INTERNAL bool c4_set_sync_period(uint32_t period, uint64_t slot, bytes32_t blockhash, ssz_ob_t sync_committee, chain_id_t chain_id, bytes32_t previous_pubkey_hash) {
  const chain_spec_t* spec          = c4_eth_get_chain_spec(chain_id);
  storage_plugin_t    storage_conf  = {0};
  c4_chain_state_t    state         = c4_get_chain_state(chain_id);
  uint32_t            allocated_len = state.len;
  char                name[100];

  if (!spec) return false;

  // Extract validators (pubkeys) from sync committee
  bytes_t validators = ssz_get(&sync_committee, "pubkeys").bytes;

  // check if we had only trusted blocks
  if (trusted_blocks_len(state)) {
    safe_free(state.blocks);
    state.blocks  = NULL;
    state.len     = 0;
    allocated_len = 0;
  }

  c4_get_storage_config(&storage_conf);

  while (state.len >= storage_conf.max_sync_states && state.blocks) {
    uint32_t oldest       = 0;
    uint32_t latest       = 0;
    int      oldest_index = 0;

    // find the oldest and latest period
    for (int i = 0; i < state.len; i++) {
      uint32_t p = uint32_from_le(state.blocks[i].period_bytes);
      if (p > latest || latest == 0)
        latest = p;
      if (p < oldest || oldest == 0) {
        oldest       = p;
        oldest_index = i;
      }
    }

    if (state.len > 2) {
      // we want to keep the oldest and the latest, but remove the second oldest
      uint32_t oldest_2nd       = 0;
      int      oldest_2nd_index = 0;
      for (int i = 0; i < state.len; i++) {
        uint32_t p = uint32_from_le(state.blocks[i].period_bytes);
        if (p > oldest && p < latest && (p < oldest_2nd || oldest_2nd == 0)) {
          oldest_2nd       = p;
          oldest_2nd_index = i;
        }
      }
      oldest_index = oldest_2nd_index;
      oldest       = oldest_2nd;
    }

    sprintf(name, "sync_%" PRIu64 "_%d", (uint64_t) chain_id, oldest);
    storage_conf.del(name);
    if (oldest_index < state.len - 1) memmove(state.blocks + oldest_index, state.blocks + oldest_index + 1, (state.len - oldest_index - 1) * sizeof(c4_trusted_block_t));
    state.len--;
  }

#ifdef C4_STATIC_MEMORY
  state.blocks = (c4_trusted_block_t*) state_buffer;
#else
  if (allocated_len == 0)
    state.blocks = safe_calloc(sizeof(c4_trusted_block_t), 1);
  else if (allocated_len < state.len + 1)
    state.blocks = safe_realloc(state.blocks, sizeof(c4_trusted_block_t) * (state.len + 1));
#endif
  if (!state.blocks) return false; // Safety check for static analyzer
  uint64_to_le(state.blocks[state.len].slot_bytes, slot);
  uint32_to_le(state.blocks[state.len].period_bytes, period);
  memcpy(state.blocks[state.len].blockhash, blockhash, 32);
  state.len++;

  // Store validators + sync_root (32 bytes appended)
  sprintf(name, "sync_%" PRIu64 "_%d", (uint64_t) chain_id, period);
  buffer_t storage_data = {0};
  buffer_append(&storage_data, validators);
  buffer_append(&storage_data, bytes(previous_pubkey_hash, 32));
  storage_conf.set(name, storage_data.data);
  buffer_free(&storage_data);

  sprintf(name, "states_%" PRIu64, (uint64_t) chain_id);
  storage_conf.set(name, bytes(state.blocks, state.len * sizeof(c4_trusted_block_t)));

#ifndef C4_STATIC_MEMORY
  safe_free(state.blocks);
#endif

  return true;
}

static c4_status_t init_sync_state(verify_ctx_t* ctx) {
  c4_chain_state_t    chain_state      = c4_get_chain_state(ctx->chain_id);
  c4_state_t*         state            = &ctx->state;
  bool                success          = false;
  bytes_t             bootstrap_data   = {0};
  bytes_t             client_update    = {0};
  bytes32_t           checkpoint_root  = {0};
  uint64_t            checkpoint_epoch = 0;
  const chain_spec_t* spec             = c4_eth_get_chain_spec(ctx->chain_id);

  if (!spec) THROW_ERROR("unsupported chain id!");

  if (chain_state.len == 0) {
#ifndef C4_STATIC_MEMORY
    safe_free(chain_state.blocks);
#endif

    // No state exists - fetch checkpoint from checkpointz server
    success = req_checkpointz_status(state, ctx->chain_id, &checkpoint_epoch, checkpoint_root);
    if (success) {
      // Set the checkpoint as trusted blockhash
      c4_eth_set_trusted_blockhashes(ctx->chain_id, bytes(checkpoint_root, 32));
      // Recursively call init_sync_state to process the bootstrap with the new trusted checkpoint
      return init_sync_state(ctx);
    }
    else
      // Request is either pending or failed
      return state->error ? C4_ERROR : C4_PENDING;
  }
  else if (trusted_blocks_len(chain_state)) {
    // We have a trusted checkpoint - use bootstrap
    bytes32_t trusted_checkpoint = {0};
    for (int i = 0; i < chain_state.len; i++) {
      if (!uint64_from_le(chain_state.blocks[i].slot_bytes)) {
        memcpy(trusted_checkpoint, chain_state.blocks[i].blockhash, 32);
        break;
      }
    }

    success = req_bootstrap(state, trusted_checkpoint, ctx->chain_id, &bootstrap_data);
    if (success) {
      TRY_ASYNC(c4_handle_bootstrap(ctx, bootstrap_data, trusted_checkpoint));
    }
  }
  else {
    // This case should not occur - init_sync_state is only called when there's no state
    // If we have sync committee states, c4_get_validators should handle the sync
#ifndef C4_STATIC_MEMORY
    safe_free(chain_state.blocks);
#endif
    THROW_ERROR("init_sync_state called with existing sync committee state");
  }

#ifndef C4_STATIC_MEMORY
  safe_free(chain_state.blocks);
#endif
  return state->error ? C4_ERROR : (c4_state_get_pending_request(state) ? C4_PENDING : C4_SUCCESS);
}

static c4_sync_state_t get_validators_from_cache(verify_ctx_t* ctx, uint32_t period) {
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
  bool found = false;
  c4_get_storage_config(&storage_conf);

  for (uint32_t i = 0; i < chain_state.len; i++) {
    uint32_t p = uint32_from_le(chain_state.blocks[i].period_bytes);
    if (p == period) found = true;
    lowest_period  = p > lowest_period && p <= period ? p : lowest_period;
    highest_period = p > highest_period ? p : highest_period;
  }
#ifndef C4_STATIC_MEMORY
  safe_free(chain_state.blocks);
#endif
  char name[100];
  sprintf(name, "sync_%" PRIu64 "_%d", (uint64_t) ctx->chain_id, period);

  if (found && storage_conf.get) storage_conf.get(name, &validators);
  if (validators.data.len % 48 == 32)
    memcpy(previous_root, validators.data.data + validators.data.len - 32, 32);

#ifdef BLS_DESERIALIZE
  // Check if validators need deserialization (only pubkeys, not including the 32-byte root)
  size_t expected_serialized_size = 512 * 48 + 32;
  if (validators.data.data && validators.data.len == expected_serialized_size) {
#ifdef C4_STATIC_MEMORY
    memcpy(keys_48_buffer, validators.data.data, 512 * 48);
    bytes_t b = blst_deserialize_p1_affine(validators.data.data, 512, sync_buffer);
#else
    bytes_t b = blst_deserialize_p1_affine(validators.data.data, 512, NULL);
    buffer_free(&validators);
#endif
    validators.data = b;
    storage_conf.set(name, b);
  }
  else if (validators.data.data && validators.data.len == 512 * 48 + 32) {
    // Old format without deserialization - just strip the root
    validators.data.len = 512 * 48;
  }
#else
  // Strip the 32-byte root from validators data if present
  if (validators.data.len >= 32) {
    validators.data.len -= 32;
  }
#endif

  if (validators.data.len == 0) validators.data.data = NULL; // just to make sure we mark it as not found, even if we are using static memory

  c4_sync_state_t result = {
      .deserialized    = validators.data.data && validators.data.len > 512 * 48,
      .current_period  = period,
      .lowest_period   = lowest_period,
      .highest_period  = highest_period,
      .last_checkpoint = chain_state.last_checkpoint,
      .validators      = validators.data};
  memcpy(result.previous_pubkeys_hash, previous_root, 32);
  return result;
}

// Helper function to clear all sync state on critical errors
static void clear_sync_state(chain_id_t chain_id) {
  storage_plugin_t storage_conf = {0};
  c4_get_storage_config(&storage_conf);

  // Delete all sync states for this chain
  c4_chain_state_t chain_state = c4_get_chain_state(chain_id);
  for (uint32_t i = 0; i < chain_state.len; i++) {
    uint32_t p = uint32_from_le(chain_state.blocks[i].period_bytes);
    char     name[100];
    sprintf(name, "sync_%" PRIu64 "_%d", (uint64_t) chain_id, p);
    storage_conf.del(name);
  }

  // Delete chain state
  char name[100];
  sprintf(name, "states_%" PRIu64, (uint64_t) chain_id);
  storage_conf.del(name);

#ifndef C4_STATIC_MEMORY
  safe_free(chain_state.blocks);
#endif
}

static c4_status_t c4_check_weak_subjectivity(verify_ctx_t* ctx, c4_sync_state_t* sync_state, uint32_t target_period) {
  const chain_spec_t* spec = c4_eth_get_chain_spec(ctx->chain_id);
  if (!spec) return C4_SUCCESS; // Cannot validate without spec

  // Calculate period difference
  if (target_period <= sync_state->highest_period) return C4_SUCCESS; // No gap, no need to check

  uint32_t period_diff = target_period - sync_state->highest_period;
  uint64_t epoch_diff  = ((uint64_t) period_diff) << spec->epochs_per_period_bits;

  // Check if we exceed weak subjectivity period
  if (epoch_diff <= spec->weak_subjectivity_epochs) return C4_SUCCESS; // Within WSP

  // We exceeded WSP - need to validate against checkpointz
  if (sync_state->last_checkpoint == 0) {
    // No checkpoint to validate against - clear state and force re-initialization
    clear_sync_state(ctx->chain_id);
    return c4_state_add_error(&ctx->state, "Weak subjectivity period exceeded without checkpoint");
  }

  // Request block root from checkpointz for the finality checkpoint
  uint64_t finality_slot = sync_state->last_checkpoint;

  buffer_t url_buf = {0};
  bprintf(&url_buf, "eth/v1/beacon/blocks/%l/root", finality_slot);

  data_request_t* req = c4_state_get_data_request_by_url(&ctx->state, (char*) url_buf.data.data);
  if (!req) {
    // Create new request
    data_request_t* new_req = safe_calloc(1, sizeof(data_request_t));
    new_req->chain_id       = ctx->chain_id;
    new_req->url            = (char*) url_buf.data.data;
    new_req->encoding       = C4_DATA_ENCODING_JSON;
    new_req->type           = C4_DATA_TYPE_CHECKPOINTZ;
    c4_state_add_request(&ctx->state, new_req);
    return C4_PENDING;
  }

  buffer_free(&url_buf);

  if (req->error)
    return c4_state_add_error(&ctx->state, req->error);

  if (req->response.data) {
    // Parse JSON response: {"data":{"root":"0x..."}}
    json_t res = json_parse((char*) req->response.data);

    // Validate JSON structure
    const char* err = json_validate(res, "{data:{root:bytes32}}", "checkpointz block root");
    if (err) {
      return c4_state_add_error(&ctx->state, err);
    }

    json_t data = json_get(res, "data");
    json_t root = json_get(data, "root");

    bytes32_t checkpointz_root = {0};
    buffer_t  root_buf         = {.data = bytes(checkpointz_root, 32), .allocated = -32};
    json_as_bytes(root, &root_buf);

    // Compare with our finality checkpoint
    // We need to get the blockhash from our stored state for this checkpoint
    c4_chain_state_t chain_state    = c4_get_chain_state(ctx->chain_id);
    bytes32_t        our_checkpoint = {0};
    bool             found          = false;

    for (uint32_t i = 0; i < chain_state.len; i++) {
      uint64_t stored_slot = uint64_from_le(chain_state.blocks[i].slot_bytes);
      if (stored_slot == finality_slot) {
        memcpy(our_checkpoint, chain_state.blocks[i].blockhash, 32);
        found = true;
        break;
      }
    }

#ifndef C4_STATIC_MEMORY
    safe_free(chain_state.blocks);
#endif

    if (!found) {
      clear_sync_state(ctx->chain_id);
      return c4_state_add_error(&ctx->state, "Checkpoint slot not found in local state");
    }

    if (memcmp(checkpointz_root, our_checkpoint, 32) != 0) {
      clear_sync_state(ctx->chain_id);
      return c4_state_add_error(&ctx->state, "Weak subjectivity check failed: checkpoint mismatch");
    }

    return C4_SUCCESS;
  }

  return C4_PENDING;
}

// Helper function to sync period from next period's previous_root
// This handles the edge case where we have period+1 but not period
static c4_status_t c4_try_sync_from_next_period(verify_ctx_t* ctx, uint32_t period, c4_sync_state_t* sync_state) {
  // Edge case: We have the next period but not the current one
  // This can happen when finality was delayed at period transition
  // We can verify against the previous_sync_committee_root of the next period
  if (sync_state->highest_period != period + 1)
    return C4_SUCCESS; // Not applicable - this is not an error, just means edge case doesn't apply
  // would it be possible to sync form the lowest period?
  if (sync_state->lowest_period == 0 || sync_state->lowest_period > period) THROW_ERROR("Failed to get previous validators, because there is no anchor like the the following period.");

  // get the sync_state from the next period
  c4_sync_state_t next_sync_state = get_validators_from_cache(ctx, period + 1);
  if (next_sync_state.validators.data == NULL) THROW_ERROR("Failed to get previous validators, because there is no anchor like the the following period.");

  bytes32_t previous_hash = {0};
  bytes32_t no_prev       = {0};
  memcpy(previous_hash, sync_state->previous_pubkeys_hash, 32);
  safe_free(sync_state->validators.data);

  // Fetch light client update for the current period
  bytes_t   client_update = {0};
  bytes32_t computed_root = {0};
  if (req_client_update(&ctx->state, period, 1, ctx->chain_id, &client_update)) {
    // Parse the update and extract nextSyncCommittee
    fork_id_t        fork               = c4_eth_get_fork_for_lcu(ctx->chain_id, client_update);
    const ssz_def_t* client_update_list = eth_get_light_client_update_list(fork);

    if (!client_update_list || client_update.len < 12)
      THROW_ERROR("Invalid light client update format in edge case sync");

    // Extract first update from list
    uint32_t offset = uint32_from_le(client_update.data);
    if (offset + 12 > client_update.len)
      THROW_ERROR("Invalid offset in light client update list");

    uint64_t length              = uint64_from_le(client_update.data + offset);
    bytes_t  client_update_bytes = bytes(client_update.data + offset + 12, length - 4);
    ssz_ob_t update_ob           = {.bytes = client_update_bytes, .def = client_update_list->def.vector.type};
    ssz_ob_t next_sync_committee = ssz_get(&update_ob, "nextSyncCommittee");

    if (ssz_is_error(next_sync_committee))
      THROW_ERROR("Failed to extract nextSyncCommittee from light client update");

#ifdef BLS_DESERIALIZE
    bytes_t b = blst_deserialize_p1_affine(ssz_get(&next_sync_committee, "pubkeys").bytes.data, 512, NULL);
#else
    bytes_t b = bytes_dup(ssz_get(&next_sync_committee, "pubkeys").bytes);
#endif
    sha256(b, computed_root);

    bool valid = memcmp(previous_hash, computed_root, 32) == 0;
    if (valid)
      sync_state->validators = b;
    else {
      safe_free(b.data);
      THROW_ERROR("Sync committee root mismatch in period transition edge case");
    }

    // Root matches - extract slot and blockhash from finalized header
    ssz_ob_t  finalized = ssz_get(&update_ob, "finalizedHeader");
    ssz_ob_t  header    = ssz_get(&finalized, "beacon");
    uint64_t  slot      = ssz_get_uint64(&header, "slot");
    bytes32_t blockhash = {0};
    ssz_hash_tree_root(header, blockhash);

    // Store the sync committee for this period (+1 because nextSyncCommittee is for next period)
    const chain_spec_t* spec = c4_eth_get_chain_spec(ctx->chain_id);
    if (!c4_set_sync_period(period, slot, blockhash, next_sync_committee, ctx->chain_id, no_prev)) {
      safe_free(b.data);
      THROW_ERROR("Failed to store sync committee for period transition");
    }
    else {
      // TODO write to storage
      return C4_SUCCESS;
    }

    return C4_SUCCESS;
  }
  else
    return ctx->state.error ? C4_ERROR : C4_PENDING;
}

INTERNAL const c4_status_t c4_get_validators(verify_ctx_t* ctx, uint32_t period, c4_sync_state_t* target_state, bytes32_t pubkey_hash) {
  c4_sync_state_t sync_state = get_validators_from_cache(ctx, period);

  if (sync_state.validators.data == NULL) {
    if (sync_state.lowest_period == 0) { // there is nothing we can start syncing from
      if (sync_state.highest_period) THROW_ERROR("the last sync state is higher than the required period, but we cannot sync backwards");
      TRY_ASYNC(init_sync_state(ctx));
      // we must have a initial state now, so let's call it again to get the state for the period we need.
      return c4_get_validators(ctx, period, target_state, NULL);
    }

    // Try edge case: sync from next period using previous_root
    TRY_ASYNC(c4_try_sync_from_next_period(ctx, period, &sync_state));

    // Successfully synced from next period, reload validators
    if (sync_state.validators.data) {
      *target_state = sync_state;
      return C4_SUCCESS;
    }

    // Normal sync path
    bytes_t client_update = {0};
    if (req_client_update(&ctx->state, sync_state.lowest_period, sync_state.current_period - sync_state.lowest_period, ctx->chain_id, &client_update)) {
      if (!c4_handle_client_updates(ctx, client_update))
        return c4_state_get_pending_request(&ctx->state) ? C4_PENDING : c4_state_add_error(&ctx->state, "Failed to handle client updates");
    }
    else
      return ctx->state.error ? C4_ERROR : C4_PENDING;

    // Check weak subjectivity period BEFORE loading new sync state
    // If this fails, clear_sync_state() is called inside to force re-initialization
    TRY_ASYNC(c4_check_weak_subjectivity(ctx, &sync_state, period));

    // Load new sync state after successful WSP check
    sync_state = get_validators_from_cache(ctx, period);
    if (sync_state.validators.data == NULL) return c4_state_add_error(&ctx->state, "Failed to get validators");
  }

  *target_state = sync_state;
  if (pubkey_hash)
    sha256(sync_state.validators, pubkey_hash);

  return C4_SUCCESS;
}
