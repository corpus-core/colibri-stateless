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
#include "el_header.h"
#include "eth_compute_units.h"
#include "eth_req.h"
#include "eth_tools.h"
#include "historic_proof.h"
#include "json.h"
#include "logger.h"
#ifdef PROVER_CACHE
#include "logs_cache.h"
#endif
#include "patricia.h"
#include "proof_logs_completeness.h"
#include "prover.h"
#include "rlp.h"
#include "ssz.h"
#include "version.h"
#include <inttypes.h> // Include this header for PRIu64 and PRIx64
#include <stdlib.h>
#include <string.h>

typedef struct proof_logs_tx {
  uint64_t              block_number;
  bytes32_t             tx_hash;
  uint32_t              tx_index;
  uint32_t              log_index;
  struct proof_logs_tx* next;
} proof_logs_tx_t;

typedef struct proof_logs_block {
  uint64_t                 block_number;
  bytes_t                  block_hash;
  bytes_t                  proof;
  struct proof_logs_block* next;
  json_t                   block_receipts;
  proof_logs_tx_t*         txs;
  uint32_t                 tx_count;
  eth_block_t              beacon_block;
  bytes32_t                body_root;
  blockroot_proof_t        block_proof;
  ssz_ob_t                 receipt_proof;
  ssz_ob_t                 tx_proof;
} proof_logs_block_t;

typedef enum {
  ETH_GET_LOGS   = 0,
  ETH_PROOF_LOGS = 1
} logs_proof_type_t;

static inline logs_proof_type_t proof_logs_block_proof_type(prover_ctx_t* ctx) {
  return !ctx->method || strcmp(ctx->method, "eth_getLogs") == 0
             ? ETH_GET_LOGS
             : ETH_PROOF_LOGS;
}

static inline uint32_t get_block_count(proof_logs_block_t* blocks) {
  uint32_t count = 0;
  while (blocks) {
    count++;
    blocks = blocks->next;
  }
  return count;
}

static void free_blocks(proof_logs_block_t* blocks) {
  while (blocks) {
    while (blocks->txs) {
      proof_logs_tx_t* next = blocks->txs->next;
      safe_free(blocks->txs);
      blocks->txs = next;
    }
    if (blocks->receipt_proof.bytes.data) safe_free(blocks->receipt_proof.bytes.data);
    if (blocks->tx_proof.bytes.data) safe_free(blocks->tx_proof.bytes.data);
    if (blocks->proof.data) safe_free(blocks->proof.data);
    c4_free_block_proof(&blocks->block_proof);
    proof_logs_block_t* next = blocks->next;
    safe_free(blocks);
    blocks = next;
  }
}

static inline proof_logs_block_t* find_block(proof_logs_block_t* blocks, uint64_t block_number) {
  while (blocks && blocks->block_number != block_number) blocks = blocks->next;
  return blocks;
}

static inline proof_logs_tx_t* find_tx(proof_logs_block_t* block, uint32_t tx_index) {
  if (!block) return NULL;
  proof_logs_tx_t* tx = block->txs;
  while (tx && tx->tx_index != tx_index) tx = tx->next;
  return tx;
}

static inline void add_blocks(proof_logs_block_t** blocks, json_t logs) {
  json_for_each_value(logs, log) {
    uint64_t            block_number = json_get_uint64(log, "blockNumber");
    uint32_t            tx_index     = json_get_uint32(log, "transactionIndex");
    proof_logs_block_t* block        = find_block(*blocks, block_number);
    if (!block) {
      block = safe_calloc(1, sizeof(proof_logs_block_t));
#ifdef __clang_analyzer__
      memset(block, 0, sizeof(proof_logs_block_t));
#endif
      block->block_number = block_number;
      block->next         = *blocks;
      *blocks             = block;
    }

    proof_logs_tx_t* tx = find_tx(block, tx_index);
    if (!tx) {
      tx           = safe_calloc(1, sizeof(proof_logs_tx_t));
      tx->tx_index = tx_index;
      tx->next     = block->txs;
      block->txs   = tx;
      block->tx_count++;
    }
  }
}

static c4_status_t get_receipts(prover_ctx_t* ctx, proof_logs_block_t* blocks) {
  c4_status_t status   = C4_SUCCESS;
  uint8_t     tmp[100] = {0};
  buffer_t    buf      = stack_buffer(tmp);
  for (proof_logs_block_t* block = blocks; block; block = block->next) {
    buffer_reset(&buf);
    json_t block_number = json_parse(bprintf(&buf, "\"0x%lx\"", block->block_number));
    TRY_ADD_ASYNC(status, c4_beacon_get_block_for_eth_with_body(ctx, block_number, &block->beacon_block));
#ifdef PROVER_CACHE
    // we get the merkle tree from the cache if available now so we can use it later in the worker thread
    bytes32_t cachekey;
    if (status == C4_SUCCESS && block->beacon_block.el_header.data &&
        c4_prover_cache_get(ctx, c4_eth_receipt_cachekey(cachekey, block->beacon_block.el_block_hash)) &&
        c4_prover_cache_get(ctx, c4_eth_tx_cachekey(cachekey, block->beacon_block.el_block_hash)))
      continue;
#endif

    TRY_ADD_ASYNC(status, eth_getBlockReceipts(ctx, block_number, &block->block_receipts));
  }
  return status;
}

