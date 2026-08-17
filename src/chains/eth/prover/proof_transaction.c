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
#include "eth_compute_units.h"
#include "eth_req.h"
#include "eth_tools.h"
#include "historic_proof.h"
#include "json.h"
#include "logger.h"
#include "prover.h"
#include "ssz.h"
#include "sync_committee.h"
#include "tx_cache.h"
#include "version.h"
#include <inttypes.h> // Include this header for PRIu64 and PRIx64
#include <stdlib.h>
#include <string.h>

#include "patricia.h"
static ssz_ob_t create_tx_proof(ssz_ob_t execution_payload, uint32_t tx_index, node_t** root_var) {
  node_t*   root         = NULL;
  bytes32_t tmp          = {0};
  buffer_t  tx_buf = {0};
  buffer_t  buf          = stack_buffer(tmp);
  if (root_var && *root_var) 
    root     = *root_var;
  else {
    ssz_ob_t transactions = ssz_get(&execution_payload, "transactions");
    uint32_t len = ssz_len(transactions);
    for (uint32_t i = 0; i < len; i++) 
      patricia_set_value(&root, c4_eth_create_tx_path(i, &buf), ssz_at(transactions, i).bytes);
  }

  ssz_ob_t proof = patricia_create_merkle_proof(root, c4_eth_create_tx_path(tx_index, &buf));

  if (root_var)
    *root_var = root;
  else
    patricia_node_free(root);

  buffer_free(&buf);
  return proof;
}

c4_status_t c4_eth_get_tx_proof(prover_ctx_t* ctx, bytes32_t block_hash, ssz_ob_t execution_payload, uint32_t tx_index,  ssz_ob_t* tx_proof) {

  // Account for Patricia trie work: one insertion per receipt plus one proof.
  // When the trie is served from the prover cache the linear part is essentially
  // a no-op, but we still bill it because the original work that filled the
  // cache was performed by this server -- this keeps the formula simple.
  eth_cu_add_patricia(ctx, 100, 1);

  // now we should have all data required to create the proof
#ifdef PROVER_CACHE
  bytes32_t cachekey;
  c4_eth_tx_cachekey(cachekey, block_hash);
  node_t* tx_tree = (node_t*) c4_prover_cache_get(ctx, cachekey);
  bool    cache_hit    = tx_tree != NULL;
  if (!cache_hit) REQUEST_WORKER_THREAD(ctx);
  *tx_proof = create_tx_proof(execution_payload, tx_index, &tx_tree);
  if (!cache_hit) c4_prover_cache_set(ctx, cachekey, tx_tree, 100000, 200000, (cache_free_cb) patricia_node_free);
#else
  *tx_proof = create_receipts_proof(execution_payload, tx_index, NULL);
#endif
  return C4_SUCCESS;
}


