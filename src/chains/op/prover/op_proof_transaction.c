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
#include "eth_req.h"
#include "eth_tools.h"
#include "json.h"
#include "op_prover.h"
#include "op_tools.h"
#include "op_types.h"
#include "prover.h"

c4_status_t c4_op_proof_transaction(prover_ctx_t* ctx) {
  bytes32_t     body_root    = {0};
  json_t        txhash       = json_at(ctx->params, 0);
  json_t        tx_data      = {0};
  uint32_t      tx_index     = 0;
  json_t        block_number = {0};
  ssz_builder_t block_proof  = {0};

  // finde the blocknumber and txindex
  if (strcmp(ctx->method, "eth_getTransactionByBlockHashAndIndex") == 0 || strcmp(ctx->method, "eth_getTransactionByBlockNumberAndIndex") == 0) {
    tx_index     = json_as_uint32(json_at(ctx->params, 1));
    block_number = json_at(ctx->params, 0);
  }
  else { // eth_getTransactionByHash
    if (txhash.type != JSON_TYPE_STRING || txhash.len != 68 || txhash.start[1] != '0' || txhash.start[2] != 'x') THROW_ERROR("Invalid hash");
    TRY_ASYNC(get_eth_tx(ctx, txhash, &tx_data));
    tx_index     = json_get_uint32(tx_data, "transactionIndex");
    block_number = json_get(tx_data, "blockNumber");
    if (block_number.type != JSON_TYPE_STRING || block_number.len < 5 || block_number.start[1] != '0' || block_number.start[2] != 'x') THROW_ERROR("Invalid block number");
  }

  // get the execution payload
  TRY_ASYNC(c4_op_create_block_proof(ctx, block_number, &block_proof));

  // build the proof
  ssz_builder_t eth_tx_proof = ssz_builder_for_op_type(OP_SSZ_VERIFY_TRANSACTION_PROOF);
  ssz_add_bytes(&eth_tx_proof, "tx_proof", NULL_BYTES); // no proof since we have the full execution payload
  ssz_add_uint32(&eth_tx_proof, tx_index);              // txindex
  ssz_add_builders(&eth_tx_proof, "block_proof", block_proof);

  ctx->proof = op_create_proof_request(
      ctx->chain_id,
      NULL_SSZ_BUILDER,
      eth_tx_proof,
      NULL_SSZ_BUILDER);

  return C4_SUCCESS;
}