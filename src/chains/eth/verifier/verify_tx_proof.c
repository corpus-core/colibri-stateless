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

#define GINDEX_BLOCKUMBER    806
#define GINDEX_BLOCHASH      812
#define GINDEX_BASEFEEPERGAS 811
#define GINDEX_TXINDEX_G     1704984576L // gindex of the first tx

static bool verify_merkle_proof(verify_ctx_t* ctx, ssz_ob_t proof, bytes_t block_hash, bytes_t block_number, bytes_t base_fee_per_gas, bytes_t raw, uint32_t tx_index, bytes32_t body_root) {
  uint8_t   leafes[128] = {0};                                                                                     // 4 leafes, 32 bytes each
  bytes32_t root_hash   = {0};                                                                                     // calculated body root hash
  gindex_t  gindexes[]  = {GINDEX_BLOCKUMBER, GINDEX_BLOCHASH, GINDEX_BASEFEEPERGAS, GINDEX_TXINDEX_G + tx_index}; // calculate the gindexes for the proof

  // copy leaf data
  memcpy(leafes, block_number.data, block_number.len);
  memcpy(leafes + 32, block_hash.data, block_hash.len);
  memcpy(leafes + 64, base_fee_per_gas.data, base_fee_per_gas.len);
  ssz_hash_tree_root(ssz_ob(ssz_transactions_bytes, raw), leafes + 96);

  if (!ssz_verify_multi_merkle_proof(proof.bytes, bytes(leafes, sizeof(leafes)), gindexes, root_hash)) RETURN_VERIFY_ERROR(ctx, "invalid tx proof, missing nodes!");
  if (memcmp(root_hash, body_root, 32) != 0) RETURN_VERIFY_ERROR(ctx, "invalid tx proof, body root mismatch!");
  return true;
}

static bool create_eth_tx_data(verify_ctx_t* ctx, bytes_t raw, bytes32_t block_hash, uint64_t block_number, uint64_t base_fee_per_gas, uint32_t tx_index) {
  if (ctx->data.def->type != SSZ_TYPE_NONE) RETURN_VERIFY_ERROR(ctx, "data must be empty!");
  ssz_builder_t tx_data = ssz_builder_for_type(ETH_SSZ_DATA_TX);
  bytes32_t     tx_hash = {0};
  keccak(raw, tx_hash);
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
static bool verify_args(verify_ctx_t* ctx, bytes_t raw, uint32_t tx_index, bytes32_t block_hash) {
  if (ctx->method == NULL) return true;
  if (strcmp(ctx->method, "eth_getTransactionByHash") == 0) {
    if (!c4_tx_verify_tx_hash(ctx, raw)) RETURN_VERIFY_ERROR(ctx, "invalid tx hash!");
  }
  else if (strcmp(ctx->method, "eth_getTransactionByBlockHashAndIndex") == 0) {
    bytes32_t tmp            = {0};
    buffer_t  buf            = stack_buffer(tmp);
    bytes_t   req_block_hash = json_as_bytes(json_at(ctx->args, 0), &buf);
    if (req_block_hash.len != 32) RETURN_VERIFY_ERROR(ctx, "invalid block hash!");
    if (memcmp(req_block_hash.data, block_hash, 32) != 0) RETURN_VERIFY_ERROR(ctx, "invalid block hash!");
    if (json_as_uint32(json_at(ctx->args, 1)) != tx_index) RETURN_VERIFY_ERROR(ctx, "invalid tx index!");
  }
  else if (strcmp(ctx->method, "eth_getTransactionByBlockNumberAndIndex") == 0) {
    uint64_t req_block_num = json_as_uint64(json_at(ctx->args, 0));
    if (!req_block_num) RETURN_VERIFY_ERROR(ctx, "invalid block number!");
    if (req_block_num != ssz_get_uint64(&ctx->proof, "blockNumber")) RETURN_VERIFY_ERROR(ctx, "invalid block number!");
    if (json_as_uint32(json_at(ctx->args, 1)) != tx_index) RETURN_VERIFY_ERROR(ctx, "invalid tx index!");
  }
  else
    RETURN_VERIFY_ERROR(ctx, "invalid method for tx proof!");
  return true;
}

bool verify_tx_proof(verify_ctx_t* ctx) {
  ssz_ob_t raw              = ssz_get(&ctx->proof, "transaction");
  ssz_ob_t tx_proof         = ssz_get(&ctx->proof, "proof");
  ssz_ob_t tx_index         = ssz_get(&ctx->proof, "transactionIndex");
  ssz_ob_t header           = ssz_get(&ctx->proof, "header");
  ssz_ob_t block_hash       = ssz_get(&ctx->proof, "blockHash");
  ssz_ob_t block_number     = ssz_get(&ctx->proof, "blockNumber");
  ssz_ob_t body_root        = ssz_get(&header, "bodyRoot");
  ssz_ob_t base_fee_per_gas = ssz_get(&ctx->proof, "baseFeePerGas");

  if (!verify_args(ctx, raw.bytes, ssz_uint32(tx_index), block_hash.bytes.data)) return false;
  if (!verify_merkle_proof(ctx, tx_proof, block_hash.bytes, block_number.bytes, base_fee_per_gas.bytes, raw.bytes, ssz_uint32(tx_index), body_root.bytes.data)) RETURN_VERIFY_ERROR(ctx, "invalid tx proof!");
  if (c4_verify_header(ctx, header, ctx->proof) != C4_SUCCESS) return false;
  if (!create_eth_tx_data(ctx, raw.bytes, block_hash.bytes.data, ssz_uint64(block_number), ssz_uint64(base_fee_per_gas), ssz_uint32(tx_index))) return false;

  ctx->success = true;
  return true;
}