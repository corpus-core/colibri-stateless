#include "../util/json.h"
#include "../util/logger.h"
#include "../util/patricia.h"
#include "../util/rlp.h"
#include "../util/ssz.h"
#include "../util/version.h"
#include "../verifier/types_beacon.h"
#include "../verifier/types_verify.h"
#include "beacon.h"
#include "eth_req.h"
#include "proofer.h"
#include "ssz_types.h"
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
      if (blocks->txs->proof.bytes.data) free(blocks->txs->proof.bytes.data);
      proof_logs_tx_t* next = blocks->txs->next;
      free(blocks->txs);
      blocks->txs = next;
    }
    if (blocks->proof.data) free(blocks->proof.data);
    proof_logs_block_t* next = blocks->next;
    free(blocks);
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
  bytes32_t tmp = {0};
  buffer_t  buf = stack_buffer(tmp);
  json_for_each_value(logs, log) {
    uint64_t            block_number = json_get_uint64(log, "blockNumber");
    uint32_t            tx_index     = json_get_uint32(log, "transactionIndex");
    proof_logs_block_t* block        = find_block(*blocks, block_number);
    if (!block) {
      block               = calloc(1, sizeof(proof_logs_block_t));
      block->block_number = block_number;
      block->next         = *blocks;
      *blocks             = block;
    }

    proof_logs_tx_t* tx = find_tx(block, tx_index);
    if (!tx) {
      tx           = calloc(1, sizeof(proof_logs_tx_t));
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
    TRY_ADD_ASYNC(status, eth_getBlockReceipts(ctx, block_number, &block->block_receipts));
  }
  return status;
}

static c4_status_t proof_create_multiproof(proofer_ctx_t* ctx, proof_logs_block_t* block) {

  int       i      = 0;
  gindex_t* gindex = calloc(3 + block->tx_count, sizeof(gindex_t));
  gindex[0]        = ssz_gindex(block->beacon_block.body.def, 2, "executionPayload", "blockNumber");
  gindex[1]        = ssz_gindex(block->beacon_block.body.def, 2, "executionPayload", "blockHash");
  gindex[2]        = ssz_gindex(block->beacon_block.body.def, 2, "executionPayload", "receiptsRoot");
  for (proof_logs_tx_t* tx = block->txs; tx; tx = tx->next, i++)
    gindex[i + 3] = ssz_gindex(block->beacon_block.body.def, 3, "executionPayload", "transactions", tx->tx_index);

  block->proof = ssz_create_multi_proof_for_gindexes(block->beacon_block.body, block->body_root, gindex, 3 + block->tx_count);
  free(gindex);

  return C4_SUCCESS;
}

static c4_status_t proof_block(proofer_ctx_t* ctx, proof_logs_block_t* block) {
  node_t*   root         = NULL;
  bytes32_t tmp          = {0};
  buffer_t  receipts_buf = {0};
  buffer_t  buf          = stack_buffer(tmp);

  block->block_hash = ssz_get(&block->beacon_block.execution, "blockHash").bytes;

  // create receipts tree
  json_for_each_value(block->block_receipts, r)
      patricia_set_value(&root,
                         c4_eth_create_tx_path(json_get_uint32(r, "transactionIndex"), &buf),
                         c4_serialize_receipt(r, &receipts_buf));

  // create receipts proofs
  for (proof_logs_tx_t* tx = block->txs; tx; tx = tx->next) {
    tx->proof  = patricia_create_merkle_proof(root, c4_eth_create_tx_path(tx->tx_index, &buf));
    tx->raw_tx = ssz_at(ssz_get(&block->beacon_block.execution, "transactions"), tx->tx_index).bytes;
  }

  // create multiproof for the transactions
  proof_create_multiproof(ctx, block);
  patricia_node_free(root);
  buffer_free(&buf);
  buffer_free(&receipts_buf);

  return C4_SUCCESS;
}

static c4_status_t serialize_log_proof(proofer_ctx_t* ctx, proof_logs_block_t* blocks, json_t logs) {

  buffer_t      tmp         = {0};
  ssz_builder_t c4_req      = ssz_builder_for(C4_REQUEST_CONTAINER);
  ssz_builder_t block_list  = {0};
  uint32_t      block_count = get_block_count(blocks);
  ssz_def_t     txs_def     = SSZ_LIST("txs", ETH_LOGS_TX_CONTAINER, 256);

  ssz_add_uniondef(&block_list, C4_REQUEST_PROOFS_UNION, "LogsProof");
  for (proof_logs_block_t* block = blocks; block; block = block->next) {
    ssz_builder_t block_ssz = ssz_builder_for(ETH_LOGS_BLOCK_CONTAINER);
    ssz_add_uint64(&block_ssz, block->block_number);
    ssz_add_bytes(&block_ssz, "blockHash", block->block_hash);
    ssz_add_bytes(&block_ssz, "proof", block->proof);
    ssz_add_builders(&block_ssz, "header", c4_proof_add_header(block->beacon_block.header, block->body_root));
    ssz_add_bytes(&block_ssz, "sync_committee_bits", ssz_get(&block->beacon_block.sync_aggregate, "syncCommitteeBits").bytes);
    ssz_add_bytes(&block_ssz, "sync_committee_signature", ssz_get(&block->beacon_block.sync_aggregate, "syncCommitteeSignature").bytes);

    ssz_builder_t tx_list = ssz_builder_for(txs_def);
    for (proof_logs_tx_t* tx = block->txs; tx; tx = tx->next) {
      ssz_builder_t tx_ssz = ssz_builder_for(ETH_LOGS_TX_CONTAINER);
      ssz_add_bytes(&tx_ssz, "transaction", tx->raw_tx);
      ssz_add_uint32(&tx_ssz, tx->tx_index);
      ssz_add_bytes(&tx_ssz, "proof", tx->proof.bytes);
      ssz_add_dynamic_list_builders(&tx_list, block->tx_count, tx_ssz);
    }
    ssz_add_builders(&block_ssz, "txs", tx_list);
    ssz_add_dynamic_list_builders(&block_list, block_count, block_ssz);
  }

  // build the request
  ssz_add_bytes(&c4_req, "version", bytes(c4_version_bytes, 4));
  ssz_add_bytes(&c4_req, "data", c4_proofer_add_data(logs, "EthLogs", &tmp));
  ssz_add_builders(&c4_req, "proof", block_list);
  ssz_add_bytes(&c4_req, "sync_data", bytes(NULL, 1));

  ctx->proof = ssz_builder_to_bytes(&c4_req).bytes;

  buffer_free(&tmp);
  return C4_SUCCESS;
}

c4_status_t c4_proof_logs(proofer_ctx_t* ctx) {
  json_t              logs   = {0};
  proof_logs_block_t* blocks = NULL;
  TRY_ASYNC(eth_get_logs(ctx, ctx->params, &logs));

  add_blocks(&blocks, logs);
  TRY_ASYNC_CATCH(get_receipts(ctx, blocks), free_blocks(blocks));

  for (proof_logs_block_t* block = blocks; block; block = block->next)
    TRY_ASYNC_CATCH(proof_block(ctx, block), free_blocks(blocks));

  // serialize the proof
  serialize_log_proof(ctx, blocks, logs);

  free_blocks(blocks);
  return C4_SUCCESS;
}