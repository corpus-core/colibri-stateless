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

bool verify_receipt_proof(verify_ctx_t* ctx) {
  ssz_ob_t  tx_proof      = ssz_get(&ctx->proof, "transactionProof");
  ssz_ob_t  receipt_proof = ssz_get(&ctx->proof, "receiptProof");
  uint32_t  tx_index      = ssz_get_uint32(&ctx->proof, "transactionIndex");
  bytes_t   raw_receipt   = {0};
  bytes_t   el_header     = {0};
  bytes32_t block_hash    = {0};
  bytes_t   raw_tx        = {0};

  if (c4_verify_block(ctx, ssz_get(&ctx->proof, "block"), &el_header, block_hash) != C4_SUCCESS) return false;
  if (!c4_verify_mpt_proof(ctx,
                           tx_proof, tx_index,
                           eth_el_header_get(el_header, EL_TRANSACTIONS_ROOT).data, &raw_tx)) return false;
  if (!c4_tx_verify_tx_hash(ctx, raw_tx)) RETURN_VERIFY_ERROR(ctx, "invalid tx hash!");
  if (!c4_verify_mpt_proof(ctx, receipt_proof, tx_index, eth_el_header_get(el_header, EL_RECEIPTS_ROOT).data, &raw_receipt)) RETURN_VERIFY_ERROR(ctx, "invalid receipt proof!");
  if (!c4_tx_verify_receipt_data(ctx, ctx->data,
                                 block_hash, eth_el_header_get_uint64(el_header, EL_BLOCK_NUMBER),
                                 tx_index, raw_tx, raw_receipt)) RETURN_VERIFY_ERROR(ctx, "invalid tx data!");

  ctx->success = true;
  return true;
}