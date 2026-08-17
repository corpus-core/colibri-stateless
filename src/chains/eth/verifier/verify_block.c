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
#include "json.h"
#include "patricia.h"
#include "rlp.h"
#include "ssz.h"
#include "sync_committee.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define EXECUTION_PAYLOAD_ROOT_GINDEX 25

// GINDEX_BLOCKUMBER (806) is provided by eth_tx.h; GINDEX_TIMESTAMP (809) by
// eth_account.h. Both already get pulled in via the includes above.

static const char* SHA3_UNCLUES = "\x1d\xcc\x4d\xe8\xde\xc7\x5d\x7a\xab\x85\xb5\x67\xb6\xcc\xd4\x1a\xd3\x12\x45\x1b\x94\x8a\x74\x13\xf0\xa1\x42\xfd\x40\xd4\x93\x47";
static const char* EMPTY_SHA256 = "\xe3\xb0\xc4\x42\x98\xfc\x1c\x14\x9a\xfb\xf4\xc8\x99\x6f\xb9\x24\x27\xae\x41\xe4\x64\x9b\x93\x4c\xa4\x95\x99\x1b\x78\x52\xb8\x55";

static ssz_builder_t create_txs_builder(verify_ctx_t* ctx, const ssz_def_t* tx_union_def, bool include_txs, ssz_ob_t txs, bytes32_t tx_root, uint64_t block_number, bytes32_t block_hash, uint64_t base_fee) {
  ssz_builder_t txs_builder = ssz_builder_for_def(tx_union_def->def.container.elements + ((int) include_txs));
  node_t*       root        = NULL;
  bytes32_t     tmp         = {0};
  buffer_t      buf         = stack_buffer(tmp);
  ssz_builder_t tx_builder  = ssz_builder_for_def(txs_builder.def->def.vector.type);

  int len = ssz_len(txs);
  for (int i = 0; i < len; i++) {
    bytes_t   raw_tx = ssz_at(txs, i).bytes;
    bytes32_t tx_hash;
    keccak(raw_tx, tx_hash);
    patricia_set_value(&root, c4_eth_create_tx_path(i, &buf), raw_tx);

    if (include_txs) {
      // we reset the builder to to avoid allocating memory too ofter and simply resuing the already allocated memory
      tx_builder.fixed.data.len   = 0;
      tx_builder.dynamic.data.len = 0;
      if (!c4_write_tx_data_from_raw(ctx, &tx_builder, raw_tx, tx_hash, block_hash, block_number, i, base_fee)) break;
      buffer_append(&tx_builder.fixed, tx_builder.dynamic.data);
      ssz_add_dynamic_list_bytes(&txs_builder, len, tx_builder.fixed.data);
    }
    else
      buffer_append(&txs_builder.fixed, bytes(tx_hash, 32));
  }
  if (root) {
    memcpy(tx_root, patricia_get_root(root).data, 32);

    patricia_node_free(root);
  }
  else
    memcpy(tx_root, EMPTY_ROOT_HASH, 32);

  buffer_free(&tx_builder.dynamic);
  buffer_free(&tx_builder.fixed);

  return txs_builder;
}

