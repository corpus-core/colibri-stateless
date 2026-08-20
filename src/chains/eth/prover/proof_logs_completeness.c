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

#include "proof_logs_completeness.h"
#include "beacon.h"
#include "beacon_types.h"
#include "el_header.h"
#include "eth_bloom.h"
#include "eth_compute_units.h"
#include "eth_req.h"
#include "eth_tools.h"
#include "eth_tx.h"
#include "historic_proof.h"
#include "json.h"
#include "patricia.h"
#include "ssz.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

// Union selectors for ETH_COMPLETENESS_BLOCK_UNION (see verify_proof_types.h).
#define COMPLETENESS_BLOCK_NONE 0
#define COMPLETENESS_BLOCK_FULL 1

static uint32_t g_max_blocks = C4_LOGS_COMPLETENESS_DEFAULT_MAX_BLOCKS;

void c4_eth_set_logs_completeness_max_blocks(uint32_t max_blocks) {
  g_max_blocks = max_blocks ? max_blocks : C4_LOGS_COMPLETENESS_DEFAULT_MAX_BLOCKS;
}

uint32_t c4_eth_get_logs_completeness_max_blocks(void) {
  return g_max_blocks;
}

typedef struct {
  uint64_t       block_number;
  beacon_block_t beacon;   // el_header always set; el_body set for full blocks
  bool           is_full;  // true: deliver all receipts, false: bloom-negative
  json_t         receipts; // block receipts (only for full blocks)
} compl_block_t;

// Builds a JSON block identifier ("0x..") for the given block number into buf.
static json_t block_id_json(uint64_t block_number, buffer_t* buf) {
  buffer_reset(buf);
  return json_parse(bprintf(buf, "\"0x%lx\"", block_number));
}

// Serializes one bloom-negative block: selector 0 (NONE). The EL header already
// carries logsBloom, so no extra payload is needed.
static void serialize_negative_block(ssz_builder_t* blist, uint32_t count) {
  uint8_t sel = COMPLETENESS_BLOCK_NONE;
  ssz_add_dynamic_list_bytes(blist, count, bytes(&sel, 1));
}

// Serializes one full-receipts block (all receipts + Patricia proofs of matching txs).
static void serialize_full_block(prover_ctx_t* ctx, ssz_builder_t* blist, uint32_t count, compl_block_t* block, const ssz_def_t* union_def, bytes_t query_blooms) {
  ssz_ob_t transactions = ssz_get(&block->beacon.el_body, "transactions");
  //  uint32_t receipt_len  = (uint32_t) json_len(block->receipts);
  uint32_t tx_count = ssz_len(transactions);

  // Collect the transaction indices whose receipt could contain a matching log
  // (bloom-positive). These provide the transaction hashes for matched logs.
  uint32_t* match_idx   = safe_calloc(tx_count, sizeof(uint32_t));
  uint32_t  match_count = 0;
  uint8_t   bloom_tmp[256];
  buffer_t  bloom_buf = stack_buffer(bloom_tmp);
  json_for_each_value(block->receipts, r) {
    bytes_t rbloom = json_get_bytes(r, "logsBloom", &bloom_buf);
    if (!c4_eth_bloom_negative(query_blooms, rbloom)) {
      uint32_t idx = json_get_uint32(r, "transactionIndex");
      if (idx < tx_count)
        match_idx[match_count++] = idx;
    }
  }

  // Build the transactions Patricia trie once, then extract a proof per match.
  node_t*   tx_root  = NULL;
  bytes32_t path_tmp = {0};
  buffer_t  path_buf = stack_buffer(path_tmp);
  for (uint32_t i = 0; i < tx_count; i++)
    patricia_set_value(&tx_root, c4_eth_create_tx_path(i, &path_buf), ssz_at(transactions, i).bytes);

  eth_cu_add_patricia(ctx, tx_count, match_count);

  ssz_builder_t    v            = ssz_builder_for_def(union_def->def.container.elements + COMPLETENESS_BLOCK_FULL);
  const ssz_def_t* receipts_def = ssz_get_def(union_def->def.container.elements + COMPLETENESS_BLOCK_FULL, "receipts");
  ssz_builder_t    rlist        = ssz_builder_for_def(receipts_def);
  buffer_t         rbuf         = {0};
  json_for_each_value(block->receipts, r) {
    buffer_reset(&rbuf);
    ssz_add_dynamic_list_bytes(&rlist, tx_count, c4_serialize_receipt(r, &rbuf));
  }
  buffer_free(&rbuf);
  ssz_add_builders(&v, "receipts", rlist);

  uint8_t*      tx_idxs    = safe_malloc(match_count * 4);
  mpt_builder_t tx_builder = {0};
  mpt_builder_init(&tx_builder, tx_root);

  for (uint32_t i = 0; i < match_count; i++) {
    uint32_to_le(tx_idxs + 4 * i, match_idx[i]);
    mpt_builder_add_proof(&tx_builder, c4_eth_create_tx_path(match_idx[i], &path_buf));
  }
  ssz_ob_t tx_proof = mpt_builder_finish(&tx_builder);
  ssz_add_bytes(&v, "transactionProof", tx_proof.bytes);
  ssz_add_bytes(&v, "txs", bytes(tx_idxs, match_count * 4));

  // clean up
  safe_free(tx_proof.bytes.data);
  safe_free(tx_idxs);
  patricia_node_free(tx_root);
  safe_free(match_idx);

  ssz_ob_t vbytes = ssz_builder_to_bytes(&v);
  buffer_t elem   = {0};
  uint8_t  sel    = COMPLETENESS_BLOCK_FULL;
  buffer_append(&elem, bytes(&sel, 1));
  buffer_append(&elem, vbytes.bytes);
  ssz_add_dynamic_list_bytes(blist, count, elem.data);
  buffer_free(&elem);
  safe_free(vbytes.bytes.data);
}

