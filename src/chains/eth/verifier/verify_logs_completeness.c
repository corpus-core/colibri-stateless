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
#include "eth_bloom.h"
#include "eth_tx.h"
#include "eth_verify.h"
#include "json.h"
#include "patricia.h"
#include "rlp.h"
#include "ssz.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Hard upper bound on the number of blocks a single completeness proof may span.
// Progressive lists have no SSZ capacity, so these DoS guards are the only cap
// before the (untrusted) range is otherwise validated.
#define VERIFY_LOGS_COMPLETENESS_MAX_BLOCKS   4096
#define VERIFY_LOGS_COMPLETENESS_MAX_RECEIPTS 65536
#define VERIFY_LOGS_COMPLETENESS_MAX_HEADER   2048

// Walks the parentHash chain from the verified anchor back to fromBlock.
// `headers` is ascending fromBlock .. toBlock-1. On success `el_headers[0..count-1]`
// and `hashes[0..count-1]` are filled (borrowed views into the proof / anchor).
static bool reconstruct_el_chain(verify_ctx_t* ctx, ssz_ob_t headers, uint32_t count,
                                 bytes_t anchor_hdr, bytes32_t anchor_hash,
                                 bytes_t* el_headers, bytes32_t* hashes) {
  uint32_t  header_count = ssz_len(headers);
  bytes32_t anchor_got   = {0};
  if (header_count != count - 1) RETURN_VERIFY_ERROR(ctx, "completeness proof header chain length mismatch!");
  if (!anchor_hdr.data || anchor_hdr.len == 0 || anchor_hdr.len > VERIFY_LOGS_COMPLETENESS_MAX_HEADER)
    RETURN_VERIFY_ERROR(ctx, "invalid anchor execution header!");
  keccak(anchor_hdr, anchor_got);
  if (memcmp(anchor_got, anchor_hash, 32) != 0)
    RETURN_VERIFY_ERROR(ctx, "anchor header hash mismatch!");

  el_headers[count - 1] = anchor_hdr;
  memcpy(hashes[count - 1], anchor_hash, 32);

  for (uint32_t i = count; i-- > 1;) {
    bytes_t   older  = ssz_at(headers, i - 1).bytes;
    bytes32_t h      = {0};
    bytes_t   parent = eth_el_header_get(el_headers[i], EL_PARENT_HASH);
    if (!older.data || !older.len) RETURN_VERIFY_ERROR(ctx, "missing execution header in completeness chain!");
    if (older.len > VERIFY_LOGS_COMPLETENESS_MAX_HEADER) RETURN_VERIFY_ERROR(ctx, "execution header too large!");
    keccak(older, h);
    if (parent.len != 32 || memcmp(h, parent.data, 32) != 0)
      RETURN_VERIFY_ERROR(ctx, "completeness parentHash mismatch!");
    uint64_t newer_num = eth_el_header_get_uint64(el_headers[i], EL_BLOCK_NUMBER);
    uint64_t older_num = eth_el_header_get_uint64(older, EL_BLOCK_NUMBER);
    if (older_num + 1 != newer_num)
      RETURN_VERIFY_ERROR(ctx, "completeness blockNumber sequence has a gap!");
    el_headers[i - 1] = older;
    memcpy(hashes[i - 1], h, 32);
  }
  return true;
}

static bool verify_negative_block(verify_ctx_t* ctx, bytes_t el_header, bytes_t query_blooms) {
  bytes_t logs_bloom = eth_el_header_get(el_header, EL_LOGS_BLOOM);
  if (logs_bloom.len != 256) RETURN_VERIFY_ERROR(ctx, "invalid logsBloom in completeness proof!");
  // Independently re-check the negativity claim against the proven logsBloom.
  if (!c4_eth_bloom_negative(query_blooms, logs_bloom))
    RETURN_VERIFY_ERROR(ctx, "block claimed bloom-negative but query bloom is a subset of logsBloom!");
  return true;
}