void eth_set_block_data(verify_ctx_t* ctx, uint32_t mask, ssz_ob_t block, bytes32_t parent_root, bytes32_t withdrawel_root, bool include_txs) {
  if (ctx->data.def && ctx->data.def->type == SSZ_TYPE_CONTAINER) return;

  bytes32_t     tx_root = {0};
  ssz_builder_t data    = ssz_builder_for_type(ETH_SSZ_DATA_BLOCK);
  ssz_add_uint32(&data, mask);
  ssz_add_bytes(&data, "number", ssz_get(&block, "blockNumber").bytes);
  ssz_add_bytes(&data, "hash", ssz_get(&block, "blockHash").bytes);
  ssz_add_builders(&data, "transactions", create_txs_builder(ctx, ssz_get_def(data.def, "transactions"), include_txs, ssz_get(&block, "transactions"), tx_root, ssz_get_uint64(&block, "blockNumber"), ssz_get(&block, "blockHash").bytes.data, ssz_get_uint64(&block, "baseFeePerGas")));
  ssz_add_bytes(&data, "logsBloom", ssz_get(&block, "logsBloom").bytes);
  ssz_add_bytes(&data, "receiptsRoot", ssz_get(&block, "receiptsRoot").bytes);
  ssz_add_bytes(&data, "extraData", ssz_get(&block, "extraData").bytes);
  ssz_add_bytes(&data, "withdrawalsRoot", bytes(withdrawel_root, 32));
  ssz_add_bytes(&data, "baseFeePerGas", ssz_get(&block, "baseFeePerGas").bytes);
  ssz_add_bytes(&data, "nonce", bytes(NULL, 8));
  ssz_add_bytes(&data, "miner", ssz_get(&block, "feeRecipient").bytes);
  ssz_add_bytes(&data, "withdrawals", ssz_get(&block, "withdrawals").bytes);
  ssz_add_bytes(&data, "excessBlobGas", ssz_get(&block, "excessBlobGas").bytes);
  ssz_add_bytes(&data, "difficulty", NULL_BYTES);
  ssz_add_bytes(&data, "gasLimit", ssz_get(&block, "gasLimit").bytes);
  ssz_add_bytes(&data, "gasUsed", ssz_get(&block, "gasUsed").bytes);
  ssz_add_bytes(&data, "timestamp", ssz_get(&block, "timestamp").bytes);
  ssz_add_bytes(&data, "mixHash", ssz_get(&block, "prevRandao").bytes);
  ssz_add_bytes(&data, "parentHash", ssz_get(&block, "parentHash").bytes);
  ssz_add_bytes(&data, "uncles", NULL_BYTES);
  ssz_add_bytes(&data, "parentBeaconBlockRoot", bytes(parent_root, 32));
  ssz_add_bytes(&data, "sha3Uncles", bytes(SHA3_UNCLUES, 32));
  ssz_add_bytes(&data, "transactionsRoot", bytes(tx_root, 32));
  ssz_add_bytes(&data, "stateRoot", ssz_get(&block, "stateRoot").bytes);
  ssz_add_bytes(&data, "blobGasUsed", ssz_get(&block, "blobGasUsed").bytes);
  ssz_add_bytes(&data, "requestsHash", bytes(EMPTY_SHA256, 32));
  ctx->data = ssz_builder_to_bytes(&data);
  ctx->flags |= VERIFY_FLAG_FREE_DATA;
}

bool c4_eth_matches_blocknumber(verify_ctx_t* ctx, ssz_ob_t block, json_t req_block) {
  const char* err = json_validate(req_block, req_block.len == 68 ? "bytes32" : "block", "params[0]");
  if (err) {
    c4_state_add_error(&ctx->state, err);
    safe_free((void*) err);
    ctx->success = false;
    return false;
  }
  // Unprovable tags (pending/earliest) must never wildcard-match a proof: they are routed to a
  // direct RPC call and can never legitimately back a sync-committee proof. Rejecting them here
  // keeps the guard that `check_block` used to provide before it accepted these tags.
  if (eth_json_is_unproofable_tag(req_block)) RETURN_VERIFY_ERROR(ctx, "block tag not provable");
  if (req_block.start[1] != '0' || req_block.start[2] != 'x') return true; // already validated as 'latest'/'safe'/'finalized'
  if (req_block.len == 68) {                                               // hash
    bytes32_t hash = {0};
    buffer_t  buf  = stack_buffer(hash);
    json_as_bytes(req_block, &buf);
    if (memcmp(hash, ssz_get(&block, "blockHash").bytes.data, 32) != 0) RETURN_VERIFY_ERROR(ctx, "blockhash mismatch");
    return true;
  }
  else if (ssz_get_uint64(&block, "blockNumber") != json_as_uint64(req_block))
    RETURN_VERIFY_ERROR(ctx, "blocknumber mismatch");
  return true;
}

bool verify_block_proof_for_block(verify_ctx_t* ctx, ssz_ob_t block_proof, json_t block_number, bytes32_t execution_payload_root) {

  bytes32_t body_root         = {0};
  bytes32_t exec_root         = {0};
  ssz_ob_t  execution_payload = ssz_get(&block_proof, "executionPayload");
  ssz_ob_t  proof             = ssz_get(&block_proof, "proof");
  ssz_ob_t  header            = ssz_get(&block_proof, "header");

  // calculate the tree root of the execution payload
  ssz_hash_tree_root(execution_payload, exec_root);

  ssz_verify_single_merkle_proof(proof.bytes, exec_root, EXECUTION_PAYLOAD_ROOT_GINDEX, body_root);
  if (memcmp(body_root, ssz_get(&header, "bodyRoot").bytes.data, 32) != 0) RETURN_VERIFY_ERROR(ctx, "invalid body root!");
  if (c4_verify_header(ctx, header, block_proof) != C4_SUCCESS) return false;
  ssz_hash_tree_root(ssz_get(&execution_payload, "withdrawals"), exec_root);

  if (ctx->state.error || !c4_eth_matches_blocknumber(ctx, execution_payload, block_number)) return false;
  if (execution_payload_root) memcpy(execution_payload_root, exec_root, 32);
  return true;
}