static c4_status_t proof_block(prover_ctx_t* ctx, proof_logs_block_t* block) {
  node_t* receipt_root = NULL;
  node_t* tx_root      = NULL;

  bytes32_t tmp          = {0};
  buffer_t  receipts_buf = {0};
  buffer_t  buf          = stack_buffer(tmp);

  block->block_hash = bytes(block->beacon_block.el_block_hash, 32);

  TRY_ASYNC(c4_check_blockroot_proof(ctx, &block->block_proof, &block->beacon_block));

  TRACE_START(ctx, "build_receipt_tree");
  TRACE_ADD_UINT64(ctx, "block", block->block_number);

  // Patricia trie work for this block: linear cost per receipt inserted into
  // the trie, plus one proof per matching tx.
  eth_cu_add_patricia(ctx, (uint32_t) json_len(block->block_receipts), block->tx_count);

#ifdef PROVER_CACHE
  bytes32_t cachekey_receipts;
  bytes32_t cachekey_txs;
  receipt_root = (node_t*) c4_prover_cache_get(ctx, c4_eth_receipt_cachekey(cachekey_receipts, block->block_hash.data));
  tx_root      = (node_t*) c4_prover_cache_get(ctx, c4_eth_tx_cachekey(cachekey_txs, block->block_hash.data));
  if (!receipt_root || !tx_root) {
    REQUEST_WORKER_THREAD(ctx);
    int len = 0;
#endif
    if (!receipt_root) {
      // create receipts tree
      json_for_each_value(block->block_receipts, r) {
        patricia_set_value(&receipt_root,
                           c4_eth_create_tx_path(json_get_uint32(r, "transactionIndex"), &buf),
                           c4_serialize_receipt(r, &receipts_buf));
#ifdef PROVER_CACHE
        len++;
#endif
      }
#ifdef PROVER_CACHE
      c4_prover_cache_set(ctx, cachekey_receipts, receipt_root, 500 * len + 200, 200 * 1000, (cache_free_cb) patricia_node_free);
    }
#endif
  }
  if (!tx_root) {
    ssz_ob_t txs = ssz_get(&block->beacon_block.el_body, "transactions");
    uint32_t len = ssz_len(txs);
    for (int i = 0; i < len; i++)
      patricia_set_value(&tx_root,
                         c4_eth_create_tx_path(i, &buf),
                         ssz_at(txs, i).bytes);

#ifdef PROVER_CACHE
    c4_prover_cache_set(ctx, cachekey_txs, tx_root, 500 * len + 200, 200 * 1000, (cache_free_cb) patricia_node_free);
#endif
  }

  TRACE_START(ctx, "create_receipt_proofs");
  TRACE_ADD_UINT64(ctx, "block", block->block_number);
  TRACE_ADD_UINT64(ctx, "tx_count", block->tx_count);
  mpt_builder_t receipt_builder = {0};
  mpt_builder_t tx_builder      = {0};
  mpt_builder_init(&receipt_builder, receipt_root);
  mpt_builder_init(&tx_builder, tx_root);

  proof_logs_tx_t* next_tx = NULL;
  for (proof_logs_tx_t* tx = block->txs; tx; tx = next_tx) {
    bytes_t path = c4_eth_create_tx_path(tx->tx_index, &buf);
    next_tx      = tx->next;
    mpt_builder_add_proof(&receipt_builder, path);
    mpt_builder_add_proof(&tx_builder, path);
  }
  block->receipt_proof = mpt_builder_finish(&receipt_builder);
  block->tx_proof      = mpt_builder_finish(&tx_builder);

#ifndef PROVER_CACHE
  patricia_node_free(receipt_root);
  patricia_node_free(tx_root);
#endif
  buffer_free(&buf);
  buffer_free(&receipts_buf);

  return C4_SUCCESS;
}

