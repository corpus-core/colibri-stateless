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
#include "chains.h"
#include "crypto.h"
#include "eth_account.h"
#include "eth_tx.h"
#include "eth_verify.h"
#include "json.h"
#include "logger.h"
#include "op_payload.h"
#include "op_types.h"
#include "op_verify.h"
#include "patricia.h"
#include "rlp.h"
#include "ssz.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

bool op_verify_block(verify_ctx_t* ctx) {
  bool      is_blocknumber    = strcmp(ctx->method, "eth_blockNumber") == 0;
  json_t    block_number      = is_blocknumber ? (json_t) {.type = JSON_TYPE_STRING, .start = "\"latest\"", .len = 8} : json_at(ctx->args, 0);
  bool      include_txs       = is_blocknumber ? false : json_as_bool(json_at(ctx->args, 1));
  ssz_ob_t  block_proof       = ssz_get(&ctx->proof, "block_proof");
  bytes32_t parent_root       = {0};
  bytes32_t withdrawel_root   = {0};
  ssz_ob_t* execution_payload = op_extract_verified_execution_payload(ctx, block_proof, &block_number, &parent_root);
  if (!execution_payload) return false;

  if (is_blocknumber) {
    ctx->data       = ssz_get(execution_payload, "blockNumber");
    ctx->data.bytes = bytes_dup(ctx->data.bytes); // need to copy bytes, because payload will be deleted
    ctx->success    = true;
    ctx->flags |= VERIFY_FLAG_FREE_DATA;
  }
  else {
    ssz_hash_tree_root(ssz_get(execution_payload, "withdrawals"), withdrawel_root);
    eth_set_block_data(ctx, ETH_BLOCK_DATA_MASK_ALL, *execution_payload, parent_root, withdrawel_root, include_txs);
  }
  safe_free(execution_payload);
  ctx->success = true;
  return true;
}
