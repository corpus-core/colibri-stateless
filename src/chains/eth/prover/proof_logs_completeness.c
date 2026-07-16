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
#include "eth_bloom.h"
#include "eth_compute_units.h"
#include "eth_req.h"
#include "eth_tools.h"
#include "historic_proof.h"
#include "json.h"
#include "logger.h"
#include "ssz.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

// Union selectors for ETH_COMPLETENESS_BLOCK_UNION (see verify_proof_types.h).
#define COMPLETENESS_BLOCK_NONE     0
#define COMPLETENESS_BLOCK_NEGATIVE 1
#define COMPLETENESS_BLOCK_FULL     2

// Union selectors for ETH_STATE_BLOCK_UNION used as the anchor `tag_proof` (see verify_proof_types.h).
#define STATE_BLOCK_UNION_NONE      0
#define STATE_BLOCK_UNION_TIMESTAMP 4

static uint32_t g_max_blocks = C4_LOGS_COMPLETENESS_DEFAULT_MAX_BLOCKS;

void c4_eth_set_logs_completeness_max_blocks(uint32_t max_blocks) {
  g_max_blocks = max_blocks ? max_blocks : C4_LOGS_COMPLETENESS_DEFAULT_MAX_BLOCKS;
}

uint32_t c4_eth_get_logs_completeness_max_blocks(void) {
  return g_max_blocks;
}

typedef struct {
  uint64_t       block_number;
  beacon_block_t beacon;    // header + execution + body + sync_aggregate
  bytes32_t      body_root; // hash_tree_root of the beacon block body
  bool           is_full;   // true: deliver all receipts, false: bloom-negative
  json_t         receipts;  // block receipts (only for full blocks)
} compl_block_t;

// Builds a JSON block identifier ("0x..") for the given block number into buf.
static json_t block_id_json(uint64_t block_number, buffer_t* buf) {
  buffer_reset(buf);
  return json_parse(bprintf(buf, "\"0x%" PRIx64 "\"", block_number));
}

// Serializes one bloom-negative block (blockNumber + logsBloom proven to bodyRoot).
static void serialize_negative_block(prover_ctx_t* ctx, ssz_builder_t* blist, uint32_t count, compl_block_t* block, const ssz_def_t* union_def) {
  ssz_ob_t exec   = block->beacon.execution;
  bytes_t  bloom  = ssz_get(&exec, "logsBloom").bytes;
  gindex_t gindex[2];
  gindex[0] = ssz_gindex(block->beacon.body.def, 2, "executionPayload", "blockNumber");
  gindex[1] = ssz_gindex(block->beacon.body.def, 2, "executionPayload", "logsBloom");

  bytes32_t tmp_root = {0};
  bytes_t   proof    = ssz_create_multi_proof_for_gindexes(block->beacon.body, tmp_root, gindex, 2);
  eth_cu_add_multi_proof(ctx, 2);

  ssz_builder_t v = ssz_builder_for_def(union_def->def.container.elements + COMPLETENESS_BLOCK_NEGATIVE);
  ssz_add_uint64(&v, block->block_number);
  ssz_add_bytes(&v, "logsBloom", bloom);
  ssz_add_bytes(&v, "proof", proof);
  safe_free(proof.data);

  ssz_ob_t vbytes = ssz_builder_to_bytes(&v);
  buffer_t elem   = {0};
  uint8_t  sel    = COMPLETENESS_BLOCK_NEGATIVE;
  buffer_append(&elem, bytes(&sel, 1));
  buffer_append(&elem, vbytes.bytes);
  ssz_add_dynamic_list_bytes(blist, count, elem.data);
  buffer_free(&elem);
  safe_free(vbytes.bytes.data);
}