bool verify_block_proof(verify_ctx_t* ctx) {
  bool is_hybrid = ssz_is_type(&ctx->proof, eth_ssz_verification_type(ETH_SSZ_VERIFY_HYBRID_BLOCK_PROOF));

  if (is_hybrid) {
    if (!(ctx->flags & VERIFY_FLAG_HYBRID))
      RETURN_VERIFY_ERROR(ctx, "hybrid block proof requires hybrid mode");

    json_t   block_number      = json_at(ctx->args, 0);
    bool     include_txs       = json_as_bool(json_at(ctx->args, 1));
    ssz_ob_t execution_payload = ssz_get(&ctx->proof, "executionPayload");
    if (!execution_payload.bytes.data) RETURN_VERIFY_ERROR(ctx, "missing executionPayload in hybrid block proof");
    if (!c4_eth_matches_blocknumber(ctx, execution_payload, block_number)) return false;

    if (!eth_check_latest_freshness(ctx, eth_json_is_latest(block_number), true,
                                    ssz_get_uint64(&execution_payload, "timestamp")))
      return false;

    bytes32_t withdrawal_root = {0};
    ssz_hash_tree_root(ssz_get(&execution_payload, "withdrawals"), withdrawal_root);

    bytes32_t parent_root = {0}; // BeaconBlockHeader not available in hybrid mode
    eth_set_block_data(ctx, ETH_BLOCK_DATA_MASK_ALL_WITHOUT_REQUESTS, execution_payload, parent_root, withdrawal_root, include_txs);
    if (ctx->state.error) return false;

    ctx->success = true;
    return true;
  }

  json_t    block_number      = json_at(ctx->args, 0);
  bytes32_t exec_root         = {0};
  ssz_ob_t  execution_payload = ssz_get(&ctx->proof, "executionPayload");
  ssz_ob_t  header            = ssz_get(&ctx->proof, "header");
  bool      include_txs       = json_as_bool(json_at(ctx->args, 1));

  if (!verify_block_proof_for_block(ctx, ctx->proof, block_number, exec_root)) return false;

  if (!eth_check_latest_freshness(ctx, eth_json_is_latest(block_number), true,
                                  ssz_get_uint64(&execution_payload, "timestamp")))
    return false;

  eth_set_block_data(ctx, ETH_BLOCK_DATA_MASK_ALL_WITHOUT_REQUESTS, execution_payload, ssz_get(&header, "parentRoot").bytes.data, exec_root, include_txs);
  if (ctx->state.error) return false;

  ctx->success = true;
  return true;
}

// EIP-4844 blob base fee: factor * e^(numerator/denominator) via Taylor series.
// Uses (a*b)/c = (a/c)*b + (a%c)*b/c to avoid 128-bit intermediate values.
static uint64_t fake_exponential(uint64_t factor, uint64_t numerator, uint64_t denominator) {
  uint64_t i = 1, output = 0, numerator_accum = factor * denominator;
  while (numerator_accum > 0) {
    output += numerator_accum;
    uint64_t div    = denominator * i;
    uint64_t q      = numerator_accum / div;
    uint64_t r      = numerator_accum % div;
    numerator_accum = q * numerator + r * numerator / div;
    i++;
  }
  return output / denominator;
}

static bool is_block_header_method(const char* method) {
  return strcmp(method, "eth_getBlockHeader") == 0 || strcmp(method, "eth_blockNumber") == 0 || strcmp(method, "eth_blobBaseFee") == 0 || strcmp(method, "eth_maxPriorityFeePerGas") == 0;
}

