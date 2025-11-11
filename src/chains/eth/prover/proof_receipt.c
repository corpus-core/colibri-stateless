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
#include "patricia.h"
#include "prover.h"
#include "rlp.h"
#include "ssz.h"
#include "tx_cache.h"
#include "version.h"
#include <inttypes.h> // Include this header for PRIu64 and PRIx64
#include <stdlib.h>
#include <string.h>

static c4_status_t create_eth_receipt_proof(prover_ctx_t* ctx, beacon_block_t* block_data, bytes32_t body_root, ssz_ob_t receipt_proof, json_t receipt, bytes_t tx_proof, blockroot_proof_t block_proof) {

  buffer_t      tmp          = {0};
  ssz_builder_t eth_tx_proof = ssz_builder_for_type(ETH_SSZ_VERIFY_RECEIPT_PROOF);
  uint32_t      tx_index     = json_get_uint32(receipt, "transactionIndex");
  ssz_builder_t sync_proof   = NULL_SSZ_BUILDER;

  // get the sync_proof if needed
  TRY_ASYNC(c4_get_syncdata_proof(ctx, &block_proof.sync, &sync_proof));

  // build the proof
  ssz_add_bytes(&eth_tx_proof, "transaction", ssz_at(ssz_get(&block_data->execution, "transactions"), tx_index).bytes);
  ssz_add_uint32(&eth_tx_proof, tx_index);
  ssz_add_uint64(&eth_tx_proof, json_get_uint64(receipt, "blockNumber"));
  ssz_add_bytes(&eth_tx_proof, "blockHash", json_get_bytes(receipt, "blockHash", &tmp));
  ssz_add_bytes(&eth_tx_proof, "receipt_proof", receipt_proof.bytes);
  ssz_add_bytes(&eth_tx_proof, "block_proof", tx_proof);
  ssz_add_builders(&eth_tx_proof, "header", c4_proof_add_header(block_data->header, body_root));
  ssz_add_header_proof(&eth_tx_proof, block_data, block_proof);

  buffer_free(&tmp);

  ctx->proof = eth_create_proof_request(
      ctx->chain_id,
      FROM_JSON(receipt, ETH_SSZ_DATA_RECEIPT),
      eth_tx_proof,
      sync_proof);

  return C4_SUCCESS;
}

static ssz_ob_t create_receipts_proof(json_t block_receipts, uint32_t tx_index, json_t* receipt, node_t** root_var) {
  node_t*   root         = NULL;
  bytes32_t tmp          = {0};
  buffer_t  receipts_buf = {0};
  buffer_t  buf          = stack_buffer(tmp);
  if (root_var && *root_var) {
    root     = *root_var;
    *receipt = json_at(block_receipts, tx_index);
  }
  else
    json_for_each_value(block_receipts, r) {
      uint32_t index = json_get_uint32(r, "transactionIndex");
      if (index == tx_index) *receipt = r;
      patricia_set_value(&root, c4_eth_create_tx_path(index, &buf), c4_serialize_receipt(r, &receipts_buf));
    }

  ssz_ob_t proof = patricia_create_merkle_proof(root, c4_eth_create_tx_path(tx_index, &buf));

  if (root_var)
    *root_var = root;
  else
    patricia_node_free(root);

  buffer_free(&buf);
  buffer_free(&receipts_buf);
  return proof;
}

c4_status_t c4_eth_get_receipt_proof(prover_ctx_t* ctx, bytes32_t block_hash, json_t block_receipts, uint32_t tx_index, json_t* receipt, ssz_ob_t* receipt_proof) {

  // now we should have all data required to create the proof
#ifdef PROVER_CACHE
  bytes32_t cachekey;
  c4_eth_receipt_cachekey(cachekey, block_hash);
  node_t* receipt_tree = (node_t*) c4_prover_cache_get(ctx, cachekey);
  bool    cache_hit    = receipt_tree != NULL;
  if (!cache_hit) REQUEST_WORKER_THREAD(ctx);
  if (!cache_hit) REQUEST_WORKER_THREAD_CATCH(ctx, );
  *receipt_proof = create_receipts_proof(block_receipts, tx_index, receipt, &receipt_tree);
  if (!cache_hit) c4_prover_cache_set(ctx, cachekey, receipt_tree, 100000, 200000, (cache_free_cb) patricia_node_free);
#else
  *receipt_proof = create_receipts_proof(block_receipts, tx_index, receipt, NULL);
#endif
  return C4_SUCCESS;
}

