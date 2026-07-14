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

// Reconstructs the ascending parentRoot header chain (oldest `header` -> anchor) without verifying
// the anchor's canonicity (that is done separately via the shared `header_proof` union).
// Fills body_roots[0..count-1] with the bodyRoot of each in-range block (ascending) and writes the
// reconstructed anchor (the newest header) into `anchor_bytes`/`*anchor_out`.
static c4_status_t reconstruct_chain(verify_ctx_t* ctx, ssz_ob_t header, ssz_ob_t headers,
                                     bytes32_t* body_roots, uint32_t count,
                                     uint8_t anchor_bytes[112], ssz_ob_t* anchor_out) {
  uint32_t  header_count    = ssz_len(headers);
  bytes32_t last_block_root = {0};
  ssz_ob_t  header_ob       = {.bytes = bytes(anchor_bytes, 112), .def = eth_ssz_type_for_denep(ETH_SSZ_BEACON_BLOCK_HEADER, C4_CHAIN_MAINNET)};

  if (header_count != count - 1) THROW_ERROR("completeness proof header chain length mismatch!");

  // bodyRoot of the oldest block comes from the full header
  memcpy(body_roots[0], ssz_get(&header, "bodyRoot").bytes.data, 32);
  ssz_hash_tree_root(header, last_block_root);

  // walk the parent_root chain from the oldest+1 up to the anchor (newest)
  for (uint32_t i = 0; i < header_count; i++) {
    ssz_ob_t h = ssz_at(headers, i);
    memcpy(anchor_bytes, h.bytes.data, 16);           // slot and proposerIndex
    memcpy(anchor_bytes + 16, last_block_root, 32);   // parentRoot = previous block root
    memcpy(anchor_bytes + 48, h.bytes.data + 16, 64); // stateRoot and bodyRoot
    ssz_hash_tree_root(header_ob, last_block_root);
    memcpy(body_roots[i + 1], h.bytes.data + 48, 32); // bodyRoot of this block
  }

  // the anchor is the last reconstructed header, or the full header for a single-block range
  *anchor_out = header_count ? header_ob : header;
  return C4_SUCCESS;
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

// Verifies the anchor `tag_proof` (ETH_STATE_BLOCK_UNION) and runs the `latest` freshness gate.
//   - `timestamp` variant: the anchor's execution timestamp is proven against its bodyRoot via
//     `tag_proof_branch`; the proven value feeds `eth_check_latest_freshness`.
//   - `checkpoint_proof` variant (`safe`/`finalized`): structurally prepared but not verified yet.
//   - `none` variant (pinned block): no timestamp -> the gate fails closed if the host demanded
//     freshness for an open-ended tag.
//
// `safe`/`finalized` bind the range endpoint to the beacon checkpoint, which requires the (not yet
// implemented) `checkpoint_proof` variant. Until then these tags are rejected fail-closed: accepting
// the `timestamp` variant for them would let a prover anchor at an arbitrary older canonical block
// (no freshness enforced for non-`latest` tags) and silently omit newer logs.
static bool verify_tag_proof_freshness(verify_ctx_t* ctx, ssz_ob_t proof, json_t filter, bytes32_t anchor_body_root) {
  json_t   to_tag    = json_get(filter, "toBlock");
  bool     is_latest = to_tag.type == JSON_TYPE_NOT_FOUND || eth_json_is_latest(to_tag);
  bool     is_pinned = to_tag.type == JSON_TYPE_STRING && to_tag.len > 2 && to_tag.start[1] == '0' && to_tag.start[2] == 'x';
  if (!is_latest && !is_pinned)
    RETURN_VERIFY_ERROR(ctx, "toBlock tag not yet supported for completeness (only a pinned block or 'latest')");

  ssz_ob_t tag       = ssz_get(&proof, "tag_proof");
  bool     is_ts     = tag.def == eth_ssz_verification_type(ETH_SSZ_DATA_STATE_BLOCK_TIMESTAMP);
  uint64_t anchor_ts = 0;

  if (is_ts) {
    ssz_ob_t  branch    = ssz_get(&proof, "tag_proof_branch");
    gindex_t  gindex[1] = {GINDEX_TIMESTAMP};
    uint8_t   leaf[32]  = {0};
    bytes32_t root      = {0};
    if (tag.bytes.len >= 8) memcpy(leaf, tag.bytes.data, 8); // uint64 leaf: 8 bytes LE, zero-padded to 32
    if (!ssz_verify_multi_merkle_proof(branch.bytes, bytes(leaf, 32), gindex, root))
      RETURN_VERIFY_ERROR(ctx, "invalid tag_proof, missing nodes!");
    if (memcmp(root, anchor_body_root, 32) != 0)
      RETURN_VERIFY_ERROR(ctx, "invalid tag_proof, anchor body root mismatch!");
    anchor_ts = ssz_uint64(tag);
  }
  else if (tag.def && tag.def->name && strcmp(tag.def->name, "checkpoint_proof") == 0)
    RETURN_VERIFY_ERROR(ctx, "checkpoint tag_proof not yet supported for completeness");
  else if (tag.def && tag.def->type != SSZ_TYPE_NONE)
    RETURN_VERIFY_ERROR(ctx, "unexpected tag_proof variant in completeness proof!");

  return eth_check_latest_freshness(ctx, is_latest, is_ts, anchor_ts);
}

bool verify_logs_completeness(verify_ctx_t* ctx) {
  ssz_ob_t proof   = ctx->proof;
  ssz_ob_t header  = ssz_get(&proof, "header");
  ssz_ob_t headers = ssz_get(&proof, "headers");
  ssz_ob_t blocks  = ssz_get(&proof, "blocks");

  // The claim is the requested range (from the RPC request); the proof carries no range endpoints.
  // The block count is derived from the (proven) parentRoot chain length.
  uint64_t count = (uint64_t) ssz_len(headers) + 1;
  // Hard upper bound before any allocation: the SSZ list caps at 4096, but lists of dynamic (union)
  // elements are not length-checked by ssz_is_valid, so reject oversized ranges up front (DoS guard).
  if (count > VERIFY_LOGS_COMPLETENESS_MAX_BLOCKS) RETURN_VERIFY_ERROR(ctx, "completeness range too large!");

  json_t filter = json_at(ctx->args, 0);
  if (filter.type != JSON_TYPE_OBJECT) RETURN_VERIFY_ERROR(ctx, "eth_getLogs completeness requires a filter object!");

  if ((uint64_t) ssz_len(blocks) != count) RETURN_VERIFY_ERROR(ctx, "completeness proof block count mismatch!");

  // reconstruct the parentRoot chain -> per-block bodyRoots + the reconstructed anchor (newest) header
  bytes32_t* body_roots       = safe_calloc((size_t) count, sizeof(bytes32_t));
  uint8_t    anchor_bytes[112] = {0};
  ssz_ob_t   anchor           = {0};
  if (reconstruct_chain(ctx, header, headers, body_roots, (uint32_t) count, anchor_bytes, &anchor) != C4_SUCCESS) {
    safe_free(body_roots);
    return false;
  }

  // Anchor the chain via the shared header_proof union (signature / header-chain / historic summaries),
  // exactly like every other proof type. May return C4_PENDING (need sync committee) or C4_ERROR;
  // both propagate to the caller via ctx->state.
  if (c4_verify_header(ctx, anchor, proof) != C4_SUCCESS) {
    safe_free(body_roots);
    return false;
  }

  bytes_t query_blooms = c4_eth_filter_query_blooms(filter);

  const ssz_def_t* logs_def         = eth_ssz_verification_type(ETH_SSZ_DATA_LOGS);
  ssz_builder_t    data_builder     = ssz_builder_for_def(logs_def);
  uint32_t         log_count        = 0;
  uint64_t         from_num         = 0;
  uint64_t         to_num           = 0;
  bytes32_t        anchor_body_root = {0};
  bool             success          = true;

  for (uint64_t i = 0; i < count; i++) {
    ssz_ob_t union_ob = ssz_at(blocks, (uint32_t) i);
    ssz_ob_t block    = ssz_union(union_ob);
    if (!block.def) { success = false; break; }

    // blockNumber is bound to this block's bodyRoot by the per-block multi proof, so from_num/to_num
    // become cryptographically trusted once the block is verified.
    uint64_t bn = ssz_get_uint64(&block, "blockNumber");
    if (i == 0) from_num = bn;
    // the gap-free blockNumber sequence together with the parentRoot chain rules out skipped blocks
    if (bn != from_num + i) { success = false; break; }
    if (i + 1 == count) {
      to_num = bn;
      memcpy(anchor_body_root, body_roots[i], 32); // keep the anchor bodyRoot for the tag_proof below
    }

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

  // Bind the proven range (now cryptographically established via the bodyRoots) to the range the
  // client actually requested (see bind_range_endpoint). Without this a malicious prover could
  // shrink the range and still produce a valid-looking proof.
  if (success &&
      (!bind_range_endpoint(ctx, json_get(filter, "fromBlock"), from_num, to_num) ||
       !bind_range_endpoint(ctx, json_get(filter, "toBlock"), to_num, to_num)))
    success = false;

  // Freshness gate for an open-ended `toBlock` (absent -> defaults to "latest"): the anchor is only
  // proven canonical, not recent, so a stale-but-signed anchor could otherwise omit recent logs. The
  // anchor's block-tag is proven via the shared ETH_STATE_BLOCK_UNION (`tag_proof`).
  if (success && !verify_tag_proof_freshness(ctx, proof, filter, anchor_body_root)) success = false;

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