static bool create_block_header_data(verify_ctx_t* ctx, bytes_t el_header) {
  if (strcmp(ctx->method, "eth_blobBaseFee") == 0) {
    uint64_t      fee     = fake_exponential(1, ssz_get_uint64(&ctx->data, "excessBlobGas"), 3338477);
    ssz_builder_t builder = ssz_builder_for_type(ETH_SSZ_DATA_UINT256);
    ssz_add_uint64(&builder, fee);
    buffer_append(&builder.fixed, bytes(NULL, 24));
    ctx->data = ssz_builder_to_bytes(&builder);
  }
  else if (strcmp(ctx->method, "eth_maxPriorityFeePerGas") == 0) {
    ssz_builder_t builder = ssz_builder_for_type(ETH_SSZ_DATA_UINT256);
    ssz_add_uint64(&builder, 1000000000ULL);
    buffer_append(&builder.fixed, bytes(NULL, 24));
    ctx->data = ssz_builder_to_bytes(&builder);
  }
  else if (strcmp(ctx->method, "eth_blockNumber") == 0) {
    ssz_builder_t builder = ssz_builder_for_type(ETH_SSZ_DATA_UINT256);
    ssz_add_uint64(&builder, eth_el_header_get_uint64(el_header, "blockNumber"));
    buffer_append(&builder.fixed, bytes(NULL, 24));
    ctx->data = ssz_builder_to_bytes(&builder);
  }
  else if (strcmp(ctx->method, "eth_getBlockHeader") == 0) {
    bytes32_t block_hash = {0};
    keccak(el_header, block_hash);
    ssz_builder_t builder = ssz_builder_for_type(ETH_SSZ_DATA_BLOCK_HEADER);
    ssz_add_bytes(&builder, "parentHash", eth_el_header_get(el_header, EL_PARENT_HASH));
    ssz_add_bytes(&builder, "stateRoot", eth_el_header_get(el_header, EL_STATE_ROOT));
    ssz_add_bytes(&builder, "receiptsRoot", eth_el_header_get(el_header, EL_RECEIPTS_ROOT));
    ssz_add_bytes(&builder, "logsBloom", eth_el_header_get(el_header, EL_LOGS_BLOOM));
    ssz_add_uint64(&builder, eth_el_header_get_uint64(el_header, EL_BLOCK_NUMBER));
    ssz_add_uint64(&builder, eth_el_header_get_uint64(el_header, EL_GAS_LIMIT));
    ssz_add_uint64(&builder, eth_el_header_get_uint64(el_header, EL_GAS_USED));
    ssz_add_uint64(&builder, eth_el_header_get_uint64(el_header, EL_TIMESTAMP));
    ssz_add_uint256(&builder, eth_el_header_get(el_header, EL_BASE_FEE_PER_GAS));
    ssz_add_bytes(&builder, "blockHash", bytes(block_hash, 32));
    ssz_add_uint64(&builder, eth_el_header_get_uint64(el_header, EL_BLOB_GAS_USED));
    ssz_add_uint64(&builder, eth_el_header_get_uint64(el_header, EL_EXCESS_BLOB_GAS));
    ssz_add_bytes(&builder, "feeRecipient", eth_el_header_get(el_header, EL_FEE_RECIPIENT));
    ssz_add_bytes(&builder, "transactionsRoot", eth_el_header_get(el_header, EL_TRANSACTIONS_ROOT));
    ctx->data = ssz_builder_to_bytes(&builder);
  }
  else
    return false;
  ctx->flags |= VERIFY_FLAG_FREE_DATA;
  return true;
}

bool verify_block_header_proof(verify_ctx_t* ctx) {
  // `eth_blobBaseFee` and `eth_maxPriorityFeePerGas` take no arguments and
  // implicitly target `latest`; `eth_getBlockHeader` may take an explicit tag.
  bool      is_latest  = json_len(ctx->args) == 0 || eth_json_is_latest(json_at(ctx->args, 0));
  bytes_t   el_header  = {0};
  bytes32_t block_hash = {0};
  if (!ctx->method || !is_block_header_method(ctx->method)) RETURN_VERIFY_ERROR(ctx, "method mismatch for block header proof");
  if (json_len(ctx->args) > 1) RETURN_VERIFY_ERROR(ctx, "invalid arguments for block header proof");
  if (c4_verify_block(ctx, ssz_get(&ctx->proof, "block"), &el_header, block_hash) != C4_SUCCESS) return false;
  if (!create_block_header_data(ctx, el_header)) return false;
  if (json_len(ctx->args) >= 1 && !c4_eth_matches_blocknumber(ctx, ctx->data, json_at(ctx->args, 0))) return false;
  if (!eth_check_latest_freshness(ctx, is_latest, true, eth_el_header_get_uint64(el_header, "timestamp"))) return false;
  ctx->success = true;
  return ctx->success;
}