c4_status_t c4_proof_receipt(prover_ctx_t* ctx) {
  json_t            txhash         = json_at(ctx->params, 0);
  json_t            tx_data        = {0};
  json_t            block_receipts = {0};
  beacon_block_t    block          = {0};
  json_t            receipt        = {0};
  bytes32_t         body_root      = {0};
  blockroot_proof_t block_proof    = {0};
  ssz_ob_t          receipt_proof  = {0};
  c4_status_t       status         = C4_SUCCESS;
  uint32_t          tx_index       = 0;
  json_t            block_number   = {0};

  CHECK_JSON(txhash, "bytes32", "Invalid arguments for Tx: ");

  TRACE_START(ctx, "get_tx_data");

#ifdef PROVER_CACHE
  // check tx cache for the block number and tx index if we have it
  uint8_t   block_buffer[32] = {0};
  buffer_t  block_buf        = stack_buffer(block_buffer);
  uint64_t  block_number_val = 0;
  bytes32_t tx_hash          = {0};

  hex_to_bytes(txhash.start + 1, txhash.len - 2, bytes(tx_hash, 32));
  if (c4_eth_tx_cache_get(tx_hash, &block_number_val, &tx_index))
    block_number = json_parse(bprintf(&block_buf, "\"0x%lx\"", block_number_val));
  TRACE_ADD_STR(ctx, "tx_cache_hit", block_number_val ? "hit" : "miss");
#endif

  // not found in cache, so we need to get it from the RPC
  if (block_number.type == JSON_TYPE_INVALID) {
    TRY_ASYNC(get_eth_tx(ctx, txhash, &tx_data));
    tx_index     = json_get_uint32(tx_data, "transactionIndex");
    block_number = json_get(tx_data, "blockNumber");
  }

  TRACE_START(ctx, "get_beacon_block");
  TRY_ADD_ASYNC(status, c4_beacon_get_block_for_eth(ctx, block_number, &block));
  TRACE_START(ctx, "get_block_receipts");
  TRY_ADD_ASYNC(status, eth_getBlockReceipts(ctx, block_number, &block_receipts));
  TRY_ASYNC(status);

  TRACE_START(ctx, "check_blockroot_proof");
  TRY_ASYNC(c4_check_blockroot_proof(ctx, &block_proof, &block));

  TRACE_START(ctx, "receipt_proof");
  TRY_ASYNC_CATCH(c4_eth_get_receipt_proof(ctx, ssz_get(&(block.execution), "blockHash").bytes.data, block_receipts, tx_index, &receipt, &receipt_proof),
                  c4_free_block_proof(&block_proof));

  REQUEST_WORKER_THREAD_CATCH(ctx, c4_free_block_proof(&block_proof));
  TRACE_START(ctx, "multiproof");
  bytes_t state_proof = ssz_create_multi_proof(block.body, body_root, 4,
                                               ssz_gindex(block.body.def, 2, "executionPayload", "blockNumber"),
                                               ssz_gindex(block.body.def, 2, "executionPayload", "blockHash"),
                                               ssz_gindex(block.body.def, 2, "executionPayload", "receiptsRoot"),
                                               ssz_gindex(block.body.def, 3, "executionPayload", "transactions", tx_index)

  );
  TRACE_START(ctx, "finalize_proof");

  TRY_ASYNC_FINAL(
      create_eth_receipt_proof(ctx, &block, body_root, receipt_proof, receipt, state_proof, block_proof),

      safe_free(state_proof.data);
      safe_free(receipt_proof.bytes.data);
      c4_free_block_proof(&block_proof));
  return C4_SUCCESS;
}