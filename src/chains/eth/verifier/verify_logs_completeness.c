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
#include "eth_bloom.h"
#include "eth_tx.h"
#include "eth_verify.h"
#include "json.h"
#include "patricia.h"
#include "rlp.h"
#include "ssz.h"
#include "sync_committee.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Hard upper bound on the number of blocks a single completeness proof may span. This matches the
// SSZ list capacity (4096) and caps allocation before the (untrusted) range is otherwise validated.
#define VERIFY_LOGS_COMPLETENESS_MAX_BLOCKS 4096

// Verifies the parent_root header chain and the anchor's sync committee signature.
// Fills body_roots[0..count-1] with the bodyRoot of each in-range block (ascending).
static c4_status_t verify_chain(verify_ctx_t* ctx, ssz_ob_t header, ssz_ob_t headers, ssz_ob_t bits, ssz_ob_t sig, bytes32_t* body_roots, uint32_t count) {
  uint32_t  header_count      = ssz_len(headers);
  uint8_t   header_bytes[112] = {0};
  bytes32_t last_block_root   = {0};
  ssz_ob_t  header_ob         = {.bytes = bytes(header_bytes, sizeof(header_bytes)), .def = eth_ssz_type_for_denep(ETH_SSZ_BEACON_BLOCK_HEADER, C4_CHAIN_MAINNET)};

  if (header_count != count - 1) THROW_ERROR("completeness proof header chain length mismatch!");

  // bodyRoot of fromBlock comes from the full header
  memcpy(body_roots[0], ssz_get(&header, "bodyRoot").bytes.data, 32);
  ssz_hash_tree_root(header, last_block_root);

  // walk the parent_root chain from fromBlock+1 up to the anchor (toBlock)
  for (uint32_t i = 0; i < header_count; i++) {
    ssz_ob_t h = ssz_at(headers, i);
    memcpy(header_bytes, h.bytes.data, 16);           // slot and proposerIndex
    memcpy(header_bytes + 16, last_block_root, 32);   // parentRoot = previous block root
    memcpy(header_bytes + 48, h.bytes.data + 16, 64); // stateRoot and bodyRoot
    ssz_hash_tree_root(header_ob, last_block_root);
    memcpy(body_roots[i + 1], h.bytes.data + 48, 32); // bodyRoot of this block
  }

  // verify the sync committee signature over the anchor (last header, or the full header for a single-block range)
  ssz_ob_t anchor = header_count ? header_ob : header;
  return c4_verify_blockroot_signature(ctx, &anchor, &bits, &sig, 0, NULL);
}

static bool verify_negative_block(verify_ctx_t* ctx, ssz_ob_t block, bytes32_t body_root, bytes_t query_blooms) {
  bytes_t   block_number = ssz_get(&block, "blockNumber").bytes;
  bytes_t   logs_bloom   = ssz_get(&block, "logsBloom").bytes;
  ssz_ob_t  proof        = ssz_get(&block, "proof");
  uint8_t   leafes[64]   = {0};
  gindex_t  gindexes[2]  = {GINDEX_BLOCKUMBER, GINDEX_LOGS_BLOOM};
  bytes32_t root_hash    = {0};

  if (logs_bloom.len != 256) RETURN_VERIFY_ERROR(ctx, "invalid logsBloom in completeness proof!");
  memcpy(leafes, block_number.data, block_number.len);
  // logsBloom is a 256-byte vector -> its leaf is the hash_tree_root of the vector, not the raw bytes.
  ssz_hash_tree_root(ssz_get(&block, "logsBloom"), leafes + 32);

  if (!ssz_verify_multi_merkle_proof(proof.bytes, bytes(leafes, sizeof(leafes)), gindexes, root_hash))
    RETURN_VERIFY_ERROR(ctx, "invalid bloom-negative proof, missing nodes!");
  if (memcmp(root_hash, body_root, 32) != 0)
    RETURN_VERIFY_ERROR(ctx, "invalid bloom-negative proof, body root mismatch!");

  // Independently re-check the negativity claim against the proven logsBloom.
  if (!c4_eth_bloom_negative(query_blooms, logs_bloom))
    RETURN_VERIFY_ERROR(ctx, "block claimed bloom-negative but query bloom is a subset of logsBloom!");
  return true;
}

// Finds the raw transaction for the given index in the block's matched-tx list.
static bytes_t find_tx_raw(ssz_ob_t txs, uint32_t tx_index) {
  uint32_t n = ssz_len(txs);
  for (uint32_t i = 0; i < n; i++) {
    ssz_ob_t tx = ssz_at(txs, i);
    if (ssz_get_uint64(&tx, "transactionIndex") == tx_index)
      return ssz_get(&tx, "transaction").bytes;
  }
  return NULL_BYTES;
}

