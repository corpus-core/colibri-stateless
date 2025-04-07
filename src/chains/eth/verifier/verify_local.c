
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

static const ssz_def_t eth_address_def  = SSZ_ADDRESS("address");
static const ssz_def_t eth_accounts_def = SSZ_LIST("accounts", eth_address_def, 4096);

static ssz_ob_t eth_chainId(verify_ctx_t* ctx) {
  bytes_t result = bytes(malloc(8), 8);
  uint64_to_le(result.data, ctx->chain_id);
  return (ssz_ob_t) {.bytes = result, .def = &ssz_uint64_def};
}

static ssz_ob_t eth_accounts(verify_ctx_t* ctx) {
  return (ssz_ob_t) {.def = &eth_accounts_def, .bytes = NULL_BYTES};
}

static ssz_ob_t eth_getUncleByBlockHashAndIndex(verify_ctx_t* ctx) {
  return (ssz_ob_t) {.def = &eth_accounts_def, .bytes = NULL_BYTES};
}

static ssz_ob_t eth_getUncleByBlockNumberAndIndex(verify_ctx_t* ctx) {
  return (ssz_ob_t) {.def = &eth_accounts_def, .bytes = NULL_BYTES};
}

static ssz_ob_t eth_getUncleCountByBlockNumber(verify_ctx_t* ctx) {
  return (ssz_ob_t) {.def = &eth_accounts_def, .bytes = NULL_BYTES};
}

static ssz_ob_t eth_getUncleCountByBlockHash(verify_ctx_t* ctx) {
  return (ssz_ob_t) {.def = &eth_accounts_def, .bytes = NULL_BYTES};
}

static ssz_ob_t eth_protocolVersion(verify_ctx_t* ctx) {
  bytes_t result = bytes(malloc(8), 8);
  uint64_to_le(result.data, 0x41);
  return (ssz_ob_t) {.def = &ssz_uint64_def, .bytes = result};
}

static ssz_ob_t web3_clientVersion(verify_ctx_t* ctx) {
  bytes_t result = bytes(malloc(19), 18);
  strcpy(result.data, "C4/v1.0.0-alpha.1");
  return (ssz_ob_t) {.def = &ssz_string_def, .bytes = result};
}

static ssz_ob_t web3_sha3(verify_ctx_t* ctx) {
  buffer_t buf = {0};
  json_as_bytes(json_at(ctx->args, 0), &buf);
  buffer_grow(&buf, 32);
  keccak(buf.data, buf.data.data);
  return (ssz_ob_t) {.def = &ssz_bytes32, .bytes = bytes(buf.data.data, 32)};
}

bool verify_eth_local(verify_ctx_t* ctx) {
  if (strcmp(ctx->method, "eth_chainId") == 0)
    ctx->data = eth_chainId(ctx);
  else if (strcmp(ctx->method, "eth_accounts") == 0)
    ctx->data = eth_accounts(ctx);
  else if (strcmp(ctx->method, "eth_getUncleByBlockHashAndIndex") == 0)
    ctx->data = eth_getUncleByBlockHashAndIndex(ctx);
  else if (strcmp(ctx->method, "eth_getUncleByBlockNumberAndIndex") == 0)
    ctx->data = eth_getUncleByBlockNumberAndIndex(ctx);
  else if (strcmp(ctx->method, "eth_getUncleCountByBlockNumber") == 0)
    ctx->data = eth_getUncleCountByBlockNumber(ctx);
  else if (strcmp(ctx->method, "eth_getUncleCountByBlockHash") == 0)
    ctx->data = eth_getUncleCountByBlockHash(ctx);
  else if (strcmp(ctx->method, "eth_protocolVersion") == 0)
    ctx->data = eth_protocolVersion(ctx);
  else if (strcmp(ctx->method, "web3_clientVersion") == 0)
    ctx->data = web3_clientVersion(ctx);
  else if (strcmp(ctx->method, "web3_sha3") == 0)
    ctx->data = web3_sha3(ctx);
  else
    RETURN_VERIFY_ERROR(ctx, "unknown local method");
  ctx->success = true;
  if (ctx->data.bytes.data)
    ctx->flags |= VERIFY_FLAG_FREE_DATA;
  return true;
}