// True if `tx_index` is present in the block's matched-tx index list.
static bool has_tx(ssz_ob_t txs, uint32_t tx_index) {
  uint32_t n = ssz_len(txs);
  for (uint32_t i = 0; i < n; i++) {
    if (ssz_uint32(ssz_at(txs, i)) == tx_index) return true;
  }
  return false;
}

static bool receipt_get_bloom(verify_ctx_t* ctx, bytes_t receipt, bytes_t* bloom, int* num_logs, bytes_t* logs_rlp) {
  if (receipt.len && receipt.data[0] < 0x80) { // strip the (optional) EIP-2718 type byte
    receipt.data++;
    receipt.len--;
  }
  if (rlp_decode(&receipt, 0, &receipt) != RLP_LIST) RETURN_VERIFY_ERROR(ctx, "invalid receipt encoding in completeness proof!");
  if (rlp_decode(&receipt, 2, bloom) != RLP_ITEM || bloom->len != 256) RETURN_VERIFY_ERROR(ctx, "invalid receipt logsBloom!");
  if (rlp_decode(&receipt, 3, logs_rlp) != RLP_LIST) RETURN_VERIFY_ERROR(ctx, "invalid receipt logs!");
  *num_logs = rlp_decode(logs_rlp, -1, logs_rlp);
  if (*num_logs < 0) RETURN_VERIFY_ERROR(ctx, "invalid receipt logs count!");
  return true;
}

// Decodes every log of one receipt and appends it to `data_builder`.
// On failure the current log's builders are freed and ctx already holds the error;
// the caller must free `mpt_proof_t`. Partial appends stay in `data_builder`.
static bool append_receipt_logs(verify_ctx_t* ctx, bytes_t logs_rlp, int num_logs,
                                bytes32_t block_hash, uint64_t blk_num,
                                bytes32_t tx_hash, uint32_t tx_index,
                                uint32_t* block_log_index, ssz_builder_t* data_builder,
                                uint32_t* out_count) {
  const ssz_def_t* log_def = data_builder->def->def.vector.type;

  for (int l = 0; l < num_logs; l++, (*block_log_index)++) {
    bytes_t log_rlp = {0}, addr = {0}, topics_rlp = {0}, data = {0};
    if (rlp_decode(&logs_rlp, l, &log_rlp) != RLP_LIST) RETURN_VERIFY_ERROR(ctx, "invalid log encoding!");
    if (rlp_decode(&log_rlp, 0, &addr) != RLP_ITEM) RETURN_VERIFY_ERROR(ctx, "invalid log address!");
    if (rlp_decode(&log_rlp, 1, &topics_rlp) != RLP_LIST) RETURN_VERIFY_ERROR(ctx, "invalid log topics!");
    if (rlp_decode(&log_rlp, 2, &data) != RLP_ITEM) RETURN_VERIFY_ERROR(ctx, "invalid log data!");
    int num_topics = rlp_decode(&topics_rlp, -1, &topics_rlp);
    if (num_topics < 0) RETURN_VERIFY_ERROR(ctx, "invalid log topics count!");

    ssz_builder_t log_builder = ssz_builder_for_def(log_def);
    ssz_add_bytes(&log_builder, "blockHash", bytes(block_hash, 32));
    ssz_add_uint64(&log_builder, blk_num);
    ssz_add_bytes(&log_builder, "transactionHash", bytes(tx_hash, 32));
    ssz_add_uint32(&log_builder, tx_index);
    uint8_t addr20[20] = {0};
    if (addr.len <= 20) memcpy(addr20 + (20 - addr.len), addr.data, addr.len);
    ssz_add_bytes(&log_builder, "address", bytes(addr20, 20));
    ssz_add_uint32(&log_builder, *block_log_index);
    ssz_add_uint8(&log_builder, 0);
    ssz_builder_t topics_builder = ssz_builder_for_def(ssz_get_def(log_def, "topics"));
    for (int t = 0; t < num_topics; t++) {
      bytes_t topic       = {0};
      uint8_t topic32[32] = {0};
      if (rlp_decode(&topics_rlp, t, &topic) != RLP_ITEM) {
        ssz_builder_free(&topics_builder);
        ssz_builder_free(&log_builder);
        RETURN_VERIFY_ERROR(ctx, "invalid topic!");
      }
      if (topic.len <= 32) memcpy(topic32 + (32 - topic.len), topic.data, topic.len);
      ssz_add_dynamic_list_bytes(&topics_builder, num_topics, bytes(topic32, 32));
    }
    ssz_add_builders(&log_builder, "topics", topics_builder);
    ssz_add_bytes(&log_builder, "data", data);
    ssz_add_dynamic_list_builders(data_builder, 0, log_builder);
    (*out_count)++;
  }
  return true;
}

