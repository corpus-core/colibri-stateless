#include "proof_account.h"
#include "../util/json.h"
#include <stdlib.h>
#include <string.h>

void c4_proof_account(proofer_ctx_t* ctx) {
  json_t address = json_at(ctx->params, 0);
  json_t block   = json_at(ctx->params, 1);

  if (address.type != JSON_TYPE_STRING || address.len != 44 || address.start[1] != '0' || address.start[2] != 'x') {
    ctx->error = strdup("Invalid address");
    return;
  }

  char tmp[100];
  snprintf(tmp, sizeof(tmp), "[%.44s,[],\"latest\"]", address.start);

  json_t result = {0};
  if (c4u_send_eth_rpc(ctx, "eth_getProof", tmp, &result)) return;

  ctx->proof = bytes_dup(bytes((uint8_t*) result.start, result.len));
}