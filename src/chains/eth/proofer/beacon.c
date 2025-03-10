#include "beacon.h"
#include "beacon_types.h"
#include "eth_req.h"
#include "json.h"
#include "proofer.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static c4_status_t get_beacon_header_by_hash(proofer_ctx_t* ctx, char* hash, json_t* header) {

  json_t result;
  char   path[100];
  sprintf(path, "eth/v1/beacon/headers/%s", hash);

  TRY_ASYNC(c4_send_beacon_json(ctx, path, NULL, &result));

  json_t val = json_get(result, "data");
  val        = json_get(val, "header");
  *header    = json_get(val, "message");

  if (!header->start) THROW_ERROR("Invalid header!");

  return C4_SUCCESS;
}

static c4_status_t get_block(proofer_ctx_t* ctx, uint64_t slot, ssz_ob_t* block) {

  bytes_t block_data;
  char    path[100];
  if (slot == 0)
    sprintf(path, "eth/v2/beacon/blocks/head");
  else
    sprintf(path, "eth/v2/beacon/blocks/%" PRIu64, slot);

  TRY_ASYNC(c4_send_beacon_ssz(ctx, path, NULL, eth_ssz_type_for_fork(ETH_SSZ_SIGNED_BEACON_BLOCK_CONTAINER, C4_FORK_DENEB), block));
  *block = ssz_get(block, "message");
  return C4_SUCCESS;
}

static c4_status_t get_latest_block(proofer_ctx_t* ctx, uint64_t slot, ssz_ob_t* sig_block, ssz_ob_t* data_block) {

  c4_status_t status = C4_SUCCESS;

  TRY_ADD_ASYNC(status, get_block(ctx, slot, sig_block));
  if (slot) {
    TRY_ADD_ASYNC(status, get_block(ctx, slot - 1, data_block));
  }
  else if (status == C4_SUCCESS) {
    uint64_t sig_slot = ssz_get_uint64(sig_block, "slot");
    TRY_ADD_ASYNC(status, get_block(ctx, sig_slot - 1, data_block));
  }

  return status;
}

static c4_status_t eth_get_block(proofer_ctx_t* ctx, json_t block, bool full_tx, json_t* result) {
  uint8_t  tmp[200] = {0};
  buffer_t buffer   = stack_buffer(tmp);
  return c4_send_eth_rpc(ctx, "eth_getBlockByNumber", bprintf(&buffer, "[%J,%s]", block, full_tx ? "true" : "false"), result);
}

c4_status_t c4_beacon_get_block_for_eth(proofer_ctx_t* ctx, json_t block, beacon_block_t* beacon_block) {
  uint8_t  tmp[100] = {0};
  uint64_t slot     = 0;
  ssz_ob_t sig_block, data_block, sig_body;
#ifdef PROOFER_CACHE
  slot = c4_beacon_cache_get_slot(block, ctx->chain_id);
  if (slot) {
    TRY_ASYNC(get_latest_block(ctx, slot, &sig_block, &data_block));
  }
  else {
#endif

    if (strncmp(block.start, "\"latest\"", 8) == 0)
      TRY_ASYNC(get_latest_block(ctx, slot, &sig_block, &data_block));
    else {
      if (block.type != JSON_TYPE_STRING || block.len < 5 || block.start[1] != '0' || block.start[2] != 'x') THROW_ERROR("Invalid block!");
      json_t eth_block;
      TRY_ASYNC(eth_get_block(ctx, block, false, &eth_block));

      json_t hash = json_get(eth_block, "parentBeaconBlockRoot");
      if (hash.len != 68) THROW_ERROR("The Block is not a Beacon Block!");
      json_t header;
      memcpy(tmp, hash.start + 1, hash.len - 2);
      TRY_ASYNC(get_beacon_header_by_hash(ctx, (char*) tmp, &header));
      TRY_ASYNC(get_latest_block(ctx, json_as_uint64(json_get(header, "slot")) + 2, &sig_block, &data_block));
    }

#ifdef PROOFER_CACHE
    ssz_ob_t body      = ssz_get(&data_block, "body");
    ssz_ob_t execution = ssz_get(&body, "executionPayload");
    c4_beacon_cache_update(
        ctx->chain_id,
        ssz_get_uint64(&data_block, "slot"),
        ssz_get_uint64(&execution, "blockNumber"),
        ssz_get(&execution, "blockHash").bytes.data,
        strncmp(block.start, "\"latest\"", 8) == 0);
  }
#endif

  sig_body                     = ssz_get(&sig_block, "body");
  beacon_block->slot           = ssz_get_uint64(&data_block, "slot");
  beacon_block->header         = data_block;
  beacon_block->body           = ssz_get(&data_block, "body");
  beacon_block->execution      = ssz_get(&beacon_block->body, "executionPayload");
  beacon_block->sync_aggregate = ssz_get(&sig_body, "syncAggregate");
  return C4_SUCCESS;
}

