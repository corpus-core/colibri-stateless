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
#include "eth_account.h"
#include "eth_tx.h"
#include "eth_verify.h"
#include "json.h"
#include "op_types.h"
#include "op_verify.h"
#include "op_zstd.h"
#include "patricia.h"
#include "rlp.h"
#include "ssz.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

bool op_verify_block_proof(verify_ctx_t* ctx) {

  json_t   block_number    = json_at(ctx->args, 0);
  bool     include_txs     = json_as_bool(json_at(ctx->args, 1));
  ssz_ob_t block_proof     = ssz_get(&ctx->proof, "block_proof");
  ssz_ob_t compressed_data = ssz_get(&block_proof, "payload");
  ssz_ob_t signature       = ssz_get(&block_proof, "signature");

  bytes_t decompressed_data = bytes(NULL, op_zstd_get_decompressed_size(compressed_data.bytes));
  decompressed_data.data    = safe_malloc(decompressed_data.len);
  op_zstd_decompress(compressed_data.bytes, decompressed_data);

  ctx->success = true;
  return true;
}
