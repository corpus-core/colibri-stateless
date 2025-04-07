
#include "beacon_types.h"
#include "bytes.h"
#include "crypto.h"
#include "eth_tx.h"
#include "eth_verify.h"
#include "json.h"
#include "patricia.h"
#include "rlp.h"
#include "ssz.h"
#include "sync_committee.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ssz_ob_t eth_chainId(verify_ctx_t* ctx) {
  bytes_t result = bytes(malloc(8), 8);
  uint64_to_le(result.data, ctx->chain_id);
  return (ssz_ob_t) {.bytes = result, .def = &ssz_uint64_def};
}

bool verify_eth_local(verify_ctx_t* ctx) {
  if (strcmp(ctx->method, "eth_chainId") == 0)
    ctx->data = eth_chainId(ctx);
  else
    RETURN_VERIFY_ERROR(ctx, "unknown local method");
  ctx->success = true;
  ctx->flags |= VERIFY_FLAG_FREE_DATA;
  return true;
}