// True if the JSON string token `t` equals the C string `s` (token includes the surrounding quotes).
static bool json_str_eq(json_t t, const char* s) {
  size_t n = strlen(s);
  return t.type == JSON_TYPE_STRING && (size_t) t.len == n + 2 && strncmp(t.start + 1, s, n) == 0;
}

// True if both tokens are the same JSON value (same type and raw spelling).
static bool json_token_eq(json_t a, json_t b) {
  return a.type == b.type && a.len == b.len && a.start && b.start && memcmp(a.start, b.start, a.len) == 0;
}

// True if the block tag is a pinned hex quantity/hash (e.g. "0x...").
static bool tag_is_pinned(json_t tag) {
  return tag.type == JSON_TYPE_STRING && tag.len > 2 && tag.start[1] == '0' && tag.start[2] == 'x';
}

static c4_status_t serialize_completeness_proof(prover_ctx_t* ctx, compl_block_t* blocks, uint32_t count, bytes_t query_blooms, blockroot_proof_t anchor_proof) {
  ssz_builder_t    proof       = ssz_builder_for_type(ETH_SSZ_VERIFY_LOGS_COMPLETENESS_PROOF);
  compl_block_t*   anchor      = &blocks[count - 1];
  ssz_builder_t    sync_proof  = NULL_SSZ_BUILDER;
  const ssz_def_t* headers_def = ssz_get_def(proof.def, "headers");
  const ssz_def_t* blocks_def  = ssz_get_def(proof.def, "blocks");

  // get the sync_proof if needed
  TRY_ASYNC(c4_get_syncdata_proof(ctx, &anchor_proof.sync, &sync_proof));

  // newest block via the shared ETH_BLOCK_PROOF_UNION (same as logs / tx / receipt)
  eth_add_block_proof(ctx, &proof, &anchor->beacon, &anchor_proof);

  // parentHash chain: raw RLP headers for fromBlock .. toBlock-1 (ascending)
  ssz_builder_t headers      = ssz_builder_for_def(headers_def);
  uint32_t      header_count = count - 1;
  for (uint32_t i = 0; i < header_count; i++)
    ssz_add_dynamic_list_bytes(&headers, header_count, blocks[i].beacon.el_header);
  ssz_add_builders(&proof, "headers", headers);
  eth_cu_add(ctx, header_count * CU_HISTORIC_HEADER_HOP);

  // per-block payloads (NONE or FullReceipts), aligned with fromBlock..toBlock
  ssz_builder_t blist = ssz_builder_for_def(blocks_def);
  for (uint32_t i = 0; i < count; i++) {
    if (blocks[i].is_full)
      serialize_full_block(ctx, &blist, count, &blocks[i], blocks_def->def.vector.type, query_blooms);
    else
      serialize_negative_block(&blist, count);
  }
  ssz_add_builders(&proof, "blocks", blist);

  ctx->proof = eth_create_proof_request(ctx->chain_id, NULL_SSZ_BUILDER, proof, sync_proof);
  return C4_SUCCESS;
}

