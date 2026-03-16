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
#include "patricia.h"
#include "ssz.h"
#include "sync_committee.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define GINDEX_TRANSACTIONS  813
#define GINDEX_BASEFEEPERGAS 811

static bool verify_block_receipts_merkle_proof(verify_ctx_t* ctx, ssz_ob_t proof, bytes_t block_hash, bytes_t block_number,
                                               bytes32_t receipt_root, bytes32_t tx_root, bytes_t base_fee_per_gas,
                                               bytes32_t body_root) {
  uint8_t   leafes[5 * 32] = {0};
  bytes32_t root_hash      = {0};
  gindex_t  gindexes[]     = {GINDEX_BLOCKUMBER, GINDEX_BLOCHASH, GINDEX_RECEIPT_ROOT, GINDEX_TRANSACTIONS, GINDEX_BASEFEEPERGAS};

  memcpy(leafes, block_number.data, block_number.len);
  memcpy(leafes + 32, block_hash.data, block_hash.len);
  memcpy(leafes + 64, receipt_root, 32);
  memcpy(leafes + 96, tx_root, 32);
  memcpy(leafes + 128, base_fee_per_gas.data, base_fee_per_gas.len);

  if (!ssz_verify_multi_merkle_proof(proof.bytes, bytes(leafes, sizeof(leafes)), gindexes, root_hash))
    RETURN_VERIFY_ERROR(ctx, "invalid block receipts proof, missing nodes!");
  if (memcmp(root_hash, body_root, 32) != 0)
    RETURN_VERIFY_ERROR(ctx, "invalid block receipts proof, body root mismatch!");
  return true;
}

bool verify_block_receipts_proof_for(verify_ctx_t* ctx, ssz_ob_t receipts_proof) {
  ssz_ob_t  transactions     = ssz_get(&receipts_proof, "transactions");
  ssz_ob_t  receipts         = ssz_get(&receipts_proof, "receipts");
  ssz_ob_t  block_proof      = ssz_get(&receipts_proof, "block_proof");
  ssz_ob_t  header           = ssz_get(&receipts_proof, "header");
  ssz_ob_t  block_hash       = ssz_get(&receipts_proof, "blockHash");
  ssz_ob_t  block_number     = ssz_get(&receipts_proof, "blockNumber");
  ssz_ob_t  base_fee_per_gas = ssz_get(&receipts_proof, "baseFeePerGas");
  ssz_ob_t  body_root        = ssz_get(&header, "bodyRoot");
  uint32_t  num_receipts     = ssz_len(receipts);
  uint32_t  num_txs          = ssz_len(transactions);
  bytes32_t receipt_root     = {0};
  bytes32_t tx_root          = {0};

  if (num_receipts != num_txs)
    RETURN_VERIFY_ERROR(ctx, "receipt count does not match transaction count!");

  // build the receipt Patricia trie from all serialized receipts and compute the root
  node_t*   trie_root = NULL;
  bytes32_t tmp       = {0};
  buffer_t  buf       = stack_buffer(tmp);
  for (uint32_t i = 0; i < num_receipts; i++)
    patricia_set_value(&trie_root, c4_eth_create_tx_path(i, &buf), ssz_at(receipts, i).bytes);

  if (trie_root) {
    memcpy(receipt_root, patricia_get_root(trie_root).data, 32);
    patricia_node_free(trie_root);
  }
  else
    memcpy(receipt_root, EMPTY_ROOT_HASH, 32);

  // compute the SSZ hash_tree_root of the transactions list
  ssz_hash_tree_root(transactions, tx_root);

  // verify multi-merkle proof: blockNumber, blockHash, receiptsRoot, transactions, baseFeePerGas against bodyRoot
  if (!verify_block_receipts_merkle_proof(ctx, block_proof, block_hash.bytes, block_number.bytes,
                                          receipt_root, tx_root, base_fee_per_gas.bytes, body_root.bytes.data))
    return false;

  // verify beacon header and sync committee signature
  if (c4_verify_header(ctx, header, receipts_proof) != C4_SUCCESS) return false;

  return true;
}

bool verify_block_receipts_proof(verify_ctx_t* ctx) {
  if (ctx->data.def->type != SSZ_TYPE_NONE)
    RETURN_VERIFY_ERROR(ctx, "data must be empty; verifier builds receipt data from RLP!");

  if (!verify_block_receipts_proof_for(ctx, ctx->proof))
    RETURN_VERIFY_ERROR(ctx, "invalid block receipts proof!");

  // build receipt data list from RLP receipts + transactions (reduces payload; no redundant data from prover)
  const ssz_def_t* list_def        = eth_ssz_verification_type(ETH_SSZ_DATA_BLOCK_RECEIPTS);
  ssz_builder_t    data_builder    = ssz_builder_for_def(list_def);
  uint64_t         base_fee        = ssz_get_uint64(&ctx->proof, "baseFeePerGas");
  uint64_t         prev_cumulative = 0;
  uint32_t         next_log_index  = 0;
  ssz_ob_t         receipts        = ssz_get(&ctx->proof, "receipts");
  ssz_ob_t         block_hash      = ssz_get(&ctx->proof, "blockHash");
  ssz_ob_t         transactions    = ssz_get(&ctx->proof, "transactions");
  uint64_t         blk_num         = ssz_get_uint64(&ctx->proof, "blockNumber");
  uint32_t         num_receipts    = ssz_len(receipts);

  for (uint32_t i = 0; i < num_receipts; i++) {
    bytes_t          raw_tx       = ssz_at(transactions, i).bytes;
    bytes_t          raw_receipt  = ssz_at(receipts, i).bytes;
    const ssz_def_t* item_def     = list_def->def.vector.type;
    ssz_builder_t    item_builder = ssz_builder_for_def(item_def);

    if (!c4_write_receipt_data_from_raw(ctx, &item_builder, raw_tx, raw_receipt, block_hash.bytes.data, blk_num, i,
                                        base_fee, &prev_cumulative, &next_log_index))
      RETURN_VERIFY_ERROR(ctx, "invalid receipt data from RLP!");
    ssz_add_dynamic_list_builders(&data_builder, (int) num_receipts, item_builder);
  }
  ctx->data = ssz_builder_to_bytes(&data_builder);
  ctx->flags |= VERIFY_FLAG_FREE_DATA;
  ctx->success = true;
  return true;
}
