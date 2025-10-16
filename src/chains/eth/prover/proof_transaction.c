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
#include "prover.h"
#include "ssz.h"
#include "sync_committee.h"
#include "version.h"
#include <inttypes.h> // Include this header for PRIu64 and PRIx64
#include <stdlib.h>
#include <string.h>

static c4_status_t create_eth_tx_proof(prover_ctx_t* ctx, uint32_t tx_index, beacon_block_t* block_data, bytes32_t body_root, bytes_t tx_proof, blockroot_proof_t block_proof) {

  ssz_builder_t eth_tx_proof = ssz_builder_for_type(ETH_SSZ_VERIFY_TRANSACTION_PROOF);

  // build the proof
  ssz_add_bytes(&eth_tx_proof, "transaction", ssz_at(ssz_get(&block_data->execution, "transactions"), tx_index).bytes);
  ssz_add_uint32(&eth_tx_proof, tx_index);
  ssz_add_bytes(&eth_tx_proof, "blockNumber", ssz_get(&block_data->execution, "blockNumber").bytes);
  ssz_add_bytes(&eth_tx_proof, "blockHash", ssz_get(&block_data->execution, "blockHash").bytes);
  ssz_add_uint64(&eth_tx_proof, ssz_get_uint64(&block_data->execution, "baseFeePerGas"));
  ssz_add_bytes(&eth_tx_proof, "proof", tx_proof);
  ssz_add_builders(&eth_tx_proof, "header", c4_proof_add_header(block_data->header, body_root));
  ssz_add_blockroot_proof(&eth_tx_proof, block_data, block_proof);

  ctx->proof = eth_create_proof_request(
      ctx->chain_id,
      NULL_SSZ_BUILDER,
      eth_tx_proof,
      NULL_SSZ_BUILDER);

  return C4_SUCCESS;
}

c4_status_t c4_proof_transaction(prover_ctx_t* ctx) {
  bytes32_t         body_root    = {0};
  json_t            txhash       = json_at(ctx->params, 0);
  json_t            tx_data      = {0};
  beacon_block_t    block        = {0};
  uint32_t          tx_index     = 0;
  json_t            block_number = {0};
  blockroot_proof_t block_proof  = {0};
  c4_status_t       status       = C4_SUCCESS;

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

  // geth the beacon-block with signature
  TRY_ADD_ASYNC(status, c4_beacon_get_block_for_eth(ctx, block_number, &block));

  // check if we need historical proofs
  if (block.slot) TRY_ADD_ASYNC(status, c4_check_historic_proof(ctx, &block_proof, &block));

  if (status != C4_SUCCESS) {
    if (block_proof.historic_proof.data) safe_free(block_proof.historic_proof.data);
    return status;
  }

  bytes_t state_proof = ssz_create_multi_proof(block.body, body_root, 4,
                                               ssz_gindex(block.body.def, 2, "executionPayload", "blockNumber"),
                                               ssz_gindex(block.body.def, 2, "executionPayload", "blockHash"),
                                               ssz_gindex(block.body.def, 2, "executionPayload", "baseFeePerGas"),
                                               ssz_gindex(block.body.def, 3, "executionPayload", "transactions", tx_index)

  );
  TRY_ASYNC_FINAL(
      create_eth_tx_proof(ctx, tx_index, &block, body_root, state_proof, block_proof),
      safe_free(state_proof.data);
      c4_free_block_proof(&block_proof));
  return C4_SUCCESS;
}