ssz_builder_t c4_proof_add_header(ssz_ob_t block, bytes32_t body_root) {
  ssz_builder_t beacon_header = {.def = eth_ssz_type_for_denep(ETH_SSZ_BEACON_BLOCK_HEADER), .dynamic = {0}, .fixed = {0}};
  ssz_add_bytes(&beacon_header, "slot", ssz_get(&block, "slot").bytes);
  ssz_add_bytes(&beacon_header, "proposerIndex", ssz_get(&block, "proposerIndex").bytes);
  ssz_add_bytes(&beacon_header, "parentRoot", ssz_get(&block, "parentRoot").bytes);
  ssz_add_bytes(&beacon_header, "stateRoot", ssz_get(&block, "stateRoot").bytes);
  ssz_add_bytes(&beacon_header, "bodyRoot", bytes(body_root, 32));
  return beacon_header;
}

c4_status_t c4_send_beacon_json(proofer_ctx_t* ctx, char* path, char* query, json_t* result) {
  bytes32_t id     = {0};
  buffer_t  buffer = {0};
  buffer_add_chars(&buffer, path);
  if (query) {
    buffer_add_chars(&buffer, "?");
    buffer_add_chars(&buffer, query);
  }
  sha256(buffer.data, id);
  data_request_t* data_request = c4_state_get_data_request_by_id(&ctx->state, id);
  if (data_request) {
    buffer_free(&buffer);
    if (c4_state_is_pending(data_request)) return C4_PENDING;
    if (!data_request->error && data_request->response.data) {
      json_t response = json_parse((char*) data_request->response.data);
      if (response.type == JSON_TYPE_INVALID) THROW_ERROR("Invalid JSON response");
      *result = response;
      return C4_SUCCESS;
    }
    else
      THROW_ERROR(data_request->error ? data_request->error : "Data request failed");
  }
  else {
    data_request = calloc(1, sizeof(data_request_t));
    memcpy(data_request->id, id, 32);
    data_request->url      = (char*) buffer.data.data;
    data_request->encoding = C4_DATA_ENCODING_JSON;
    data_request->method   = C4_DATA_METHOD_GET;
    data_request->type     = C4_DATA_TYPE_BEACON_API;
    c4_state_add_request(&ctx->state, data_request);
    return C4_PENDING;
  }

  return C4_SUCCESS;
}

c4_status_t c4_send_beacon_ssz(proofer_ctx_t* ctx, char* path, char* query, const ssz_def_t* def, ssz_ob_t* result) {
  bytes32_t id     = {0};
  buffer_t  buffer = {0};
  buffer_add_chars(&buffer, path);
  if (query) {
    buffer_add_chars(&buffer, "?");
    buffer_add_chars(&buffer, query);
  }
  sha256(buffer.data, id);
  data_request_t* data_request = c4_state_get_data_request_by_id(&ctx->state, id);
  if (data_request) {
    buffer_free(&buffer);
    if (c4_state_is_pending(data_request)) return C4_PENDING;
    if (!data_request->error && data_request->response.data) {
      *result = (ssz_ob_t) {.def = def, .bytes = data_request->response};
      return ssz_is_valid(*result, true, &ctx->state) ? C4_SUCCESS : C4_ERROR;
    }
    else
      THROW_ERROR(data_request->error ? data_request->error : "Data request failed");
  }
  else {
    data_request = calloc(1, sizeof(data_request_t));
    memcpy(data_request->id, id, 32);
    data_request->url      = (char*) buffer.data.data;
    data_request->encoding = C4_DATA_ENCODING_SSZ;
    data_request->method   = C4_DATA_METHOD_GET;
    data_request->type     = C4_DATA_TYPE_BEACON_API;
    c4_state_add_request(&ctx->state, data_request);
    return C4_PENDING;
  }

  return C4_SUCCESS;
}
