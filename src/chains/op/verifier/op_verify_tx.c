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
#include "eth_tx.h"
#include "eth_verify.h"
#include "json.h"
#include "op_types.h"
#include "op_verify.h"
#include "rlp.h"
#include "ssz.h"

static bool create_eth_tx_data(verify_ctx_t* ctx, bytes32_t tx_hash_expected, bytes_t raw, bytes32_t block_hash,
                               uint64_t block_number, uint64_t base_fee_per_gas, uint32_t tx_index) {
  if (ctx->data.def->type != SSZ_TYPE_NONE) RETURN_VERIFY_ERROR(ctx, "data must be empty!");
  ssz_builder_t tx_data = ssz_builder_for_type(ETH_SSZ_DATA_TX);
  bytes32_t     tx_hash = {0};
  keccak(raw, tx_hash);
  if (tx_hash_expected && memcmp(tx_hash_expected, tx_hash, 32) != 0) RETURN_VERIFY_ERROR(ctx, "invalid tx hash!");
  bool success = c4_write_tx_data_from_raw(ctx, &tx_data, raw, tx_hash, block_hash, block_number, tx_index, base_fee_per_gas);
  if (!success) {
    buffer_free(&tx_data.dynamic);
    buffer_free(&tx_data.fixed);
    RETURN_VERIFY_ERROR(ctx, "invalid tx proof!");
  }
  ctx->data = ssz_builder_to_bytes(&tx_data);
  ctx->flags |= VERIFY_FLAG_FREE_DATA;
  return true;
}

bool op_verify_tx_proof(verify_ctx_t* ctx) {
  ssz_ob_t  block_proof       = ssz_get(&ctx->proof, "block_proof");
  uint32_t  tx_index          = ssz_get_uint32(&ctx->proof, "transactionIndex");
  ssz_ob_t* execution_payload = NULL;
  bytes32_t tx_hash_expected  = {0};

  if (strcmp(ctx->method, "eth_getTransactionByBlockNumberAndIndex") == 0) {
    json_t block_number = json_at(ctx->args, 0);
    if (json_as_uint32(json_at(ctx->args, 1)) != tx_index) RETURN_VERIFY_ERROR(ctx, "invalid tx index!");
    execution_payload = op_extract_verified_execution_payload(ctx, block_proof, &block_number);
  }
  else if (strcmp(ctx->method, "eth_getTransactionByBlockHashAndIndex") == 0) {
    bytes32_t block_hash_expected = {0};
    buffer_t  buf                 = stack_buffer(block_hash_expected);
    json_as_bytes(json_at(ctx->args, 0), &buf);
    if (json_as_uint32(json_at(ctx->args, 1)) != tx_index) RETURN_VERIFY_ERROR(ctx, "invalid tx index!");
    execution_payload = op_extract_verified_execution_payload(ctx, block_proof, NULL);
    if (execution_payload == NULL) return false; // error already set
    bytes_t block_hash_found = ssz_get(execution_payload, "blockHash").bytes;
    if (memcmp(block_hash_expected, block_hash_found.data, 32) != 0) {
      safe_free(execution_payload);
      RETURN_VERIFY_ERROR(ctx, "invalid block hash!");
    }
  }
  else if (strcmp(ctx->method, "eth_getTransactionByHash") == 0) {
    execution_payload = op_extract_verified_execution_payload(ctx, block_proof, NULL);
    buffer_t buf      = stack_buffer(tx_hash_expected);
    json_as_bytes(json_at(ctx->args, 0), &buf);
  }
  if (execution_payload == NULL) return false; // error already set

  // for now we only support the full execution payload
  ssz_ob_t raw              = ssz_at(ssz_get(execution_payload, "transactions"), tx_index);
  ssz_ob_t block_hash       = ssz_get(execution_payload, "blockHash");
  ssz_ob_t block_number     = ssz_get(execution_payload, "blockNumber");
  ssz_ob_t base_fee_per_gas = ssz_get(execution_payload, "baseFeePerGas");

  ctx->success = create_eth_tx_data(ctx, bytes_all_zero(bytes(tx_hash_expected, 32)) ? tx_hash_expected : NULL, raw.bytes, block_hash.bytes.data, ssz_uint64(block_number), ssz_uint64(base_fee_per_gas), tx_index);
  safe_free(execution_payload);
  return ctx->success;
}