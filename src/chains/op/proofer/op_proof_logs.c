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
#include "op_proofer.h"
#include "op_tools.h"
#include "op_types.h"
#include "op_zstd.h"
#include "patricia.h"
#include "proofer.h"
#include "ssz.h"
#include "version.h"
#include <inttypes.h> // Include this header for PRIu64 and PRIx64
#include <stdlib.h>
#include <string.h>
static const ssz_def_t EXECUTION_PAYLOAD_CONTAINER = SSZ_CONTAINER("payload", DENEP_EXECUTION_PAYLOAD);

typedef struct proof_logs_tx {
  uint64_t              block_number;
  bytes32_t             tx_hash;
  uint32_t              tx_index;
  ssz_ob_t              proof;
  bytes_t               raw_tx;
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
  ssz_builder_t            block_proof;
  bytes32_t                cache_key;
  ssz_ob_t*                execution_payload;
} proof_logs_block_t;

typedef enum {
  ETH_GET_LOGS   = 0,
  ETH_PROOF_LOGS = 1
} logs_proof_type_t;

static inline logs_proof_type_t proof_logs_block_proof_type(proofer_ctx_t* ctx) {
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
      if (blocks->txs->proof.bytes.data) safe_free(blocks->txs->proof.bytes.data);
      proof_logs_tx_t* next = blocks->txs->next;
      safe_free(blocks->txs);
      blocks->txs = next;
    }
    if (blocks->proof.data) safe_free(blocks->proof.data);
    if (blocks->execution_payload) safe_free(blocks->execution_payload);
    ssz_builder_free(&blocks->block_proof);
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
      block               = safe_calloc(1, sizeof(proof_logs_block_t));
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

static ssz_ob_t* get_execution_payload(proof_logs_block_t* block) {
  if (!block->execution_payload) {
    size_t  len     = op_zstd_get_decompressed_size(block->block_proof.dynamic.data);
    bytes_t payload = bytes(safe_malloc(len), len);
    op_zstd_decompress(block->block_proof.dynamic.data, payload);
    ssz_ob_t* ob             = (ssz_ob_t*) (void*) payload.data;
    ob->bytes                = bytes_slice(payload, 32, len - 32);
    ob->def                  = &EXECUTION_PAYLOAD_CONTAINER;
    block->execution_payload = ob;
  }
  return block->execution_payload;
}

static uint8_t* get_cache_key(proof_logs_block_t* block) {
  if (bytes_all_zero(bytes(block->cache_key, 32))) {
    if (!block->block_proof.def) return NULL;
    ssz_ob_t* execution_payload = get_execution_payload(block);
    ssz_ob_t  receipt_root      = ssz_get(execution_payload, "receiptsRoot");
    memcpy(block->cache_key, receipt_root.bytes.data, 32);
  }
  return block->cache_key;
}

static c4_status_t get_receipts(proofer_ctx_t* ctx, proof_logs_block_t* blocks) {
  c4_status_t status   = C4_SUCCESS;
  uint8_t     tmp[100] = {0};
  buffer_t    buf      = stack_buffer(tmp);
  for (proof_logs_block_t* block = blocks; block; block = block->next) {
    buffer_reset(&buf);
    json_t block_number = json_parse(bprintf(&buf, "\"0x%lx\"", block->block_number));
    TRY_ADD_ASYNC(status, c4_op_create_block_proof(ctx, block_number, &block->block_proof));
#ifdef PROOFER_CACHE
    // we get the merkle tree from the cache if available now so we can use it later in the worker thread
    if (status == C4_SUCCESS && get_cache_key(block) && c4_proofer_cache_get(ctx, block->cache_key))
      continue;
#endif

    TRY_ADD_ASYNC(status, eth_getBlockReceipts(ctx, block_number, &block->block_receipts));
  }
  return status;
}