// Serializes one full-receipts block (all receipts + matched txs + multi proof).
static void serialize_full_block(prover_ctx_t* ctx, ssz_builder_t* blist, uint32_t count, compl_block_t* block, const ssz_def_t* union_def, bytes_t query_blooms) {
  ssz_ob_t exec         = block->beacon.execution;
  ssz_ob_t transactions = ssz_get(&exec, "transactions");
  uint32_t receipt_len  = (uint32_t) json_len(block->receipts);

  // Collect the transaction indices whose receipt could contain a matching log
  // (bloom-positive). These provide the transaction hashes for matched logs.
  uint32_t* match_idx   = safe_calloc(receipt_len ? receipt_len : 1, sizeof(uint32_t));
  uint32_t  match_count = 0;
  uint8_t   bloom_tmp[256];
  buffer_t  bloom_buf = stack_buffer(bloom_tmp);
  json_for_each_value(block->receipts, r) {
    bytes_t rbloom = json_get_bytes(r, "logsBloom", &bloom_buf);
    if (!c4_eth_bloom_negative(query_blooms, rbloom))
      match_idx[match_count++] = json_get_uint32(r, "transactionIndex");
  }

  // multi proof: blockNumber, blockHash, receiptsRoot, matched transactions
  uint32_t  gcount = 3 + match_count;
  gindex_t* gindex = safe_calloc(gcount, sizeof(gindex_t));
  gindex[0]        = ssz_gindex(block->beacon.body.def, 2, "executionPayload", "blockNumber");
  gindex[1]        = ssz_gindex(block->beacon.body.def, 2, "executionPayload", "blockHash");
  gindex[2]        = ssz_gindex(block->beacon.body.def, 2, "executionPayload", "receiptsRoot");
  for (uint32_t i = 0; i < match_count; i++)
    gindex[3 + i] = ssz_gindex(block->beacon.body.def, 3, "executionPayload", "transactions", match_idx[i]);

  bytes32_t tmp_root = {0};
  bytes_t   proof    = ssz_create_multi_proof_for_gindexes(block->beacon.body, tmp_root, gindex, gcount);
  safe_free(gindex);
  eth_cu_add_multi_proof(ctx, gcount);
  eth_cu_add(ctx, receipt_len * CU_PATRICIA_INSERT);

  ssz_builder_t v = ssz_builder_for_def(union_def->def.container.elements + COMPLETENESS_BLOCK_FULL);
  ssz_add_uint64(&v, block->block_number);
  ssz_add_bytes(&v, "blockHash", ssz_get(&exec, "blockHash").bytes);

  // all RLP-serialized receipts
  const ssz_def_t* receipts_def = ssz_get_def(union_def->def.container.elements + COMPLETENESS_BLOCK_FULL, "receipts");
  ssz_builder_t    rlist        = ssz_builder_for_def(receipts_def);
  buffer_t         rbuf         = {0};
  json_for_each_value(block->receipts, r) {
    buffer_reset(&rbuf);
    ssz_add_dynamic_list_bytes(&rlist, receipt_len, c4_serialize_receipt(r, &rbuf));
  }
  buffer_free(&rbuf);
  ssz_add_builders(&v, "receipts", rlist);

  // raw transactions of the matched receipts
  const ssz_def_t* txs_def = ssz_get_def(union_def->def.container.elements + COMPLETENESS_BLOCK_FULL, "txs");
  ssz_builder_t    tx_list = ssz_builder_for_def(txs_def);
  for (uint32_t i = 0; i < match_count; i++) {
    ssz_builder_t tx_ssz = ssz_builder_for_def(txs_def->def.vector.type);
    ssz_add_bytes(&tx_ssz, "transaction", ssz_at(transactions, match_idx[i]).bytes);
    ssz_add_uint32(&tx_ssz, match_idx[i]);
    ssz_add_dynamic_list_builders(&tx_list, match_count, tx_ssz);
  }
  ssz_add_builders(&v, "txs", tx_list);
  ssz_add_bytes(&v, "proof", proof);
  safe_free(proof.data);
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

// True if the block tag is a pinned hex quantity/hash (e.g. "0x...").
static bool tag_is_pinned(json_t tag) {
  return tag.type == JSON_TYPE_STRING && tag.len > 2 && tag.start[1] == '0' && tag.start[2] == 'x';
}

// Builds the anchor `tag_proof` union (+ branch). A pinned block tag needs no freshness proof (`none`),
// `latest` uses the `timestamp` variant proven against the anchor bodyRoot. `safe`/`finalized` would use
// the `checkpoint_proof` variant (structurally prepared, not yet emitted) and are rejected earlier.
static void serialize_tag_proof(prover_ctx_t* ctx, ssz_builder_t* proof, compl_block_t* anchor, json_t to_tag) {
  if (tag_is_pinned(to_tag)) {
    uint8_t none = STATE_BLOCK_UNION_NONE;
    ssz_add_bytes(proof, "tag_proof", bytes(&none, 1));
    ssz_add_bytes(proof, "tag_proof_branch", NULL_BYTES);
    return;
  }

  // `latest`: prove the anchor's execution timestamp against its bodyRoot so the verifier can run the
  // `latest` freshness gate.
  uint8_t tag[9] = {0};
  tag[0]         = STATE_BLOCK_UNION_TIMESTAMP;
  memcpy(tag + 1, ssz_get(&anchor->beacon.execution, "timestamp").bytes.data, 8);
  ssz_add_bytes(proof, "tag_proof", bytes(tag, sizeof(tag)));

  gindex_t  gindex[1] = {ssz_gindex(anchor->beacon.body.def, 2, "executionPayload", "timestamp")};
  bytes32_t tmp_root  = {0};
  bytes_t   branch    = ssz_create_multi_proof_for_gindexes(anchor->beacon.body, tmp_root, gindex, 1);
  eth_cu_add_multi_proof(ctx, 1);
  ssz_add_bytes(proof, "tag_proof_branch", branch);
  safe_free(branch.data);
}

static c4_status_t serialize_completeness_proof(prover_ctx_t* ctx, compl_block_t* blocks, uint32_t count, bytes_t query_blooms, blockroot_proof_t anchor_proof, json_t to_tag) {
  ssz_builder_t    proof       = ssz_builder_for_type(ETH_SSZ_VERIFY_LOGS_COMPLETENESS_PROOF);
  compl_block_t*   first       = &blocks[0];
  compl_block_t*   anchor      = &blocks[count - 1];
  const ssz_def_t* headers_def = ssz_get_def(proof.def, "headers");
  const ssz_def_t* blocks_def  = ssz_get_def(proof.def, "blocks");
  const ssz_def_t* ph_def      = headers_def->def.vector.type;

  // compute the body roots for the whole range
  for (uint32_t i = 0; i < count; i++)
    ssz_hash_tree_root(blocks[i].beacon.body, blocks[i].body_root);

  // full header of the oldest block; parentRoot anchors the ascending chain
  ssz_add_builders(&proof, "header", c4_proof_add_header(first->beacon.header, first->body_root));

  // parentRoot chain: ProofHeaders for the oldest+1 .. anchor (newest)
  ssz_builder_t headers      = ssz_builder_for_def(headers_def);
  uint32_t      header_count = count - 1;
  for (uint32_t i = 1; i < count; i++) {
    ssz_builder_t ph = ssz_builder_for_def(ph_def);
    ssz_add_bytes(&ph, "slot", ssz_get(&blocks[i].beacon.header, "slot").bytes);
    ssz_add_bytes(&ph, "proposerIndex", ssz_get(&blocks[i].beacon.header, "proposerIndex").bytes);
    ssz_add_bytes(&ph, "stateRoot", ssz_get(&blocks[i].beacon.header, "stateRoot").bytes);
    ssz_add_bytes(&ph, "bodyRoot", bytes(blocks[i].body_root, 32));
    ssz_add_dynamic_list_builders(&headers, header_count, ph);
  }
  ssz_add_builders(&proof, "headers", headers);
  eth_cu_add(ctx, header_count * CU_HISTORIC_HEADER_HOP);

  // anchor the newest block via the shared header_proof union (signature / header-chain / historic)
  ssz_add_header_proof(&proof, &anchor->beacon, anchor_proof);

  // anchor block-tag proof (freshness) for an open-ended toBlock -- must be added in field order
  // (after header_proof, before blocks)
  serialize_tag_proof(ctx, &proof, anchor, to_tag);

  // per-block payloads
  ssz_builder_t blist = ssz_builder_for_def(blocks_def);
  for (uint32_t i = 0; i < count; i++) {
    if (blocks[i].is_full)
      serialize_full_block(ctx, &blist, count, &blocks[i], blocks_def->def.vector.type, query_blooms);
    else
      serialize_negative_block(ctx, &blist, count, &blocks[i], blocks_def->def.vector.type);
  }
  ssz_add_builders(&proof, "blocks", blist);

  ctx->proof = eth_create_proof_request(ctx->chain_id, NULL_SSZ_BUILDER, proof, NULL_SSZ_BUILDER);
  return C4_SUCCESS;
}

c4_status_t c4_proof_logs_completeness(prover_ctx_t* ctx) {
  if (ctx->flags & C4_PROVER_FLAG_HYBRID) THROW_ERROR("logs completeness proof is not supported in hybrid mode");
  json_t filter = json_at(ctx->params, 0);
  CHECK_JSON_INPUT(filter, JSON_GET_LOGS_FILTER_FIELDS, "Invalid eth_getLogs filter: ");

  // resolve the range endpoints (fromBlock/toBlock, defaulting to "latest")
  uint8_t  tmp_from[16], tmp_to[16];
  buffer_t bf      = stack_buffer(tmp_from);
  buffer_t bt      = stack_buffer(tmp_to);
  json_t   from_id = json_get(filter, "fromBlock");
  json_t   to_id   = json_get(filter, "toBlock");
  if (from_id.type == JSON_TYPE_NOT_FOUND) from_id = json_parse(bprintf(&bf, "\"latest\""));
  if (to_id.type == JSON_TYPE_NOT_FOUND) to_id = json_parse(bprintf(&bt, "\"latest\""));

  // `safe`/`finalized` require binding the anchor to the beacon checkpoint (checkpoint_proof variant),
  // which is not implemented yet. Reject up front instead of emitting a proof the verifier would reject.
  if (!tag_is_pinned(to_id) && !json_str_eq(to_id, "latest"))
    THROW_ERROR("eth_getLogs completeness currently supports only a pinned toBlock or 'latest'");

  beacon_block_t from_block = {0};
  beacon_block_t to_block   = {0};
  c4_status_t    status     = C4_SUCCESS;
  TRY_ADD_ASYNC(status, c4_beacon_get_block_for_eth(ctx, to_id, &to_block));
  TRY_ADD_ASYNC(status, c4_beacon_get_block_for_eth(ctx, from_id, &from_block));
  TRY_ASYNC(status);

  uint64_t from_num = ssz_get_uint64(&from_block.execution, "blockNumber");
  uint64_t to_num   = ssz_get_uint64(&to_block.execution, "blockNumber");
  if (from_num > to_num) THROW_ERROR("fromBlock is greater than toBlock");
  uint64_t count = to_num - from_num + 1;
  if (count > g_max_blocks) THROW_ERROR("completeness range exceeds the configured maximum number of blocks");

  // fetch the beacon blocks for the whole range
  compl_block_t* blocks = safe_calloc((size_t) count, sizeof(compl_block_t));
  uint8_t        idbuf[16];
  buffer_t       ib = stack_buffer(idbuf);
  status            = C4_SUCCESS;
  for (uint64_t i = 0; i < count; i++) {
    blocks[i].block_number = from_num + i;
    TRY_ADD_ASYNC(status, c4_beacon_get_block_for_eth(ctx, block_id_json(from_num + i, &ib), &blocks[i].beacon));
  }
  if (status != C4_SUCCESS) {
    safe_free(blocks);
    return status;
  }

  // decide the per-block scenario and fetch receipts for full blocks
  bytes_t query_blooms = c4_eth_filter_query_blooms(filter);
  status               = C4_SUCCESS;
  for (uint64_t i = 0; i < count; i++) {
    bytes_t bloom      = ssz_get(&blocks[i].beacon.execution, "logsBloom").bytes;
    blocks[i].is_full  = !c4_eth_bloom_negative(query_blooms, bloom);
    if (blocks[i].is_full)
      TRY_ADD_ASYNC(status, eth_getBlockReceipts(ctx, block_id_json(blocks[i].block_number, &ib), &blocks[i].receipts));
  }
  if (status != C4_SUCCESS) {
    safe_free(blocks);
    safe_free(query_blooms.data);
    return status;
  }

  // single header proof for the whole range: anchor the newest block via the shared header_proof
  // union (direct signature, header-chain or historic summaries, depending on how recent it is).
  blockroot_proof_t anchor_proof = {0};
  status                         = c4_check_blockroot_proof(ctx, &anchor_proof, &blocks[count - 1].beacon);
  if (status != C4_SUCCESS) {
    c4_free_block_proof(&anchor_proof);
    safe_free(blocks);
    safe_free(query_blooms.data);
    return status;
  }

  status = serialize_completeness_proof(ctx, blocks, (uint32_t) count, query_blooms, anchor_proof, to_id);
  c4_free_block_proof(&anchor_proof);
  safe_free(blocks);
  safe_free(query_blooms.data);
  return status;
}
