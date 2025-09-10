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
#include "op_types.h"
#include "op_verify.h"
#include "rlp.h"
#include "ssz.h"
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

bool op_verify_receipt_proof(verify_ctx_t* ctx) {
  uint32_t  tx_index          = ssz_get_uint32(&ctx->proof, "transactionIndex");
  ssz_ob_t  receipt_proof     = ssz_get(&ctx->proof, "receipt_proof");
  ssz_ob_t  block_proof       = ssz_get(&ctx->proof, "block_proof");
  ssz_ob_t* execution_payload = op_extract_verified_execution_payload(ctx, block_proof, NULL, NULL);
  if (!execution_payload) return false;

  ssz_ob_t  raw_tx                  = ssz_at(ssz_get(execution_payload, "transactions"), tx_index);
  ssz_ob_t  block_hash              = ssz_get(execution_payload, "blockHash");
  ssz_ob_t  block_number            = ssz_get(execution_payload, "blockNumber");
  ssz_ob_t  receipts_root_expected  = ssz_get(execution_payload, "receiptsRoot");
  uint64_t  block_number_val        = ssz_uint64(block_number);
  bytes32_t receipt_root_calculated = {0};
  bytes_t   raw_receipt             = {0};

  if (!c4_tx_verify_tx_hash(ctx, raw_tx.bytes))
    c4_state_add_error(&ctx->state, "invalid tx hash!");
  else if (!c4_tx_verify_receipt_proof(ctx, receipt_proof, tx_index, receipt_root_calculated, &raw_receipt))
    c4_state_add_error(&ctx->state, "invalid receipt proof!");
  else if (memcmp(receipt_root_calculated, receipts_root_expected.bytes.data, 32) != 0)
    c4_state_add_error(&ctx->state, "invalid receipt root!");
  else if (!c4_tx_verify_receipt_data(ctx, ctx->data, block_hash.bytes.data, ssz_uint64(block_number), tx_index, raw_tx.bytes, raw_receipt))
    c4_state_add_error(&ctx->state, "invalid tx data!");
  else
    ctx->success = true;
  safe_free(execution_payload);
  return ctx->success;
}