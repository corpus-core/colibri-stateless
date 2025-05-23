#include "beacon.h"
#include "beacon_types.h"
#include "eth_req.h"
#include "eth_tools.h"
#include "historic_proof.h"
#include "json.h"
#include "logger.h"
#include "patricia.h"
#include "proofer.h"
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
  beacon_block_t           beacon_block;
  bytes32_t                body_root;
  blockroot_proof_t        block_proof;
} proof_logs_block_t;

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

static c4_status_t get_receipts(proofer_ctx_t* ctx, proof_logs_block_t* blocks) {
  c4_status_t status   = C4_SUCCESS;
  uint8_t     tmp[100] = {0};
  buffer_t    buf      = stack_buffer(tmp);
  for (proof_logs_block_t* block = blocks; block; block = block->next) {
    buffer_reset(&buf);
    json_t block_number = json_parse(bprintf(&buf, "\"0x%lx\"", block->block_number));
    TRY_ADD_ASYNC(status, c4_beacon_get_block_for_eth(ctx, block_number, &block->beacon_block));
#ifdef PROOFER_CACHE
    // we get the merkle tree from the cache if available now so we can use it later in the worker thread
    bytes32_t cachekey;
    if (status == C4_SUCCESS && block->beacon_block.execution.bytes.data &&
        c4_proofer_cache_get(ctx, c4_eth_receipt_cachekey(cachekey, ssz_get(&block->beacon_block.execution, "receiptsRoot").bytes.data)))
      continue;
#endif

    TRY_ADD_ASYNC(status, eth_getBlockReceipts(ctx, block_number, &block->block_receipts));
  }
  return status;
}

static c4_status_t proof_create_multiproof(proofer_ctx_t* ctx, proof_logs_block_t* block) {

  int       i      = 0;
  gindex_t* gindex = safe_calloc(3 + block->tx_count, sizeof(gindex_t));
  gindex[0]        = ssz_gindex(block->beacon_block.body.def, 2, "executionPayload", "blockNumber");
  gindex[1]        = ssz_gindex(block->beacon_block.body.def, 2, "executionPayload", "blockHash");
  gindex[2]        = ssz_gindex(block->beacon_block.body.def, 2, "executionPayload", "receiptsRoot");
  for (proof_logs_tx_t* tx = block->txs; tx; tx = tx->next, i++)
    gindex[i + 3] = ssz_gindex(block->beacon_block.body.def, 3, "executionPayload", "transactions", tx->tx_index);

  block->proof = ssz_create_multi_proof_for_gindexes(block->beacon_block.body, block->body_root, gindex, 3 + block->tx_count);
  safe_free(gindex);

  return C4_SUCCESS;
}

static c4_status_t proof_block(proofer_ctx_t* ctx, proof_logs_block_t* block) {
  node_t*   root         = NULL;
  bytes32_t tmp          = {0};
  buffer_t  receipts_buf = {0};
  buffer_t  buf          = stack_buffer(tmp);

  block->block_hash = ssz_get(&block->beacon_block.execution, "blockHash").bytes;

  TRY_ASYNC(c4_check_historic_proof(ctx, &block->block_proof, block->beacon_block.slot));

#ifdef PROOFER_CACHE
  bytes32_t cachekey;
  root = (node_t*) c4_proofer_cache_get(ctx, c4_eth_receipt_cachekey(cachekey, block->block_hash.data));
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
    c4_proofer_cache_set(ctx, cachekey, root, 500 * len + 200, 200 * 1000, (cache_free_cb) patricia_node_free);
  }
#endif

  // create receipts proofs
  for (proof_logs_tx_t* tx = block->txs; tx; tx = tx->next) {
    tx->proof  = patricia_create_merkle_proof(root, c4_eth_create_tx_path(tx->tx_index, &buf));
    tx->raw_tx = ssz_at(ssz_get(&block->beacon_block.execution, "transactions"), tx->tx_index).bytes;
  }
  // create multiproof for the transactions
  proof_create_multiproof(ctx, block);
#ifndef PROOFER_CACHE
  patricia_node_free(root);
#endif
  buffer_free(&buf);
  buffer_free(&receipts_buf);

  return C4_SUCCESS;
}

static c4_status_t serialize_log_proof(proofer_ctx_t* ctx, proof_logs_block_t* blocks, json_t logs) {

  buffer_t         tmp         = {0};
  ssz_builder_t    block_list  = ssz_builder_for_type(ETH_SSZ_VERIFY_LOGS_PROOF);
  uint32_t         block_count = get_block_count(blocks);
  const ssz_def_t* block_def   = block_list.def->def.vector.type;
  const ssz_def_t* txs_def     = ssz_get_def(block_def, "txs");

  for (proof_logs_block_t* block = blocks; block; block = block->next) {
    ssz_builder_t block_ssz = ssz_builder_for_def(block_def);
    ssz_add_uint64(&block_ssz, block->block_number);
    ssz_add_bytes(&block_ssz, "blockHash", block->block_hash);
    ssz_add_bytes(&block_ssz, "proof", block->proof);
    ssz_add_builders(&block_ssz, "header", c4_proof_add_header(block->beacon_block.header, block->body_root));
    ssz_add_blockroot_proof(&block_ssz, &block->beacon_block, block->block_proof);

    ssz_builder_t tx_list = ssz_builder_for_def(txs_def);
    for (proof_logs_tx_t* tx = block->txs; tx; tx = tx->next) {
      ssz_builder_t tx_ssz = ssz_builder_for_def(txs_def->def.vector.type);
      ssz_add_bytes(&tx_ssz, "transaction", tx->raw_tx);
      ssz_add_uint32(&tx_ssz, tx->tx_index);
      ssz_add_bytes(&tx_ssz, "proof", tx->proof.bytes);
      ssz_add_dynamic_list_builders(&tx_list, block->tx_count, tx_ssz);
    }
    ssz_add_builders(&block_ssz, "txs", tx_list);
    ssz_add_dynamic_list_builders(&block_list, block_count, block_ssz);
  }

  ctx->proof = eth_create_proof_request(
      ctx->chain_id,
      FROM_JSON(logs, ETH_SSZ_DATA_LOGS),
      block_list,
      NULL_SSZ_BUILDER);

  buffer_free(&tmp);
  return C4_SUCCESS;
}

c4_status_t c4_proof_logs(proofer_ctx_t* ctx) {
  json_t              logs   = {0};
  proof_logs_block_t* blocks = NULL;
  TRY_ASYNC(eth_get_logs(ctx, ctx->params, &logs));

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