#include "beacon.h"
#include "../util/json.h"
#include "../verifier/types_beacon.h"
#include "../verifier/types_verify.h"
#include "proofer.h"
#include "ssz_types.h"
#include <inttypes.h>
#include <string.h>
/*
c4_status_t c4_beacon_get_block_for_eth(proofer_ctx_t* ctx, json_t block, beacon_block_t* beacon_block) {
  if (!strncmp(block.start, "\"latest\"", 8) == 0) {
    ctx->error = strdup("Block must be latest (at least for now)");
    return C4_ERROR;
  }

  json_t      sig_block;
  json_t      block;
  c4_status_t status = c4_send_beacon_json(ctx, "/eth/v2/beacon/blocks/head", NULL, &sig_block);
  if (status != C4_SUCCESS) return status;
  sig_block     = json_get(sig_block, "data");
  sig_block     = json_get(sig_block, "message");
  uint64_t slot = json_as_uint64(json_get(sig_block, "slot"));

  char path[128];
  snprintf(path, 128, "/eth/v2/beacon/blocks/%" PRIu64, slot - 1);

  status = c4_send_beacon_json(ctx, path, NULL, &block);
  if (status != C4_SUCCESS) return status;

  block = json_get(block, "data");
  block = json_get(block, "message");

  beacon_block->slot      = slot - 1;
  beacon_block->header    = json_get(block, "message");
  beacon_block->execution = json_get(block, "execution");
  beacon_block->body      = json_get(block, "body");
  return C4_SUCCESS;
}

*/

static c4_status_t get_beacon_header_by_hash(proofer_ctx_t* ctx, char* hash, json_t* header) {

  char path[100];
  sprintf(path, "eth/v1/beacon/headers/%s", hash);

  json_t result;
  TRY_ASYNC(c4_send_beacon_json(ctx, path, NULL, &result));

  json_t val = json_get(result, "data");
  val        = json_get(val, "header");
  *header    = json_get(val, "message");

  if (!header->start) {
    ctx->error = strdup("Invalid block-format!");
    return C4_ERROR;
  }

  return C4_SUCCESS;
}

static c4_status_t get_block(proofer_ctx_t* ctx, uint64_t slot, ssz_ob_t* block) {

  char path[100];
  if (slot == 0)
    sprintf(path, "eth/v2/beacon/blocks/head");
  else {
    sprintf(path, "eth/v2/beacon/blocks/%" PRIu64, slot);
  }

  bytes_t block_data;
  TRY_ASYNC(c4_send_beacon_ssz(ctx, path, NULL, &block_data));

  //  bytes_write(block_data, fopen("signed_block.ssz", "w"), true);

  ssz_ob_t signed_block = ssz_ob(SIGNED_BEACON_BLOCK_CONTAINER, block_data);
  *block                = ssz_get(&signed_block, "message");
  if (ssz_is_error(*block)) {
    ctx->error = strdup("Invalid block-format!");
    return C4_ERROR;
  }

  return C4_SUCCESS;
}

static c4_status_t get_latest_block(proofer_ctx_t* ctx, uint64_t slot, ssz_ob_t* sig_block, ssz_ob_t* data_block) {

  TRY_ASYNC(get_block(ctx, slot, sig_block));

  uint64_t sig_slot = ssz_get_uint64(sig_block, "slot");
  TRY_ASYNC(get_block(ctx, sig_slot - 1, data_block));

  if (!sig_slot) {
    ctx->error = strdup("Invalid slot!");
    return C4_ERROR;
  }
  return C4_SUCCESS;
}

static c4_status_t eth_get_block(proofer_ctx_t* ctx, json_t block, json_t* result) {
  char tmp[200] = {0};
  memcpy(tmp + 1, block.start, block.len);
  tmp[0]             = '[';
  tmp[block.len + 1] = ']';
  return c4_send_eth_rpc(ctx, "eth_getBlockByNumber", tmp, result);
}

c4_status_t c4_beacon_get_block_for_eth(proofer_ctx_t* ctx, json_t block, beacon_block_t* beacon_block) {
  uint8_t  tmp[100] = {0};
  buffer_t buffer   = stack_buffer(tmp);
  uint64_t slot     = 0;
  ssz_ob_t sig_block;
  ssz_ob_t data_block;
  ssz_ob_t sig_body;

  if (strncmp(block.start, "\"latest\"", 8) == 0)
    TRY_ASYNC(get_latest_block(ctx, slot, &sig_block, &data_block));
  else {
    if (block.type != JSON_TYPE_STRING || block.len < 5 || block.start[1] != '0' || block.start[2] != 'x') {
      ctx->error = strdup("Invalid block-format!");
      return C4_ERROR;
    }
    json_t eth_block;
    TRY_ASYNC(eth_get_block(ctx, block, &eth_block));

    json_t hash = json_get(eth_block, "parentBeaconBlockRoot");
    json_t header;
    memset(tmp, 0, sizeof(tmp));
    memcpy(tmp, hash.start + 1, hash.len - 2);
    TRY_ASYNC(get_beacon_header_by_hash(ctx, (char*) tmp, &header));
    TRY_ASYNC(get_latest_block(ctx, json_as_uint64(json_get(header, "slot")) + 2, &sig_block, &data_block));
  }

  sig_body                     = ssz_get(&sig_block, "body");
  beacon_block->slot           = ssz_get_uint64(&data_block, "slot");
  beacon_block->header         = data_block;
  beacon_block->body           = ssz_get(&data_block, "body");
  beacon_block->execution      = ssz_get(&beacon_block->body, "executionPayload");
  beacon_block->sync_aggregate = ssz_get(&sig_body, "syncAggregate");
  return C4_SUCCESS;
}