static c4_status_t create_eth_tx_proof(prover_ctx_t* ctx, uint32_t tx_index, beacon_block_t* block_data, bytes32_t body_root, ssz_ob_t tx_proof, blockroot_proof_t block_proof) {

  ssz_builder_t eth_tx_proof = ssz_builder_for_type(ETH_SSZ_VERIFY_TRANSACTION_PROOF);
  ssz_builder_t sync_proof   = NULL_SSZ_BUILDER;

  // get the sync_proof if needed
  TRY_ASYNC(c4_get_syncdata_proof(ctx, &block_proof.sync, &sync_proof));

  // build the proof
  ssz_add_uint32(&eth_tx_proof, tx_index);
  ssz_add_bytes(&eth_tx_proof, "transactionProof", tx_proof.bytes);
  eth_add_block_proof(ctx, &eth_tx_proof, block_data, &block_proof);

  ctx->proof = eth_create_proof_request(
      ctx->chain_id,
      NULL_SSZ_BUILDER,
      eth_tx_proof,
      sync_proof);

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
  ssz_ob_t tx_proof = {0};
  c4_status_t       status       = C4_SUCCESS;
#ifdef PROVER_CACHE
  uint8_t  block_buffer[32] = {0};
  buffer_t block_buf        = stack_buffer(block_buffer);
#endif
  TRACE_START(ctx, "get_data");
  if (strcmp(ctx->method, "eth_getTransactionByBlockHashAndIndex") == 0 || strcmp(ctx->method, "eth_getTransactionByBlockNumberAndIndex") == 0) {
    tx_index     = json_as_uint32(json_at(ctx->params, 1));
    block_number = json_at(ctx->params, 0);
  }
  else { // eth_getTransactionByHash
    if (txhash.type != JSON_TYPE_STRING || txhash.len != 68 || txhash.start[1] != '0' || txhash.start[2] != 'x') THROW_ERROR("Invalid hash");
#ifdef PROVER_CACHE
    // check tx cache for the block number and tx index if we have it
    uint64_t  block_number_val = 0;
    bytes32_t tx_hash          = {0};
    hex_to_bytes(txhash.start + 1, txhash.len - 2, bytes(tx_hash, 32));
    if (c4_eth_tx_cache_get(tx_hash, &block_number_val, &tx_index))
      block_number = json_parse(bprintf(&block_buf, "\"0x%lx\"", block_number_val));
    TRACE_ADD_STR(ctx, "tx_cache_hit", block_number_val ? "hit" : "miss");
#endif
    if (block_number.type == JSON_TYPE_INVALID) {
      TRY_ASYNC(get_eth_tx(ctx, txhash, &tx_data));
      if (tx_data.type == JSON_TYPE_NULL) { // did not find the tx or it is not mined yet
        ctx->proof = eth_create_proof_request(ctx->chain_id, NULL_SSZ_BUILDER, NULL_SSZ_BUILDER, NULL_SSZ_BUILDER);
        return C4_SUCCESS;
      }
      tx_index     = json_get_uint32(tx_data, "transactionIndex");
      block_number = json_get(tx_data, "blockNumber");
      if (block_number.type != JSON_TYPE_STRING || block_number.len < 5 || block_number.start[1] != '0' || block_number.start[2] != 'x') THROW_ERROR("Invalid block number");
    }
  }

  // get the block: hybrid mode fetches the verified execution payload from the remote
  // prover and makes sure the RLP EL header ends up in the verifier header cache, so
  // eth_add_block_proof can reference the block by hash only (blockHash union variant).
  if (ctx->flags & C4_PROVER_FLAG_HYBRID) {
    TRY_ADD_ASYNC(status, c4_beacon_get_execution_for_eth(ctx, block_number, &block));
    if (status == C4_SUCCESS) TRY_ADD_ASYNC(status, c4_hybrid_ensure_el_header(ctx, block.execution));
  }
  else
    // get the beacon-block with signature
    TRY_ADD_ASYNC(status, c4_beacon_get_block_for_eth(ctx, block_number, &block));

  // check if we need historical proofs
  if (block.slot) TRY_ADD_ASYNC(status, c4_check_blockroot_proof(ctx, &block_proof, &block));
  if (!block.execution.bytes.data && status == C4_SUCCESS) status = c4_state_add_error(ctx, "block execution is missing");
  else if (status == C4_SUCCESS) TRY_ADD_ASYNC(status, c4_eth_get_tx_proof(ctx,block.el_block_hash,block.execution,tx_index,&tx_proof));

  TRY_ASYNC_CATCH(status, safe_free(block_proof.historic_proof.data));
  TRACE_START(ctx, "proof_data");

  eth_cu_add_multi_proof(ctx, 4);
  TRY_ASYNC_FINAL(
      create_eth_tx_proof(ctx, tx_index, &block, body_root, tx_proof, block_proof),
      safe_free(tx_proof.bytes.data);
      c4_free_block_proof(&block_proof));
  return C4_SUCCESS;
}