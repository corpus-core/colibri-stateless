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
#include "beacon.h"
#include "beacon_types.h"
#include "eth_req.h"
#include "eth_tools.h"
#include "historic_proof.h"
#include "json.h"
#include "logger.h"
#include "op_prover.h"
#include "op_tools.h"
#include "op_types.h"
#include "patricia.h"
#include "prover.h"
#include "rlp.h"
#include "ssz.h"

static c4_status_t create_op_receipt_proof(prover_ctx_t* ctx, ssz_builder_t block_proof, ssz_ob_t receipt_proof, json_t receipt) {

  ssz_builder_t eth_tx_proof = ssz_builder_for_op_type(OP_SSZ_VERIFY_RECEIPT_PROOF);
  uint32_t      tx_index     = json_get_uint32(receipt, "transactionIndex");

  // build the proof
  ssz_add_bytes(&eth_tx_proof, "transaction", NULL_BYTES);
  ssz_add_uint32(&eth_tx_proof, tx_index);
  ssz_add_bytes(&eth_tx_proof, "receipt_proof", receipt_proof.bytes);
  ssz_add_bytes(&eth_tx_proof, "tx_proof", NULL_BYTES);
  ssz_add_builders(&eth_tx_proof, "block_proof", block_proof);

  ctx->proof = op_create_proof_request(
      ctx->chain_id,
      FROM_JSON(receipt, ETH_SSZ_DATA_RECEIPT),
      eth_tx_proof,
      NULL_SSZ_BUILDER);

  return C4_SUCCESS;
}

c4_status_t c4_op_proof_receipt(prover_ctx_t* ctx) {
  json_t      txhash         = json_at(ctx->params, 0);
  bytes32_t   block_hash     = {0};
  json_t      tx_data        = {0};
  json_t      block_receipts = {0};
  json_t      receipt        = {0};
  ssz_ob_t    receipt_proof  = {0};
  buffer_t    buf            = stack_buffer(block_hash);
  c4_status_t status         = C4_SUCCESS;
  node_t*     receipt_tree   = NULL; // the (hopefully) cached receipt tree

  CHECK_JSON(txhash, "bytes32", "Invalid arguments for Tx: ");

  TRY_ASYNC(get_eth_tx(ctx, txhash, &tx_data));

  ssz_builder_t block_proof  = {0};
  uint32_t      tx_index     = json_get_uint32(tx_data, "transactionIndex");
  json_t        block_number = json_get(tx_data, "blockNumber");
  json_get_bytes(tx_data, "blockHash", &buf);

  TRY_ADD_ASYNC(status, c4_op_create_block_proof(ctx, block_number, &block_proof));
#ifdef PROVER_CACHE
  if (status == C4_SUCCESS) {
    bytes32_t cachekey;
    c4_eth_receipt_cachekey(cachekey, block_hash);
    receipt_tree = (node_t*) c4_prover_cache_get(ctx, cachekey);
  }
#endif
  if (!receipt_tree)
    TRY_ADD_ASYNC(status, eth_getBlockReceipts(ctx, block_number, &block_receipts));
  TRY_ASYNC_CATCH(status, ssz_builder_free(&block_proof));

  TRY_ASYNC_CATCH(c4_eth_get_receipt_proof(ctx, receipt_tree, block_hash, block_receipts, tx_index, &receipt, &receipt_proof),
                  ssz_builder_free(&block_proof));

  TRY_ASYNC_FINAL(
      create_op_receipt_proof(ctx, block_proof, receipt_proof, receipt),
      safe_free(receipt_proof.bytes.data));
  return C4_SUCCESS;
}