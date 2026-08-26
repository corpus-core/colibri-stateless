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
#include "el_header.h"
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

static bool verify_mpt_root(verify_ctx_t* ctx, ssz_ob_t values, bytes32_t expected_root) {
  // build the receipt Patricia trie from all serialized receipts and compute the root
  uint32_t  count     = ssz_len(values);
  node_t*   trie_root = NULL;
  bytes32_t tmp       = {0};
  buffer_t  buf       = stack_buffer(tmp);
  for (uint32_t i = 0; i < count; i++)
    patricia_set_value(&trie_root, c4_eth_create_tx_path(i, &buf), ssz_at(values, i).bytes);

  if (trie_root) {
    memcpy(tmp, patricia_get_root(trie_root).data, 32);
    patricia_node_free(trie_root);
  }
  else
    memcpy(tmp, EMPTY_ROOT_HASH, 32);

  if (memcmp(tmp, expected_root, 32) != 0)
    RETURN_VERIFY_ERROR(ctx, "invalid MPT root, mismatch!");
  return true;
}

bool verify_block_receipts_proof_for(verify_ctx_t* ctx, ssz_ob_t receipts_proof, bytes_t* el_header, bytes32_t block_hash) {

  // verify the execution block
  if (c4_verify_block(ctx, ssz_get(&receipts_proof, "block"), el_header, block_hash) != C4_SUCCESS) return false;
  if (!verify_mpt_root(ctx, ssz_get(&receipts_proof, "transactions"), eth_el_header_get(*el_header, EL_TRANSACTIONS_ROOT).data)) return false;
  if (!verify_mpt_root(ctx, ssz_get(&receipts_proof, "receipts"), eth_el_header_get(*el_header, EL_RECEIPTS_ROOT).data)) return false;

  return true;
}

bool verify_block_receipts_proof(verify_ctx_t* ctx) {
  bytes_t   el_header;
  bytes32_t block_hash;
  if (ctx->data.def->type != SSZ_TYPE_NONE) RETURN_VERIFY_ERROR(ctx, "data must be empty; verifier builds receipt data from RLP!");
  if (!verify_block_receipts_proof_for(ctx, ctx->proof, &el_header, block_hash)) RETURN_VERIFY_ERROR(ctx, "invalid block receipts proof!");

  // write data for the block receipts
  ssz_builder_t data_builder    = ssz_builder_for_type(ETH_SSZ_DATA_BLOCK_RECEIPTS);
  uint64_t      base_fee        = eth_el_header_get_uint64(el_header, EL_BASE_FEE_PER_GAS);
  uint64_t      prev_cumulative = 0;
  uint32_t      next_log_index  = 0;
  ssz_ob_t      receipts        = ssz_get(&ctx->proof, "receipts");
  ssz_ob_t      transactions    = ssz_get(&ctx->proof, "transactions");
  uint64_t      blk_num         = eth_el_header_get_uint64(el_header, EL_BLOCK_NUMBER);
  uint32_t      num_receipts    = ssz_len(receipts);

  for (uint32_t i = 0; i < num_receipts; i++) {
    bytes_t       raw_tx       = ssz_at(transactions, i).bytes;
    bytes_t       raw_receipt  = ssz_at(receipts, i).bytes;
    ssz_builder_t item_builder = ssz_builder_for_def(data_builder.def->def.vector.type);

    if (!c4_write_receipt_data_from_raw(ctx, &item_builder, raw_tx, raw_receipt, block_hash, blk_num, i,
                                        base_fee, &prev_cumulative, &next_log_index)) {
      ssz_builder_free(&data_builder);
      ssz_builder_free(&item_builder);
      RETURN_VERIFY_ERROR(ctx, "invalid receipt data from RLP!");
    }
    ssz_add_dynamic_list_builders(&data_builder, (int) num_receipts, item_builder);
  }
  ctx->data = ssz_builder_to_bytes(&data_builder);
  ctx->flags |= VERIFY_FLAG_FREE_DATA;
  ctx->success = true;
  return true;
}
