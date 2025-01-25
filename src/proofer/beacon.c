#include "beacon.h"
#include "../util/json.h"
#include "proofer.h"
#include <inttypes.h>
#include <string.h>

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