// Rebuilds the receipts trie, verifies matching txs against transactionsRoot and appends
// the logs of every bloom-positive receipt to `data_builder`. The final exact filtering
// is done once for the whole range in `verify_logs_completeness`. `out_count` is incremented
// per appended log.
static bool verify_full_block(verify_ctx_t* ctx, ssz_ob_t block, bytes_t el_header, bytes32_t block_hash,
                              ssz_builder_t* data_builder, bytes_t query_blooms, uint32_t* out_count) {
  ssz_ob_t receipts     = ssz_get(&block, "receipts");
  ssz_ob_t txs          = ssz_get(&block, "txs");
  uint32_t num_receipts = ssz_len(receipts);
  uint32_t num_txs      = ssz_len(txs);
  uint64_t blk_num      = eth_el_header_get_uint64(el_header, EL_BLOCK_NUMBER);

  if (num_receipts > VERIFY_LOGS_COMPLETENESS_MAX_RECEIPTS)
    RETURN_VERIFY_ERROR(ctx, "too many receipts in completeness full-receipts block!");
  if (num_txs > num_receipts)
    RETURN_VERIFY_ERROR(ctx, "too many txs in completeness full-receipts block!");

  bytes_t expected_receipt_root = eth_el_header_get(el_header, EL_RECEIPTS_ROOT);
  bytes_t expected_tx_root      = eth_el_header_get(el_header, EL_TRANSACTIONS_ROOT);
  if (expected_receipt_root.len != 32) RETURN_VERIFY_ERROR(ctx, "invalid receiptsRoot in execution header!");
  if (expected_tx_root.len != 32) RETURN_VERIFY_ERROR(ctx, "invalid transactionsRoot in execution header!");

  // rebuild the receipts Patricia trie and compute the receiptsRoot
  node_t*   trie_root    = NULL;
  bytes32_t tmp          = {0};
  buffer_t  path_buf     = stack_buffer(tmp);
  bytes32_t receipt_root = {0};
  for (uint32_t i = 0; i < num_receipts; i++)
    patricia_set_value(&trie_root, c4_eth_create_tx_path(i, &path_buf), ssz_at(receipts, i).bytes);
  if (trie_root) {
    memcpy(receipt_root, patricia_get_root(trie_root).data, 32);
    patricia_node_free(trie_root);
  }
  else
    memcpy(receipt_root, EMPTY_ROOT_HASH, 32);

  if (memcmp(receipt_root, expected_receipt_root.data, 32) != 0)
    RETURN_VERIFY_ERROR(ctx, "invalid full-receipts proof, receiptsRoot mismatch!");

  mpt_proof_t tx_proof;
  uint32_t    block_log_index = 0; // block-wide log index across all receipts
  mpt_proof_init(&tx_proof, ssz_get(&block, "transactionProof"), expected_tx_root.data);

  // walk every receipt in tx order; a matching log can only exist in a bloom-positive receipt,
  // so the prover legitimately omits transactions of bloom-negative receipts. Independently
  // re-checking each receipt's (authenticated) logsBloom is what makes the proof complete.
  for (uint32_t r = 0; r < num_receipts; r++) {
    bytes_t receipt  = ssz_at(receipts, r).bytes;
    int     num_logs = 0;
    bytes_t logs_rlp = {0};
    bytes_t bloom    = {0};

    if (!receipt_get_bloom(ctx, receipt, &bloom, &num_logs, &logs_rlp)) {
      mpt_proof_free(&tx_proof);
      return false;
    }

    // bloom-negative receipts cannot contain a matching log -> skip, but keep the log index in sync
    if (c4_eth_bloom_negative(query_blooms, bloom)) {
      block_log_index += (uint32_t) num_logs;
      continue;
    }

    if (!has_tx(txs, r)) {
      mpt_proof_free(&tx_proof);
      RETURN_VERIFY_ERROR(ctx, "bloom-positive receipt without provided transaction (incomplete proof)!");
    }
    bytes_t raw_tx = {0};
    if (patricia_verify_multi(&tx_proof, c4_eth_create_tx_path(r, &path_buf), &raw_tx) != PATRICIA_FOUND) {
      mpt_proof_free(&tx_proof);
      RETURN_VERIFY_ERROR(ctx, "invalid transaction proof in completeness full-receipts block!");
    }
    bytes32_t tx_hash = {0};
    keccak(raw_tx, tx_hash);

    if (!append_receipt_logs(ctx, logs_rlp, num_logs, block_hash, blk_num, tx_hash, r,
                             &block_log_index, data_builder, out_count)) {
      mpt_proof_free(&tx_proof);
      return false;
    }
  }
  mpt_proof_free(&tx_proof);

  return true;
}

