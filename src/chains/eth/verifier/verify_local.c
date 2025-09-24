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
  const char* version = "C4/v1.0.0-alpha.1";
  bytes_t     result  = bytes(malloc(strlen(version)), strlen(version));
  memcpy(result.data, version, result.len);
  return (ssz_ob_t) {.def = &ssz_string_def, .bytes = result};
}

static ssz_ob_t web3_sha3(verify_ctx_t* ctx) {
  buffer_t buf = {0};
  json_as_bytes(json_at(ctx->args, 0), &buf);
  buffer_grow(&buf, 32);
  keccak(buf.data, buf.data.data);
  return (ssz_ob_t) {.def = &ssz_bytes32, .bytes = bytes(buf.data.data, 32)};
}

static ssz_ob_t colibri_decodeTransaction(verify_ctx_t* ctx) {
  // Get the raw transaction hex string from parameters
  json_t raw_tx_param = json_at(ctx->args, 0);
  if (raw_tx_param.type != JSON_TYPE_STRING) {
    ctx->state.error = strdup("colibri_decodeTransaction: parameter must be a hex string");
    return (ssz_ob_t) {0};
  }

  buffer_t      raw_tx_buf = {0};
  bytes_t       raw        = json_as_bytes(raw_tx_param, &raw_tx_buf);
  ssz_builder_t tx_data    = ssz_builder_for_type(ETH_SSZ_DATA_TX);
  bytes32_t     tx_hash    = {0};
  bytes32_t     block_hash = {0};
  keccak(raw, tx_hash);
  bool success = c4_write_tx_data_from_raw(ctx, &tx_data, raw, tx_hash, block_hash, 0, 0, 0);
  buffer_free(&raw_tx_buf);
  if (!success) {
    buffer_free(&tx_data.dynamic);
    buffer_free(&tx_data.fixed);
    if (ctx->state.error == NULL) c4_state_add_error(ctx, "invalid tx data!");
    return (ssz_ob_t) {0};
  }
  return ssz_builder_to_bytes(&tx_data);
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
  else if (strcmp(ctx->method, "colibri_decodeTransaction") == 0)
    ctx->data = colibri_decodeTransaction(ctx);
  else
    RETURN_VERIFY_ERROR(ctx, "unknown local method");
  if (ctx->data.bytes.data) ctx->flags |= VERIFY_FLAG_FREE_DATA;
  ctx->success = ctx->state.error == NULL;
  return ctx->success;
}