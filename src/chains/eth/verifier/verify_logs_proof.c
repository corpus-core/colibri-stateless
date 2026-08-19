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
#include "el_header.h"
#include "eth_bloom.h"
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

#define GINDEX_TX_IN_LOGS_LIST_BASE 2097152L

static bool verify_tx(verify_ctx_t* ctx, ssz_ob_t block, ssz_ob_t tx, bytes32_t receipt_root, bytes32_t tx_root, uint64_t block_number, bytes32_t block_hash) {
  bytes_t  raw_receipt = {0};
  bytes_t  raw_tx      = {0};
  uint32_t log_len     = ssz_len(ctx->data);
  uint32_t tidx        = ssz_get_uint32(&tx, "transactionIndex");

  // in case we just proof the tx, we copy the arg as data, so data ia always set.
  if (ctx->data.def->type == SSZ_TYPE_NONE && ctx->method && strcmp(ctx->method, "eth_verifyLogs") == 0) {
    ctx->data = ssz_from_json(ctx->args, eth_ssz_verification_type(ETH_SSZ_DATA_LOGS), &ctx->state);
    ctx->flags |= VERIFY_FLAG_FREE_DATA;
  }

  // verify receipt proof
  if (!c4_verify_mpt_proof(ctx, ssz_get(&tx, "receiptProof"), tidx, receipt_root, &raw_receipt)) RETURN_VERIFY_ERROR(ctx, "invalid receipt proof!");
  if (!c4_verify_mpt_proof(ctx, ssz_get(&tx, "transactionProof"), tidx, tx_root, &raw_tx)) RETURN_VERIFY_ERROR(ctx, "invalid transaction proof!");

  for (int i = 0; i < log_len; i++) {
    ssz_ob_t log = ssz_at(ctx->data, i);
    if (block_number == ssz_get_uint64(&log, "blockNumber") && tidx == ssz_get_uint32(&log, "transactionIndex")) {
      if (!c4_tx_verify_log_data(ctx, log, block_hash, block_number, tidx, raw_tx, raw_receipt)) RETURN_VERIFY_ERROR(ctx, "invalid log data!");
    }
  }
  return true;
}

static c4_status_t verify_block(verify_ctx_t* ctx, ssz_ob_t block) {

  ssz_ob_t  block_proof  = ssz_get(&block, "block");
  ssz_ob_t  txs          = ssz_get(&block, "txs");
  uint64_t  block_number = 0;
  uint8_t*  receipt_root = NULL;
  uint8_t*  tx_root      = NULL;
  uint32_t  tx_count     = ssz_len(txs);
  bytes_t   el_header    = {0};
  bytes32_t block_hash   = {0};

  TRY_ASYNC(c4_verify_block(ctx, block_proof, &el_header, block_hash));
  receipt_root = eth_el_header_get(el_header, EL_RECEIPTS_ROOT).data;
  tx_root      = eth_el_header_get(el_header, EL_TRANSACTIONS_ROOT).data;
  block_number = eth_el_header_get_uint64(el_header, EL_BLOCK_NUMBER);
  if (block_number != ssz_get_uint64(&block, "blockNumber")) THROW_ERROR("invalid block number!");

  // verify each tx and get the receipt root
  for (int i = 0; i < tx_count; i++) {
    if (!verify_tx(ctx, block, ssz_at(txs, i), receipt_root, tx_root, block_number, block_hash)) THROW_ERROR("invalid receipt proof!");
  }
  return C4_SUCCESS;
}

static bool has_proof(verify_ctx_t* ctx, bytes_t block_number, bytes_t tx_index, uint32_t block_count) {
  for (int i = 0; i < block_count; i++) {
    ssz_ob_t block    = ssz_at(ctx->proof, i);
    bytes_t  block_bn = ssz_get(&block, "blockNumber").bytes;
    if (bytes_eq(block_number, block_bn)) {
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

bool verify_logs_proof(verify_ctx_t* ctx) {
  uint32_t log_count   = ssz_len(ctx->data);
  uint32_t block_count = ssz_len(ctx->proof);

  for (int i = 0; i < block_count; i++) {
    if (verify_block(ctx, ssz_at(ctx->proof, i)) != C4_SUCCESS) return false;
  }

  for (int i = 0; i < log_count; i++) {
    ssz_ob_t log = ssz_at(ctx->data, i);
    if (!has_proof(ctx, ssz_get(&log, "blockNumber").bytes, ssz_get(&log, "transactionIndex").bytes, block_count)) RETURN_VERIFY_ERROR(ctx, "missing log proof!");
  }

#ifdef PAP
  if (ctx->flags & VERIFY_FLAG_PAP) {
    json_t filter = json_at(ctx->args, 0);
    if (filter.type != JSON_TYPE_OBJECT) RETURN_VERIFY_ERROR(ctx, "PAP mode requires filter object in args");
    ssz_ob_t filtered = c4_eth_filter_logs(ctx->data, filter);
    if (ctx->flags & VERIFY_FLAG_FREE_DATA)
      safe_free(ctx->data.bytes.data);
    ctx->data = filtered;
    ctx->flags |= VERIFY_FLAG_FREE_DATA;
  }
#endif

  ctx->success = true;
  return true;
}