// Binds one endpoint of the proven range to the value requested in the client filter. Without this
// binding a malicious prover could shrink the range and omit matching logs while still producing a
// structurally valid proof, defeating the completeness guarantee.
//
// - explicit block numbers ("0x..") must match `proof_value` exactly.
// - "latest"/"safe"/"finalized" (and an absent endpoint, which for `eth_getLogs` defaults to
//   "latest") accept the signature-anchored `anchor_value`.
// - "pending"/"earliest" are not provable.
static bool bind_range_endpoint(verify_ctx_t* ctx, json_t tag, uint64_t proof_value, uint64_t anchor_value) {
  if (tag.type == JSON_TYPE_NOT_FOUND) return proof_value == anchor_value; // default: "latest"
  if (tag.type != JSON_TYPE_STRING) RETURN_VERIFY_ERROR(ctx, "invalid block tag in eth_getLogs filter");
  if (eth_json_is_unproofable_tag(tag)) RETURN_VERIFY_ERROR(ctx, "block tag not provable for completeness");
  if (tag.len > 2 && tag.start[1] == '0' && tag.start[2] == 'x') { // explicit hex quantity
    if (json_as_uint64(tag) != proof_value) RETURN_VERIFY_ERROR(ctx, "completeness range does not match requested block range");
    return true;
  }
  return proof_value == anchor_value; // "latest"/"safe"/"finalized"
}

