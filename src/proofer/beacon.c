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

c4_status_t get_block(proofer_ctx_t* ctx, uint64_t slot, ssz_ob_t* block) {

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

c4_status_t c4_beacon_get_block_for_eth(proofer_ctx_t* ctx, json_t block, beacon_block_t* beacon_block) {
  uint64_t slot = 0;
  ssz_ob_t sig_block;
  ssz_ob_t data_block;
  ssz_ob_t sig_body;

  if (strncmp(block.start, "\"latest\"", 8) != 0) {
    ctx->error = strdup("Block must be latest (at least for now)");
    return C4_ERROR;
  }

  TRY_ASYNC(get_latest_block(ctx, slot, &sig_block, &data_block));

  sig_body                     = ssz_get(&sig_block, "body");
  beacon_block->slot           = ssz_get_uint64(&data_block, "slot");
  beacon_block->header         = data_block;
  beacon_block->body           = ssz_get(&data_block, "body");
  beacon_block->execution      = ssz_get(&beacon_block->body, "executionPayload");
  beacon_block->sync_aggregate = ssz_get(&sig_body, "syncAggregate");
  return C4_SUCCESS;
}
/*
ssz_ob_t get_execution_payload(proofer_ctx_t* ctx, ssz_ob_t block) {
  ssz_ob_t body = ssz_get(&block, "body");
  return ssz_get(&body, "executionPayload");
}
*/