c4_status_t c4_proof_logs_completeness(prover_ctx_t* ctx) {
  beacon_block_t from_block = {0};
  beacon_block_t to_block   = {0};
  c4_status_t    status     = C4_SUCCESS;
  json_t         filter     = json_at(ctx->params, 0);

  CHECK_JSON_INPUT(filter, JSON_GET_LOGS_FILTER_FIELDS, "Invalid eth_getLogs filter: ");

  // resolve the range endpoints (fromBlock/toBlock, defaulting to "latest").
  // json_parse keeps a pointer into the source string, so the string literal
  // (static storage duration) is safe here and no temporary buffer is needed.
  json_t from_id = json_get(filter, "fromBlock");
  json_t to_id   = json_get(filter, "toBlock");
  if (from_id.type == JSON_TYPE_NOT_FOUND) from_id = json_parse("\"latest\"");
  if (to_id.type == JSON_TYPE_NOT_FOUND) to_id = json_parse("\"latest\"");

  // `safe`/`finalized` require binding the anchor to the beacon checkpoint, which is
  // not implemented yet. Reject up front instead of emitting a proof the verifier would reject.
  if (!tag_is_pinned(to_id) && !json_str_eq(to_id, "latest"))
    THROW_ERROR("eth_getLogs completeness currently supports only a pinned toBlock or 'latest'");

  TRY_ADD_ASYNC(status, c4_beacon_get_block_for_eth(ctx, to_id, &to_block));
  // Identical endpoints would enqueue the same beacon request twice while the first
  // is still pending. After SUCCESS the second call is a guaranteed cache hit.
  if (!json_token_eq(from_id, to_id) || status == C4_SUCCESS)
    TRY_ADD_ASYNC(status, c4_beacon_get_block_for_eth(ctx, from_id, &from_block));
  TRY_ASYNC(status);

  if (!from_block.el_header.data || !to_block.el_header.data)
    THROW_ERROR("missing execution header for completeness range endpoints");

  uint64_t from_num = eth_el_header_get_uint64(from_block.el_header, EL_BLOCK_NUMBER);
  uint64_t to_num   = eth_el_header_get_uint64(to_block.el_header, EL_BLOCK_NUMBER);
  if (from_num > to_num) THROW_ERROR("fromBlock is greater than toBlock");
  uint64_t count = to_num - from_num + 1;
  if (count > g_max_blocks) THROW_ERROR("completeness range exceeds the configured maximum number of blocks");

  // fetch the execution blocks for the whole range (header is enough to decide bloom)
  compl_block_t* blocks = safe_calloc((size_t) count, sizeof(compl_block_t));
  uint8_t        idbuf[24];
  buffer_t       ib = stack_buffer(idbuf);
  status            = C4_SUCCESS;
  for (uint64_t i = 0; i < count; i++) {
    blocks[i].block_number = from_num + i;
    TRY_ADD_ASYNC(status, c4_beacon_get_block_for_eth(ctx, block_id_json(from_num + i, &ib), &blocks[i].beacon));
  }
  TRY_ASYNC_CATCH(status, safe_free(blocks););

  // decide the per-block scenario and fetch receipts + body for full blocks
  bytes_t query_blooms = c4_eth_filter_query_blooms(filter);
  status               = C4_SUCCESS;
  for (uint64_t i = 0; i < count; i++) {
    bytes_t bloom = eth_el_header_get(blocks[i].beacon.el_header, EL_LOGS_BLOOM);
    if (bloom.len != 256) {
      safe_free(blocks);
      safe_free(query_blooms.data);
      THROW_ERROR("invalid logsBloom in execution header");
    }
    blocks[i].is_full = !c4_eth_bloom_negative(query_blooms, bloom);
    if (blocks[i].is_full) {
      TRY_ADD_ASYNC(status, c4_beacon_get_block_for_eth_with_body(ctx, block_id_json(blocks[i].block_number, &ib), &blocks[i].beacon));
      TRY_ADD_ASYNC(status, eth_getBlockReceipts(ctx, block_id_json(blocks[i].block_number, &ib), &blocks[i].receipts));
    }
  }

  TRY_ASYNC_CATCH(status, safe_free(blocks); safe_free(query_blooms.data););

  for (uint64_t i = 0; i < count; i++) {
    if (!blocks[i].beacon.el_header.data || !blocks[i].beacon.el_header.len) {
      safe_free(blocks);
      safe_free(query_blooms.data);
      THROW_ERROR("missing execution header for completeness range");
    }
    if (blocks[i].is_full && (!blocks[i].beacon.el_body.def || !ssz_get_def(blocks[i].beacon.el_body.def, "transactions"))) {
      safe_free(blocks);
      safe_free(query_blooms.data);
      THROW_ERROR("missing execution body for completeness full-receipts block");
    }
  }

  // single block proof for the whole range: anchor the newest block
  blockroot_proof_t anchor_proof = {0};
  TRY_ASYNC_CATCH(c4_check_blockroot_proof(ctx, &anchor_proof, &blocks[count - 1].beacon), safe_free(blocks); safe_free(query_blooms.data););

  REQUEST_WORKER_THREAD_CATCH(ctx, c4_free_block_proof(&anchor_proof); safe_free(blocks); safe_free(query_blooms.data););

  status = serialize_completeness_proof(ctx, blocks, (uint32_t) count, query_blooms, anchor_proof);
  c4_free_block_proof(&anchor_proof);
  safe_free(blocks);
  safe_free(query_blooms.data);
  return status;
}