bool verify_logs_completeness(verify_ctx_t* ctx) {
  ssz_ob_t proof   = ctx->proof;
  ssz_ob_t headers = ssz_get(&proof, "headers");
  ssz_ob_t blocks  = ssz_get(&proof, "blocks");

  // The claim is the requested range (from the RPC request); the proof carries no range endpoints.
  // The block count is derived from the (proven) parentHash chain length.
  uint64_t count = (uint64_t) ssz_len(headers) + 1;
  // Hard upper bound before any allocation: progressive lists are not length-capped by SSZ.
  if (count > VERIFY_LOGS_COMPLETENESS_MAX_BLOCKS) RETURN_VERIFY_ERROR(ctx, "completeness range too large!");

  json_t filter = json_at(ctx->args, 0);
  if (filter.type != JSON_TYPE_OBJECT) RETURN_VERIFY_ERROR(ctx, "eth_getLogs completeness requires a filter object!");

  if ((uint64_t) ssz_len(blocks) != count) RETURN_VERIFY_ERROR(ctx, "completeness proof block count mismatch!");

  json_t to_tag    = json_get(filter, "toBlock");
  bool   is_latest = to_tag.type == JSON_TYPE_NOT_FOUND || eth_json_is_latest(to_tag);
  bool   is_pinned = to_tag.type == JSON_TYPE_STRING && to_tag.len > 2 && to_tag.start[1] == '0' && to_tag.start[2] == 'x';
  if (!is_latest && !is_pinned)
    RETURN_VERIFY_ERROR(ctx, "toBlock tag not yet supported for completeness (only a pinned block or 'latest')");

  bytes_t   anchor_hdr  = {0};
  bytes32_t anchor_hash = {0};
  ssz_ob_t  block_proof = ssz_get(&proof, "block");
  if (!block_proof.def) RETURN_VERIFY_ERROR(ctx, "missing block proof in completeness proof!");
  if (c4_verify_block(ctx, block_proof, &anchor_hdr, anchor_hash) != C4_SUCCESS)
    return false;

  bytes_t*   el_headers = safe_calloc((size_t) count, sizeof(bytes_t));
  bytes32_t* hashes     = safe_calloc((size_t) count, sizeof(bytes32_t));
  if (!reconstruct_el_chain(ctx, headers, (uint32_t) count, anchor_hdr, anchor_hash, el_headers, hashes)) {
    safe_free(el_headers);
    safe_free(hashes);
    return false;
  }

  bytes_t          query_blooms = c4_eth_filter_query_blooms(filter);
  const ssz_def_t* logs_def     = eth_ssz_verification_type(ETH_SSZ_DATA_LOGS);
  ssz_builder_t    data_builder = ssz_builder_for_def(logs_def);
  uint32_t         log_count    = 0;
  uint64_t         from_num     = eth_el_header_get_uint64(el_headers[0], EL_BLOCK_NUMBER);
  uint64_t         to_num       = eth_el_header_get_uint64(el_headers[count - 1], EL_BLOCK_NUMBER);
  bool             success      = true;

  for (uint64_t i = 0; i < count; i++) {
    ssz_ob_t union_ob = ssz_at(blocks, (uint32_t) i);
    ssz_ob_t block    = ssz_union(union_ob);
    if (!block.def) {
      success = false;
      break;
    }

    uint64_t bn = eth_el_header_get_uint64(el_headers[i], EL_BLOCK_NUMBER);
    if (bn != from_num + i) {
      success = false;
      break;
    }

    if (block.def->type == SSZ_TYPE_NONE) {
      if (!verify_negative_block(ctx, el_headers[i], query_blooms)) {
        success = false;
        break;
      }
    }
    else if (strcmp(block.def->name, "FullReceipts") == 0) {
      if (!verify_full_block(ctx, block, el_headers[i], hashes[i], &data_builder, query_blooms, &log_count)) {
        success = false;
        break;
      }
    }
    else {
      success = false;
      break;
    }
  }

  safe_free(el_headers);
  safe_free(hashes);
  safe_free(query_blooms.data);

  // Bind the proven range (now cryptographically established via the parentHash chain) to
  // the range the client actually requested (see bind_range_endpoint).
  if (success &&
      (!bind_range_endpoint(ctx, json_get(filter, "fromBlock"), from_num, to_num) ||
       !bind_range_endpoint(ctx, json_get(filter, "toBlock"), to_num, to_num)))
    success = false;

  // Freshness gate for an open-ended `toBlock` (absent -> defaults to "latest"): the timestamp
  // is a field of the verified anchor EL header.
  if (success && !eth_check_latest_freshness(ctx, is_latest, true, eth_el_header_get_uint64(anchor_hdr, EL_TIMESTAMP)))
    success = false;

  if (!success) {
    safe_free(data_builder.fixed.data.data);
    safe_free(data_builder.dynamic.data.data);
    if (!ctx->state.error) ctx->state.error = strdup("invalid logs completeness proof!");
    return false;
  }

  ssz_builder_fix_list_offsets(&data_builder, log_count);
  ssz_ob_t all_logs = ssz_builder_to_bytes(&data_builder);

  // Exact final filtering: plain filters drop bloom false-positives; PAP filters (bloomFilter
  // only, no address/topics) keep the bloom-matching superset for the client to filter.
  ctx->data = c4_eth_filter_logs(all_logs, filter);
  safe_free(all_logs.bytes.data);
  ctx->flags |= VERIFY_FLAG_FREE_DATA;
  ctx->success = true;
  return true;
}
