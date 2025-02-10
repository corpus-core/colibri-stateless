#include "../util/crypto.h"
#include "../util/plugin.h"
#include "./sync_committee.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static data_request_t* find_req(data_request_t* req, char* url) {
  while (req) {
    if (strcmp(req->url, url) == 0) return req;
    req = req->next;
  }
  return NULL;
}
static void add_req(data_request_t** req, data_request_t* new_req) {
  data_request_t* current = *req;
  *req                    = new_req;
  new_req->next           = current;
}

c4_chain_state_t c4_get_chain_state(chain_id_t chain_id) {
  c4_chain_state_t state = {0};
  char             name[100];
  buffer_t         tmp          = {0};
  storage_plugin_t storage_conf = {0};

  c4_get_storage_config(&storage_conf);

  sprintf(name, "states_%" PRIu64, (uint64_t) chain_id);
  tmp.allocated = sizeof(c4_trusted_block_t) * storage_conf.max_sync_states;

  if (storage_conf.get(name, &tmp) && tmp.data.data) {
    state.blocks = (c4_trusted_block_t*) tmp.data.data;
    state.len    = tmp.data.len / sizeof(c4_trusted_block_t);
  }

  return state;
}

static bool req_header(json_t slot, chain_id_t chain_id, data_request_t** requests, json_t* data, char** error) {
  buffer_t tmp = {0};
  char     name[100];
  if (slot.type == JSON_TYPE_STRING)
    bprintf(&tmp, "eth/v1/beacon/headers/%j", slot);
  else
    bprintf(&tmp, "eth/v1/beacon/headers/head");

  data_request_t* req = find_req(*requests, (char*) tmp.data.data);
  if (req) buffer_free(&tmp);
  if (req && req->response.data) {
    json_t res = json_parse((char*) req->response.data);
    if (res.type == JSON_TYPE_OBJECT) res = json_get(res, "data");
    if (res.type == JSON_TYPE_OBJECT) res = json_get(res, "header");
    if (res.type == JSON_TYPE_OBJECT) {
      *data = res;
      return true;
    }
    else {
      *error = "Invalid response for header";
      return false;
    }
  }
  else if (req && req->error) {
    *error = req->error;
    return false;
  }
  data_request_t* new_req = calloc(1, sizeof(data_request_t));
  new_req->chain_id       = chain_id;
  new_req->url            = (char*) tmp.data.data;
  new_req->encoding       = C4_DATA_ENCODING_JSON;
  new_req->type           = C4_DATA_TYPE_BEACON_API;

  add_req(requests, new_req);
  return false;
}
static bool req_client_update(uint32_t period, chain_id_t chain_id, data_request_t** requests, bytes_t* data, char** error) {
  buffer_t tmp = {0};
  char     name[100];
  bprintf(&tmp, "eth/v1/beacon/light_client/updates?start_period=%d6&count=1", period);

  data_request_t* req = find_req(*requests, (char*) tmp.data.data);
  if (req) buffer_free(&tmp);
  if (req && req->response.data) {
    *data = req->response;
    return true;
  }
  else if (req && req->error) {
    *error = req->error;
    return false;
  }
  data_request_t* new_req = calloc(1, sizeof(data_request_t));
  new_req->chain_id       = chain_id;
  new_req->url            = (char*) tmp.data.data;
  new_req->encoding       = C4_DATA_ENCODING_SSZ;
  new_req->type           = C4_DATA_TYPE_BEACON_API;

  add_req(requests, new_req);
  return false;
}

bool c4_set_sync_period(uint64_t slot, bytes32_t blockhash, bytes_t validators, chain_id_t chain_id) {
  storage_plugin_t storage_conf  = {0};
  uint32_t         period        = (slot >> 13) + 1;
  c4_chain_state_t state         = c4_get_chain_state(chain_id);
  uint32_t         allocated_len = state.len;
  char             name[100];

  c4_get_storage_config(&storage_conf);

  while (state.len >= storage_conf.max_sync_states) {
    uint32_t oldest       = 0;
    int      oldest_index = 0;
    for (int i = 0; i < state.len; i++) {
      if (state.blocks[i].period < oldest || oldest == 0) {
        oldest       = state.blocks[i].period;
        oldest_index = i;
      }
    }

    sprintf(name, "sync_%" PRIu64 "_%d", (uint64_t) chain_id, oldest);
    storage_conf.del(name);
    if (oldest_index < state.len - 1) memmove(state.blocks + oldest_index, state.blocks + oldest_index + 1, (state.len - oldest_index - 1) * sizeof(c4_trusted_block_t));
    state.len--;
  }

  if (allocated_len == 0)
    state.blocks = calloc(sizeof(c4_trusted_block_t), 1);
  else if (allocated_len < state.len + 1)
    state.blocks = realloc(state.blocks, sizeof(c4_trusted_block_t) * (state.len + 1));
  state.blocks[state.len].slot   = slot;
  state.blocks[state.len].period = period;
  memcpy(state.blocks[state.len].blockhash, blockhash, 32);
  state.len++;

  sprintf(name, "sync_%" PRIu64 "_%d", (uint64_t) chain_id, period);
  storage_conf.set(name, validators);
  sprintf(name, "states_%" PRIu64, (uint64_t) chain_id);
  storage_conf.set(name, bytes(state.blocks, state.len * sizeof(c4_trusted_block_t)));
  free(state.blocks);

  return true;
}

data_request_t* c4_set_trusted_blocks(json_t blocks, chain_id_t chain_id, data_request_t* requests, char** error) {
  c4_chain_state_t state         = c4_get_chain_state(chain_id);
  data_request_t*  req           = requests;
  json_t           data          = {0};
  bool             success       = false;
  bytes_t          client_update = {0};
  bytes32_t        blockhash     = {0};
  if (state.len == 0 && (blocks.len == 0 || json_len(blocks) == 0)) {
    // we need to fetch the last client update
    success = req_header((json_t) {0}, chain_id, &req, &data, error);
    if (success) {
      uint64_t period = (json_get_uint64(data, "slot") >> 13) - 1;
      success         = req_client_update(period, chain_id, &req, &client_update, error);
    }
  }
  else if (state.len == 0) {
    // we need to resolve the client update for the given blockhash.
    success = req_header(json_at(blocks, 0), chain_id, &req, &data, error);
    if (success) {
      uint64_t period = (json_get_uint64(data, "slot") >> 13) - 1;
      success         = req_client_update(period, chain_id, &req, &client_update, error);
    }
  }
  else {
    // we need to check if the blocks are in the cache
  }
  free(state.blocks);

  if (!success) {
    while (req) {
      data_request_t* next = req->next;
      if (req->url) free(req->url);
      if (req->error) free(req->error);
      if (req->payload.data) free(req->payload.data);
      if (req->response.data) free(req->response.data);
      free(req);
      req = next;
    }
  };

  if (success && client_update.len && !c4_handle_client_updates(client_update, chain_id, blockhash))
    *error = "Failed to handle client updates";

  return req;
}