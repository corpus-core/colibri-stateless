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
    state.blocks = (c4_trusted_block_t*) tmp.data.data;
    state.len    = tmp.data.len / sizeof(c4_trusted_block_t);
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

INTERNAL bool c4_set_sync_period(uint64_t slot, bytes32_t blockhash, bytes_t validators, chain_id_t chain_id) {
  storage_plugin_t storage_conf  = {0};
  uint32_t         period        = (slot >> 13) + 1;
  c4_chain_state_t state         = c4_get_chain_state(chain_id);
  uint32_t         allocated_len = state.len;
  char             name[100];

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
  uint64_to_le(state.blocks[state.len].slot_bytes, slot);
  uint32_to_le(state.blocks[state.len].period_bytes, period);
  memcpy(state.blocks[state.len].blockhash, blockhash, 32);
  state.len++;

  sprintf(name, "sync_%" PRIu64 "_%d", (uint64_t) chain_id, period);
  storage_conf.set(name, validators);
  sprintf(name, "states_%" PRIu64, (uint64_t) chain_id);
  storage_conf.set(name, bytes(state.blocks, state.len * sizeof(c4_trusted_block_t)));

#ifndef C4_STATIC_MEMORY
  safe_free(state.blocks);
#endif

  return true;
}

static c4_status_t init_sync_state(verify_ctx_t* ctx) {
  c4_chain_state_t chain_state        = c4_get_chain_state(ctx->chain_id);
  c4_state_t*      state              = &ctx->state;
  json_t           data               = {0};
  bool             success            = false;
  bytes_t          client_update      = {0};
  bytes_t          client_update_past = {0};
  bytes32_t        blockhash          = {0};

  if (chain_state.len == 0) { // no trusted blockhashes, so we need to fetch the last client update
    success = req_header(state, (json_t) {0}, ctx->chain_id, &data);
    if (success) {
      uint64_t slot   = json_get_uint64(data, "slot");
      uint32_t period = (slot >> 13) - 1;
      req_client_update(state, period, 1, ctx->chain_id, &client_update);
      success = req_client_update(state, period - 20, 1, ctx->chain_id, &client_update_past);
    }
  }
  else if (trusted_blocks_len(chain_state)) {
    char     name[100];
    buffer_t tmp = stack_buffer(name);
    for (int i = 0; i < chain_state.len; i++) { // currently we only support one trusted block
      if (!uint64_from_le(chain_state.blocks[i].slot_bytes)) {
        bprintf(&tmp, "\"0x%x\"", bytes(chain_state.blocks[i].blockhash, 32));
        memcpy(blockhash, chain_state.blocks[i].blockhash, 32);
        break;
      }
    }

    // we need to resolve the client update for the given blockhash.
    success = req_header(state, (json_t) {.type = JSON_TYPE_STRING, .start = name, .len = tmp.data.len}, ctx->chain_id, &data);
    if (success) {
      uint64_t period = (json_get_uint64(data, "slot") >> 13);
      success         = req_client_update(state, period, 1, ctx->chain_id, &client_update);
    }
  }

#ifndef C4_STATIC_MEMORY
  safe_free(chain_state.blocks);
#endif
  if (success && client_update.len && !c4_handle_client_updates(ctx, client_update, blockhash))
    state->error = strdup("Failed to handle client updates");
  if (success && client_update_past.len && !c4_handle_client_updates(ctx, client_update_past, blockhash))
    state->error = strdup("Failed to handle client updates");
  return state->error ? C4_ERROR : (c4_state_get_pending_request(state) ? C4_PENDING : C4_SUCCESS);
}

static c4_sync_state_t get_validators_from_cache(verify_ctx_t* ctx, uint32_t period) {
  storage_plugin_t storage_conf   = {0};
  c4_chain_state_t chain_state    = c4_get_chain_state(ctx->chain_id);
  uint32_t         last_period    = 0;
  uint32_t         highest_period = 0;
#ifdef C4_STATIC_MEMORY
  buffer_t validators = stack_buffer(sync_buffer);
#else
  buffer_t validators = {0};
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
    last_period    = p > last_period && p <= period ? p : last_period;
    highest_period = p > highest_period ? p : highest_period;
  }
#ifndef C4_STATIC_MEMORY
  safe_free(chain_state.blocks);
#endif
  char name[100];
  sprintf(name, "sync_%" PRIu64 "_%d", (uint64_t) ctx->chain_id, period);

  if (found && storage_conf.get) storage_conf.get(name, &validators);
#ifdef BLS_DESERIALIZE
  if (validators.data.data && validators.data.len == 512 * 48) {
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
#endif

  if (validators.data.len == 0) validators.data.data = NULL; // just to make sure we mark it as not found, even if we are using static memory

  return (c4_sync_state_t) {
      .deserialized   = validators.data.data && validators.data.len > 512 * 48,
      .current_period = period,
      .last_period    = last_period,
      .highest_period = highest_period,
      .validators     = validators.data};
}

INTERNAL const c4_status_t c4_get_validators(verify_ctx_t* ctx, uint32_t period, c4_sync_state_t* target_state) {
  c4_sync_state_t sync_state = get_validators_from_cache(ctx, period);

  if (sync_state.validators.data == NULL) {
    if (sync_state.last_period == 0) { // there is nothing we can start syncing from
      if (sync_state.highest_period) THROW_ERROR("the last sync state is higher than the required period, but we cannot sync backwards");
      TRY_ASYNC(init_sync_state(ctx));
      return c4_get_validators(ctx, period, target_state);
    }
    else {
      bytes_t client_update = {0};
      if (req_client_update(&ctx->state, sync_state.last_period, sync_state.current_period - sync_state.last_period, ctx->chain_id, &client_update)) {
        if (!c4_handle_client_updates(ctx, client_update, NULL))
          return c4_state_get_pending_request(&ctx->state) ? C4_PENDING : c4_state_add_error(&ctx->state, "Failed to handle client updates");
      }
      else
        return C4_PENDING;
    }
    sync_state = get_validators_from_cache(ctx, period);
    if (sync_state.validators.data == NULL) return c4_state_add_error(&ctx->state, "Failed to get validators");
  }

  *target_state = sync_state;
  return C4_SUCCESS;
}