static c4_status_t serialize_log_proof(prover_ctx_t* ctx, proof_logs_block_t* blocks, json_t logs, ssz_builder_t sync_proof) {

  buffer_t         tmp         = {0};
  ssz_builder_t    block_list  = ssz_builder_for_type(ETH_SSZ_VERIFY_LOGS_PROOF);
  uint32_t         block_count = get_block_count(blocks);
  const ssz_def_t* block_def   = block_list.def->def.vector.type;
  const ssz_def_t* txs_def     = ssz_get_def(block_def, "txs");

  for (proof_logs_block_t* block = blocks; block; block = block->next) {
    ssz_builder_t block_ssz = ssz_builder_for_def(block_def);
    ssz_builder_t tx_list   = ssz_builder_for_def(txs_def);
    for (proof_logs_tx_t* tx = block->txs; tx; tx = tx->next) {
      ssz_builder_t tx_ssz = ssz_builder_for_def(txs_def->def.vector.type);
      // TODO: fill in the logIndex and gasUsed
      ssz_add_uint32(&tx_ssz, tx->log_index); // logIndex
      ssz_add_uint32(&tx_ssz, tx->tx_index);  // transactionIndex
      ssz_add_dynamic_list_builders(&tx_list, block->tx_count, tx_ssz);
    }
    ssz_add_uint64(&block_ssz, block->block_number);
    ssz_add_bytes(&block_ssz, "transactionProof", block->tx_proof.bytes);
    ssz_add_bytes(&block_ssz, "receiptProof", block->receipt_proof.bytes);
    ssz_add_builders(&block_ssz, "txs", tx_list);
    eth_add_block_proof(ctx, &block_ssz, &block->beacon_block, &block->block_proof);

    ssz_add_dynamic_list_builders(&block_list, block_count, block_ssz);
  }

  ctx->proof = eth_create_proof_request(
      ctx->chain_id,
      proof_logs_block_proof_type(ctx) == ETH_GET_LOGS ? FROM_JSON(logs, ETH_SSZ_DATA_LOGS) : NULL_SSZ_BUILDER,
      block_list,
      sync_proof);

  buffer_free(&tmp);
  return C4_SUCCESS;
}

c4_status_t c4_proof_logs(prover_ctx_t* ctx) {
  // Validate the eth_getLogs filter object up front. eth_proofLogs passes the logs array as params,
  // so the filter schema only applies to eth_getLogs.
  if (proof_logs_block_proof_type(ctx) == ETH_GET_LOGS)
    CHECK_JSON_INPUT(json_at(ctx->params, 0), JSON_GET_LOGS_FILTER_FIELDS, "Invalid eth_getLogs filter: ");

  // A completeness proof is only meaningful for eth_getLogs (range queries), not for eth_proofLogs.
  if ((ctx->flags & C4_PROVER_FLAG_LOGS_COMPLETENESS) && proof_logs_block_proof_type(ctx) == ETH_GET_LOGS)
    return c4_proof_logs_completeness(ctx);

  json_t              logs          = {0};
  proof_logs_block_t* blocks        = NULL;
  const chain_spec_t* chain         = c4_eth_get_chain_spec(ctx->chain_id);
  ssz_builder_t       sync_proof    = NULL_SSZ_BUILDER;
  proof_logs_block_t* highest_block = NULL;

  TRACE_START(ctx, "fetch_logs");

  if (proof_logs_block_proof_type(ctx) == ETH_GET_LOGS) { // for eth_getLogs
#ifdef PROVER_CACHE
    bool served = false;
    TRY_ASYNC(c4_eth_logs_cache_scan(ctx, json_at(ctx->params, 0), &logs, &served));

    if (!served)
      TRY_ASYNC(eth_get_logs(ctx, ctx->params, &logs)); // fallback to RPC
#else
    TRY_ASYNC(eth_get_logs(ctx, ctx->params, &logs)); // => we fetch it from rpc
#endif
  }
  else                  // for eth_proofLogs
    logs = ctx->params; // => we use the logs from the proof request

  TRACE_START(ctx, "get_receipts");

  add_blocks(&blocks, logs); // find which blocks do we need
  TRY_ASYNC_CATCH(get_receipts(ctx, blocks), free_blocks(blocks));

  // now we have all the blockreceipts and the beaconblock.
  for (proof_logs_block_t* block = blocks; block; block = block->next) {
    if (!highest_block || block->beacon_block.slot > highest_block->beacon_block.slot) highest_block = block;
  }
  for (proof_logs_block_t* block = blocks; block; block = block->next)
    block->block_proof.sync.required_period = highest_block->beacon_block.slot >> (chain->slots_per_epoch_bits + chain->epochs_per_period_bits);

  // create the merkle proofs for all the blocks
  for (proof_logs_block_t* block = blocks; block; block = block->next)
    TRY_ASYNC_CATCH(proof_block(ctx, block), free_blocks(blocks));

  if (highest_block)
    TRY_ASYNC(c4_get_syncdata_proof(ctx, &highest_block->block_proof.sync, &sync_proof));

  TRACE_START(ctx, "serialize_proof");
  TRACE_ADD_UINT64(ctx, "log_count", json_len(logs));
  TRACE_ADD_UINT64(ctx, "block_count", get_block_count(blocks));

  serialize_log_proof(ctx, blocks, logs, sync_proof);
  TRACE_END(ctx);

  free_blocks(blocks);
  return C4_SUCCESS;
}