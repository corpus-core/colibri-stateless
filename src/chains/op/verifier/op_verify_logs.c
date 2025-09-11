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
#include "patricia.h"
#include "rlp.h"
#include "ssz.h"
#include "sync_committee.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool verify_tx(verify_ctx_t* ctx, ssz_ob_t block, ssz_ob_t tx, bytes32_t receipt_root) {

  if (ctx->data.def->type == SSZ_TYPE_NONE && ctx->method && strcmp(ctx->method, "eth_verifyLogs") == 0) {
    ctx->data = ssz_from_json(ctx->args, eth_ssz_verification_type(ETH_SSZ_DATA_LOGS), &ctx->state);
    ctx->flags |= VERIFY_FLAG_FREE_DATA;
  }

  bytes_t   raw_receipt  = {0};
  bytes32_t root_hash    = {0};
  uint32_t  log_len      = ssz_len(ctx->data);
  ssz_ob_t  tidx         = ssz_get(&tx, "transactionIndex");
  bytes_t   block_hash   = ssz_get(&block, "blockHash").bytes;
  ssz_ob_t  block_number = ssz_get(&block, "blockNumber");
  ssz_ob_t  tx_raw       = ssz_at(ssz_get(&block, "transactions"), ssz_uint32(tidx));

  // verify receipt proof
  if (!c4_tx_verify_receipt_proof(ctx, ssz_get(&tx, "proof"), ssz_uint32(tidx), root_hash, &raw_receipt)) RETURN_VERIFY_ERROR(ctx, "invalid receipt proof!");
  if (bytes_all_zero(bytes(receipt_root, 32)))
    memcpy(receipt_root, root_hash, 32);
  else if (memcmp(receipt_root, root_hash, 32) != 0)
    RETURN_VERIFY_ERROR(ctx, "invalid receipt proof, receipt root mismatch!");

  for (int i = 0; i < log_len; i++) {
    ssz_ob_t log = ssz_at(ctx->data, i);
    if (bytes_eq(block_number.bytes, ssz_get(&log, "blockNumber").bytes) && bytes_eq(tidx.bytes, ssz_get(&log, "transactionIndex").bytes)) {
      if (!c4_tx_verify_log_data(ctx, log, block_hash.data, ssz_uint64(block_number), ssz_uint32(tidx), tx_raw.bytes, raw_receipt)) RETURN_VERIFY_ERROR(ctx, "invalid log data!");
    }
  }
  return true;
}

static c4_status_t verif_block(verify_ctx_t* ctx, ssz_ob_t block, uint8_t* block_number_value) {
  ssz_ob_t    txs               = ssz_get(&block, "txs");
  bytes32_t   receipt_root      = {0};
  uint32_t    tx_count          = ssz_len(txs);
  ssz_ob_t*   execution_payload = op_extract_verified_execution_payload(ctx, ssz_get(&block, "block_proof"), NULL, NULL);
  c4_status_t status            = C4_SUCCESS;

  if (!execution_payload) return C4_ERROR; // error already set
  if (block_number_value) memcpy(block_number_value, ssz_get(execution_payload, "blockNumber").bytes.data, 8);

  // verify each tx and get the receipt root
  for (int i = 0; i < tx_count; i++) {
    if (!verify_tx(ctx, *execution_payload, ssz_at(txs, i), receipt_root)) {
      status = C4_ERROR;
      c4_state_add_error(&ctx->state, "Invalid Receipt");
      break;
    }
  }

  bytes_t receipts_root_expected = ssz_get(execution_payload, "receiptsRoot").bytes;
  if (status == C4_SUCCESS && memcmp(receipt_root, receipts_root_expected.data, 32) != 0) {
    status = C4_ERROR;
    c4_state_add_error(&ctx->state, "Invalid Receipts Root");
  }

  safe_free(execution_payload);
  return status;
}

static bool has_proof(verify_ctx_t* ctx, bytes_t block_numbers, bytes_t block_number, bytes_t tx_index, uint32_t block_count) {
  for (int i = 0; i < block_count; i++) {
    ssz_ob_t block = ssz_at(ctx->proof, i);
    if (bytes_eq(block_number, bytes(block_numbers.data + i * 8, 8))) {
      ssz_ob_t txs      = ssz_get(&block, "txs");
      uint32_t tx_count = ssz_len(txs);
      for (int j = 0; j < tx_count; j++) {
        ssz_ob_t tx = ssz_at(txs, j);
        if (bytes_eq(tx_index, ssz_get(&tx, "transactionIndex").bytes))
          return true;
      }
      return false;
    }
  }
  return false;
}

bool op_verify_logs_proof(verify_ctx_t* ctx) {

  uint32_t log_count     = ssz_len(ctx->data);
  uint32_t block_count   = ssz_len(ctx->proof);
  bytes_t  block_numbers = bytes(safe_malloc(block_count * 8), block_count * 8);
  bool     valid         = true;

  // verify each block we have a proof for
  for (int i = 0; i < block_count; i++) {
    if (verif_block(ctx, ssz_at(ctx->proof, i), block_numbers.data + i * 8) != C4_SUCCESS) {
      c4_state_add_error(&ctx->state, "invalid block!");
      valid = false;
      break;
    }
  }

  // make sure we have a proof for each log
  for (int i = 0; i < log_count && valid; i++) {
    ssz_ob_t log = ssz_at(ctx->data, i);
    if (!has_proof(ctx, block_numbers, ssz_get(&log, "blockNumber").bytes, ssz_get(&log, "transactionIndex").bytes, block_count)) {
      c4_state_add_error(&ctx->state, "missing log proof!");
      valid = false;
    }
  }

  ctx->success = valid;
  safe_free(block_numbers.data);
  return valid;
}