static c4_status_t proof_block(proofer_ctx_t* ctx, proof_logs_block_t* block) {
  node_t*   root         = NULL;
  bytes32_t tmp          = {0};
  buffer_t  receipts_buf = {0};
  buffer_t  buf          = stack_buffer(tmp);

#ifdef PROOFER_CACHE
  root = (node_t*) c4_proofer_cache_get(ctx, get_cache_key(block));
  if (!root) {
    REQUEST_WORKER_THREAD(ctx);
    int len = 0;
#endif
    // create receipts tree
    json_for_each_value(block->block_receipts, r) {
      patricia_set_value(&root,
                         c4_eth_create_tx_path(json_get_uint32(r, "transactionIndex"), &buf),
                         c4_serialize_receipt(r, &receipts_buf));
#ifdef PROOFER_CACHE
      len++;
#endif
    }
#ifdef PROOFER_CACHE
    c4_proofer_cache_set(ctx, block->cache_key, root, 500 * len + 200, 200 * 1000, (cache_free_cb) patricia_node_free);
  }
#endif

  // create receipts proofs
  for (proof_logs_tx_t* tx = block->txs; tx; tx = tx->next)
    tx->proof = patricia_create_merkle_proof(root, c4_eth_create_tx_path(tx->tx_index, &buf));

#ifndef PROOFER_CACHE
  patricia_node_free(root);
#endif
  buffer_free(&buf);
  buffer_free(&receipts_buf);

  return C4_SUCCESS;
}

static c4_status_t serialize_log_proof(proofer_ctx_t* ctx, proof_logs_block_t* blocks, json_t logs) {

  buffer_t         tmp         = {0};
  ssz_builder_t    block_list  = ssz_builder_for_op_type(OP_SSZ_VERIFY_LOGS_PROOF);
  uint32_t         block_count = get_block_count(blocks);
  const ssz_def_t* block_def   = block_list.def->def.vector.type;
  const ssz_def_t* txs_def     = ssz_get_def(block_def, "txs");

  for (proof_logs_block_t* block = blocks; block; block = block->next) {
    ssz_builder_t block_ssz = ssz_builder_for_def(block_def);
    ssz_add_builders(&block_ssz, "block_proof", block->block_proof);
    block->block_proof = (ssz_builder_t) {0};

    ssz_builder_t tx_list = ssz_builder_for_def(txs_def);
    for (proof_logs_tx_t* tx = block->txs; tx; tx = tx->next) {
      ssz_builder_t tx_ssz = ssz_builder_for_def(txs_def->def.vector.type);
      ssz_add_uint32(&tx_ssz, tx->tx_index);
      ssz_add_bytes(&tx_ssz, "proof", tx->proof.bytes);
      ssz_add_bytes(&tx_ssz, "tx_proof", NULL_BYTES);
      ssz_add_dynamic_list_builders(&tx_list, block->tx_count, tx_ssz);
    }
    ssz_add_builders(&block_ssz, "txs", tx_list);
    ssz_add_dynamic_list_builders(&block_list, block_count, block_ssz);
  }

  ctx->proof = op_create_proof_request(
      ctx->chain_id,
      proof_logs_block_proof_type(ctx) == ETH_GET_LOGS ? FROM_JSON(logs, ETH_SSZ_DATA_LOGS) : NULL_SSZ_BUILDER,
      block_list,
      NULL_SSZ_BUILDER);

  buffer_free(&tmp);
  return C4_SUCCESS;
}

c4_status_t c4_proof_logs(proofer_ctx_t* ctx) {
  json_t              logs   = {0};
  proof_logs_block_t* blocks = NULL;
  if (proof_logs_block_proof_type(ctx) == ETH_GET_LOGS)
    TRY_ASYNC(eth_get_logs(ctx, ctx->params, &logs));
  else
    logs = ctx->params;

  add_blocks(&blocks, logs);
  TRY_ASYNC_CATCH(get_receipts(ctx, blocks), free_blocks(blocks));

  // now we have all the blockreceipts and the beaconblock.

  // create the merkle proofs for all the blocks
  for (proof_logs_block_t* block = blocks; block; block = block->next)
    TRY_ASYNC_CATCH(proof_block(ctx, block), free_blocks(blocks));

  // serialize the proof
  serialize_log_proof(ctx, blocks, logs);

  free_blocks(blocks);
  return C4_SUCCESS;
}