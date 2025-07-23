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

static bool verify_merkle_proof(verify_ctx_t* ctx, ssz_ob_t proof, bytes_t block_hash, bytes_t block_number, bytes_t raw, uint32_t tx_index, bytes32_t receipt_root, bytes32_t body_root) {
  uint8_t   leafes[4 * 32] = {0};                                                                                    // 3 leafes, 32 bytes each
  bytes32_t root_hash      = {0};                                                                                    // calculated body root hash
  gindex_t  gindexes[]     = {GINDEX_BLOCKUMBER, GINDEX_BLOCHASH, GINDEX_RECEIPT_ROOT, GINDEX_TXINDEX_G + tx_index}; // calculate the gindexes for the proof

  // copy leaf data
  memcpy(leafes, block_number.data, block_number.len);
  memcpy(leafes + 32, block_hash.data, block_hash.len);
  memcpy(leafes + 64, receipt_root, 32);
  ssz_hash_tree_root(ssz_ob(ssz_transactions_bytes, raw), leafes + 96);

  if (!ssz_verify_multi_merkle_proof(proof.bytes, bytes(leafes, sizeof(leafes)), gindexes, root_hash)) RETURN_VERIFY_ERROR(ctx, "invalid tx proof, missing nodes!");
  if (memcmp(root_hash, body_root, 32) != 0) RETURN_VERIFY_ERROR(ctx, "invalid tx proof, body root mismatch!");
  return true;
}

bool verify_receipt_proof(verify_ctx_t* ctx) {
  ssz_ob_t  raw_tx        = ssz_get(&ctx->proof, "transaction");
  ssz_ob_t  receipt_proof = ssz_get(&ctx->proof, "receipt_proof");
  ssz_ob_t  block_proof   = ssz_get(&ctx->proof, "block_proof");
  ssz_ob_t  header        = ssz_get(&ctx->proof, "header");
  ssz_ob_t  block_hash    = ssz_get(&ctx->proof, "blockHash");
  ssz_ob_t  block_number  = ssz_get(&ctx->proof, "blockNumber");
  ssz_ob_t  body_root     = ssz_get(&header, "bodyRoot");
  uint32_t  tx_index      = ssz_get_uint32(&ctx->proof, "transactionIndex");
  bytes32_t receipt_root  = {0};
  bytes_t   raw_receipt   = {0};

  if (!c4_tx_verify_tx_hash(ctx, raw_tx.bytes)) RETURN_VERIFY_ERROR(ctx, "invalid tx hash!");
  if (!c4_tx_verify_receipt_proof(ctx, receipt_proof, tx_index, receipt_root, &raw_receipt)) RETURN_VERIFY_ERROR(ctx, "invalid receipt proof!");
  if (!c4_tx_verify_receipt_data(ctx, ctx->data, block_hash.bytes.data, ssz_uint64(block_number), tx_index, raw_tx.bytes, raw_receipt)) RETURN_VERIFY_ERROR(ctx, "invalid tx data!");
  if (!verify_merkle_proof(ctx, block_proof, block_hash.bytes, block_number.bytes, raw_tx.bytes, tx_index, receipt_root, body_root.bytes.data)) RETURN_VERIFY_ERROR(ctx, "invalid tx proof!");
  if (c4_verify_header(ctx, header, ctx->proof) != C4_SUCCESS) return false;

  ctx->success = true;
  return true;
}