// Rebuilds the receipts trie, verifies the multi proof and appends the logs of every
// bloom-positive receipt to `data_builder`. The final exact filtering is done once for the
// whole range in `verify_logs_completeness`. `out_count` is incremented per appended log.
static bool verify_full_block(verify_ctx_t* ctx, ssz_ob_t block, bytes32_t body_root,
                              ssz_builder_t* data_builder, bytes_t query_blooms, uint32_t* out_count) {
  bytes_t  block_number = ssz_get(&block, "blockNumber").bytes;
  bytes_t  block_hash   = ssz_get(&block, "blockHash").bytes;
  ssz_ob_t receipts     = ssz_get(&block, "receipts");
  ssz_ob_t txs          = ssz_get(&block, "txs");
  ssz_ob_t proof        = ssz_get(&block, "proof");
  uint32_t num_receipts = ssz_len(receipts);
  uint32_t num_txs      = ssz_len(txs);
  uint64_t blk_num      = ssz_get_uint64(&block, "blockNumber");

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

  // verify multi proof: blockNumber, blockHash, receiptsRoot, matched transactions -> bodyRoot
  uint8_t*  leafes    = safe_calloc(3 + num_txs, 32);
  gindex_t* gindexes  = safe_calloc(3 + num_txs, sizeof(gindex_t));
  bytes32_t root_hash = {0};
  memcpy(leafes, block_number.data, block_number.len);
  memcpy(leafes + 32, block_hash.data, block_hash.len);
  memcpy(leafes + 64, receipt_root, 32);
  gindexes[0] = GINDEX_BLOCKUMBER;
  gindexes[1] = GINDEX_BLOCHASH;
  gindexes[2] = GINDEX_RECEIPT_ROOT;
  for (uint32_t i = 0; i < num_txs; i++) {
    ssz_ob_t tx = ssz_at(txs, i);
    ssz_hash_tree_root(ssz_ob(ssz_transactions_bytes, ssz_get(&tx, "transaction").bytes), leafes + 96 + 32 * i);
    gindexes[3 + i] = GINDEX_TXINDEX_G + ssz_get_uint64(&tx, "transactionIndex");
  }
  bool ok = ssz_verify_multi_merkle_proof(proof.bytes, bytes(leafes, (3 + num_txs) * 32), gindexes, root_hash);
  safe_free(leafes);
  safe_free(gindexes);
  if (!ok) RETURN_VERIFY_ERROR(ctx, "invalid full-receipts proof, missing nodes!");
  if (memcmp(root_hash, body_root, 32) != 0) RETURN_VERIFY_ERROR(ctx, "invalid full-receipts proof, body root mismatch!");

  const ssz_def_t* log_def         = data_builder->def->def.vector.type;
  uint32_t         block_log_index = 0; // block-wide log index across all receipts

  // walk every receipt in tx order; a matching log can only exist in a bloom-positive receipt,
  // so the prover legitimately omits transactions of bloom-negative receipts. Independently
  // re-checking each receipt's (authenticated) logsBloom is what makes the proof complete.
  for (uint32_t r = 0; r < num_receipts; r++) {
    bytes_t receipt  = ssz_at(receipts, r).bytes;
    bytes_t logs_rlp = {0};
    bytes_t bloom    = {0};

    if (receipt.len && receipt.data[0] < 0x80) { // strip the (optional) EIP-2718 type byte
      receipt.data++;
      receipt.len--;
    }
    if (rlp_decode(&receipt, 0, &receipt) != RLP_LIST) RETURN_VERIFY_ERROR(ctx, "invalid receipt encoding in completeness proof!");
    if (rlp_decode(&receipt, 2, &bloom) != RLP_ITEM || bloom.len != 256) RETURN_VERIFY_ERROR(ctx, "invalid receipt logsBloom!");
    if (rlp_decode(&receipt, 3, &logs_rlp) != RLP_LIST) RETURN_VERIFY_ERROR(ctx, "invalid receipt logs!");
    int num_logs = rlp_decode(&logs_rlp, -1, &logs_rlp);
    if (num_logs < 0) num_logs = 0;

    // bloom-negative receipts cannot contain a matching log -> skip, but keep the log index in sync
    if (c4_eth_bloom_negative(query_blooms, bloom)) {
      block_log_index += (uint32_t) num_logs;
      continue;
    }

    bytes_t raw_tx = find_tx_raw(txs, r);
    if (!raw_tx.data) RETURN_VERIFY_ERROR(ctx, "bloom-positive receipt without provided transaction (incomplete proof)!");
    bytes32_t tx_hash = {0};
    keccak(raw_tx, tx_hash);

    for (int l = 0; l < num_logs; l++, block_log_index++) {
      bytes_t log_rlp = {0}, addr = {0}, topics_rlp = {0}, data = {0};
      if (rlp_decode(&logs_rlp, l, &log_rlp) != RLP_LIST) RETURN_VERIFY_ERROR(ctx, "invalid log encoding!");
      if (rlp_decode(&log_rlp, 0, &addr) != RLP_ITEM) RETURN_VERIFY_ERROR(ctx, "invalid log address!");
      if (rlp_decode(&log_rlp, 1, &topics_rlp) != RLP_LIST) RETURN_VERIFY_ERROR(ctx, "invalid log topics!");
      if (rlp_decode(&log_rlp, 2, &data) != RLP_ITEM) RETURN_VERIFY_ERROR(ctx, "invalid log data!");
      int num_topics = rlp_decode(&topics_rlp, -1, &topics_rlp);

      ssz_builder_t log_builder = ssz_builder_for_def(log_def);
      ssz_add_bytes(&log_builder, "blockHash", block_hash);
      ssz_add_uint64(&log_builder, blk_num);
      ssz_add_bytes(&log_builder, "transactionHash", bytes(tx_hash, 32));
      ssz_add_uint32(&log_builder, r);
      uint8_t addr20[20] = {0};
      if (addr.len <= 20) memcpy(addr20 + (20 - addr.len), addr.data, addr.len);
      ssz_add_bytes(&log_builder, "address", bytes(addr20, 20));
      ssz_add_uint32(&log_builder, block_log_index);
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
  }
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
  uint64_t from    = ssz_get_uint64(&proof, "fromBlock");
  uint64_t to      = ssz_get_uint64(&proof, "toBlock");
  ssz_ob_t header  = ssz_get(&proof, "header");
  ssz_ob_t headers = ssz_get(&proof, "headers");
  ssz_ob_t bits    = ssz_get(&proof, "sync_committee_bits");
  ssz_ob_t sig     = ssz_get(&proof, "sync_committee_signature");
  ssz_ob_t blocks  = ssz_get(&proof, "blocks");

  if (to < from) RETURN_VERIFY_ERROR(ctx, "invalid completeness range (toBlock < fromBlock)!");
  uint64_t count = to - from + 1;
  // Hard upper bound before any allocation: the SSZ list caps at 4096, but lists of dynamic (union)
  // elements are not length-checked by ssz_is_valid, so reject oversized ranges up front (DoS guard).
  if (count > VERIFY_LOGS_COMPLETENESS_MAX_BLOCKS) RETURN_VERIFY_ERROR(ctx, "completeness range too large!");

  // Bind the proven range to the range the client actually asked for (see bind_range_endpoint).
  // Without this a malicious prover could shrink the range and still produce a valid-looking proof.
  json_t filter = json_at(ctx->args, 0);
  if (filter.type != JSON_TYPE_OBJECT) RETURN_VERIFY_ERROR(ctx, "eth_getLogs completeness requires a filter object!");
  if (!bind_range_endpoint(ctx, json_get(filter, "fromBlock"), from, to)) return false;
  if (!bind_range_endpoint(ctx, json_get(filter, "toBlock"), to, to)) return false;

  if ((uint64_t) ssz_len(blocks) != count) RETURN_VERIFY_ERROR(ctx, "completeness proof block count mismatch!");

  bytes32_t*  body_roots = safe_calloc((size_t) count, sizeof(bytes32_t));
  c4_status_t chain      = verify_chain(ctx, header, headers, bits, sig, body_roots, (uint32_t) count);
  if (chain != C4_SUCCESS) {
    // C4_PENDING (need sync committee) and C4_ERROR both propagate via ctx->state.
    safe_free(body_roots);
    return false;
  }

  bytes_t query_blooms = c4_eth_filter_query_blooms(filter);

  const ssz_def_t* logs_def     = eth_ssz_verification_type(ETH_SSZ_DATA_LOGS);
  ssz_builder_t    data_builder = ssz_builder_for_def(logs_def);
  uint32_t         log_count    = 0;
  bool             success      = true;

  for (uint64_t i = 0; i < count; i++) {
    ssz_ob_t union_ob = ssz_at(blocks, (uint32_t) i);
    ssz_ob_t block    = ssz_union(union_ob);
    if (!block.def) { success = false; break; }

    // the gap-free blockNumber sequence together with the parent_root chain rules out skipped blocks
    if (ssz_get_uint64(&block, "blockNumber") != from + i) { success = false; break; }

    if (strcmp(block.def->name, "FullReceipts") == 0) {
      if (!verify_full_block(ctx, block, body_roots[i], &data_builder, query_blooms, &log_count)) { success = false; break; }
    }
    else if (strcmp(block.def->name, "BloomNegative") == 0) {
      if (!verify_negative_block(ctx, block, body_roots[i], query_blooms)) { success = false; break; }
    }
    else {
      success = false;
      break;
    }
  }

  safe_free(body_roots);
  safe_free(query_blooms.data);

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
