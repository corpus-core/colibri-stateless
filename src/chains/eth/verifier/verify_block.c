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
#include "header_cache.h"
#include "json.h"
#include "patricia.h"
#include "rlp.h"
#include "ssz.h"
#include "sync_committee.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char* SHA3_UNCLUES = "\x1d\xcc\x4d\xe8\xde\xc7\x5d\x7a\xab\x85\xb5\x67\xb6\xcc\xd4\x1a\xd3\x12\x45\x1b\x94\x8a\x74\x13\xf0\xa1\x42\xfd\x40\xd4\x93\x47";

static ssz_builder_t create_txs_builder(verify_ctx_t* ctx, const ssz_def_t* tx_union_def, bool include_txs, ssz_ob_t txs, bytes_t el_header, bytes32_t block_hash) {
  ssz_builder_t txs_builder  = ssz_builder_for_def(tx_union_def->def.container.elements + ((int) include_txs));
  node_t*       root         = NULL;
  ssz_builder_t tx_builder   = ssz_builder_for_def(txs_builder.def->def.vector.type);
  uint64_t      block_number    = eth_el_header_get_uint64(el_header, EL_BLOCK_NUMBER);
  uint64_t      base_fee        = eth_el_header_get_uint64(el_header, EL_BASE_FEE_PER_GAS);
  uint64_t      block_timestamp = eth_el_header_get_uint64(el_header, EL_TIMESTAMP);

  int len = ssz_len(txs);
  for (int i = 0; i < len; i++) {
    bytes_t   raw_tx = ssz_at(txs, i).bytes;
    bytes32_t tx_hash;
    keccak(raw_tx, tx_hash);

    if (include_txs) {
      // we reset the builder to to avoid allocating memory too ofter and simply resuing the already allocated memory
      tx_builder.fixed.data.len   = 0;
      tx_builder.dynamic.data.len = 0;
      if (!c4_write_tx_data_from_raw(ctx, &tx_builder, raw_tx, tx_hash, block_hash, block_number, i, base_fee, block_timestamp)) break;
      buffer_append(&tx_builder.fixed, tx_builder.dynamic.data);
      ssz_add_dynamic_list_bytes(&txs_builder, len, tx_builder.fixed.data);
    }
    else
      buffer_append(&txs_builder.fixed, bytes(tx_hash, 32));
  }

  buffer_free(&tx_builder.dynamic);
  buffer_free(&tx_builder.fixed);

  return txs_builder;
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
    // EthBlockData uses "hash", the other data/payload containers use "blockHash"
    bytes_t block_hash = ssz_get_def(block.def, "blockHash") ? ssz_get(&block, "blockHash").bytes : ssz_get(&block, "hash").bytes;
    if (block_hash.len != 32 || memcmp(hash, block_hash.data, 32) != 0) RETURN_VERIFY_ERROR(ctx, "blockhash mismatch");
    return true;
  }
  // EthBlockData uses "number", the other data/payload containers use "blockNumber"
  else if ((ssz_get_def(block.def, "blockNumber") ? ssz_get_uint64(&block, "blockNumber") : ssz_get_uint64(&block, "number")) != json_as_uint64(req_block))
    RETURN_VERIFY_ERROR(ctx, "blocknumber mismatch");
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

bool eth_set_block_data(verify_ctx_t* ctx, bytes_t el_header, bool include_txs, ssz_ob_t* body, uint32_t mask) {
  if (strcmp(ctx->method, "eth_blobBaseFee") == 0) {
    uint64_t      fee     = fake_exponential(1, eth_el_header_get_uint64(el_header, EL_EXCESS_BLOB_GAS), 3338477);
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
    ssz_add_bytes(&builder, "requestsHash", eth_el_header_get(el_header, EL_REQUESTS_HASH));
    ssz_add_bytes(&builder, "blockAccessListHash", eth_el_header_get(el_header, EL_BLOCK_ACCESS_LIST_HASH));
    ctx->data = ssz_builder_to_bytes(&builder);
  }
  else if (strcmp(ctx->method, "eth_getBlockByNumber") == 0 || strcmp(ctx->method, "eth_getBlockByHash") == 0) {
    bytes32_t block_hash = {0};
    keccak(el_header, block_hash);
    bytes32_t     tx_root = {0};
    ssz_builder_t data    = ssz_builder_for_type(ETH_SSZ_DATA_BLOCK);
    ssz_add_uint32(&data, mask);
    ssz_add_uint64(&data, eth_el_header_get_uint64(el_header, EL_BLOCK_NUMBER));
    ssz_add_bytes(&data, "hash", bytes(block_hash, 32));
    ssz_add_builders(&data, "transactions", create_txs_builder(ctx, ssz_get_def(data.def, "transactions"), include_txs, ssz_get(body, "transactions"), el_header, block_hash));
    ssz_add_bytes(&data, "logsBloom", eth_el_header_get(el_header, EL_LOGS_BLOOM));
    ssz_add_bytes(&data, "receiptsRoot", eth_el_header_get(el_header, EL_RECEIPTS_ROOT));
    ssz_add_bytes(&data, "extraData", eth_el_header_get(el_header, EL_EXTRA_DATA));
    ssz_add_bytes(&data, "withdrawalsRoot", eth_el_header_get(el_header, EL_WITHDRAWALS_ROOT));
    ssz_add_uint256(&data, eth_el_header_get(el_header, EL_BASE_FEE_PER_GAS));
    ssz_add_uint64(&data, eth_el_header_get_uint64(el_header, EL_NONCE));
    ssz_add_bytes(&data, "miner", eth_el_header_get(el_header, EL_FEE_RECIPIENT));
    ssz_add_bytes(&data, "withdrawals", ssz_get(body, "withdrawals").bytes);
    ssz_add_uint64(&data, eth_el_header_get_uint64(el_header, EL_EXCESS_BLOB_GAS));
    ssz_add_bytes(&data, "difficulty", NULL_BYTES);
    ssz_add_uint64(&data, eth_el_header_get_uint64(el_header, EL_GAS_LIMIT));
    ssz_add_uint64(&data, eth_el_header_get_uint64(el_header, EL_GAS_USED));
    ssz_add_uint64(&data, eth_el_header_get_uint64(el_header, EL_TIMESTAMP));
    ssz_add_bytes(&data, "mixHash", eth_el_header_get(el_header, EL_PREV_RANDAO));
    ssz_add_bytes(&data, "parentHash", eth_el_header_get(el_header, EL_PARENT_HASH));
    ssz_add_bytes(&data, "uncles", NULL_BYTES);
    ssz_add_bytes(&data, "parentBeaconBlockRoot", eth_el_header_get(el_header, EL_PARENT_BEACON_BLOCK_ROOT));
    ssz_add_bytes(&data, "sha3Uncles", bytes(SHA3_UNCLUES, 32));
    ssz_add_bytes(&data, "transactionsRoot", eth_el_header_get(el_header, EL_TRANSACTIONS_ROOT));
    ssz_add_bytes(&data, "stateRoot", eth_el_header_get(el_header, EL_STATE_ROOT));
    ssz_add_uint64(&data, eth_el_header_get_uint64(el_header, EL_BLOB_GAS_USED));
    ssz_add_bytes(&data, "requestsHash", eth_el_header_get(el_header, EL_REQUESTS_HASH));
    ssz_add_bytes(&data, "blockAccessListHash", eth_el_header_get(el_header, EL_BLOCK_ACCESS_LIST_HASH));

    ctx->data = ssz_builder_to_bytes(&data);
  }
  else
    return false;
  ctx->flags |= VERIFY_FLAG_FREE_DATA;
  return true;
}

bool verify_block_proof(verify_ctx_t* ctx) {
  bool      include_txs   = false;
  bytes_t   el_header     = {0};
  bytes32_t block_hash    = {0};
  bool      is_full_block = ctx->method && (strcmp(ctx->method, "eth_getBlockByNumber") == 0 || strcmp(ctx->method, "eth_getBlockByHash") == 0);

  if (!ctx->method || (!is_full_block && !is_block_header_method(ctx->method))) RETURN_VERIFY_ERROR(ctx, "method mismatch for block proof");
  // full block methods take [block, includeTx], header-only methods at most [block]
  if (json_len(ctx->args) > (is_full_block ? 2 : 1)) RETURN_VERIFY_ERROR(ctx, "invalid arguments for block proof");
  if (c4_verify_block(ctx, ssz_get(&ctx->proof, "block"), &el_header, block_hash) != C4_SUCCESS) return false;

  ssz_ob_t body          = ssz_get(&ctx->proof, "body");
  bool     has_body      = body.def && strcmp(body.def->name, "content") == 0;
  bytes_t  cached_header = NULL_BYTES;
  ssz_ob_t cached_body   = {0};
  if (!has_body && is_full_block) {
#ifdef EL_HEADER_CACHE
    // sequencerProof omits proof.body because the signed execution payload already
    // carries transactions + withdrawals. The OP hook rebuilds ETH_SSZ_EL_BLOCK_CONTENT
    // from that payload and stores it in the header cache; recover it here.
    // proof.body, when present, and cache-loaded bodies stay untrusted until the
    // roots below match the verified header.
    cached_header = c4_header_cache_get_el_header(ctx->chain_id, block_hash, &cached_body);
    if (cached_body.def && cached_body.bytes.data) {
      body     = cached_body;
      has_body = true;
    }
    safe_free(cached_header.data);
#endif
    if (!has_body) RETURN_VERIFY_ERROR(ctx, "missing body for block proof");
  }
  if (has_body) {
    // the body payload is untrusted until its roots match the verified header
    bytes32_t transaction_root = {0};
    include_txs                = json_as_bool(json_at(ctx->args, 1));
    eth_get_transactions_root(transaction_root, ssz_get(&body, "transactions"));
    if (memcmp(transaction_root, eth_el_header_get(el_header, EL_TRANSACTIONS_ROOT).data, 32) != 0) {
      safe_free(cached_body.bytes.data);
      RETURN_VERIFY_ERROR(ctx, "invalid transaction root!");
    }
    // Bind the withdrawals list. On ETH the merkle must equal the header
    // withdrawalsRoot (EIP-4895). On OP Isthmus that header field is the
    // L2ToL1MessagePasser storage root, so equality is not required — but the
    // list is still consensus-empty, so the merkle must be EMPTY_ROOT_HASH.
    // proof.body and persisted cache bodies are prover-controlled; do not
    // accept a non-empty spoofed list just because the header is Isthmus.
    bytes32_t withdrawal_root = {0};
    eth_get_withdrawals_root(withdrawal_root, ssz_get(&body, "withdrawals"));
    bytes_t header_wd = eth_el_header_get(el_header, EL_WITHDRAWALS_ROOT);
    if (header_wd.len != 32 || memcmp(withdrawal_root, header_wd.data, 32) != 0) {
      if (c4_chain_type(ctx->chain_id) != C4_CHAIN_TYPE_OP ||
          memcmp(withdrawal_root, EMPTY_ROOT_HASH, 32) != 0) {
        safe_free(cached_body.bytes.data);
        RETURN_VERIFY_ERROR(ctx, "invalid withdrawal root!");
      }
    }
  }

  bytes_t   header = {0};
  fork_id_t fork   = (rlp_decode(&el_header, 0, &header) == RLP_LIST && rlp_decode(&header, -1, &header) == 23) ? C4_FORK_GLOAS : C4_FORK_FULU;

  if (!eth_set_block_data(ctx, el_header, include_txs, has_body ? &body : NULL, fork == C4_FORK_GLOAS ? ETH_BLOCK_DATA_MASK_ALL : ETH_BLOCK_DATA_MASK_ALL_WITHOUT_REQUESTS)) {
    safe_free(cached_body.bytes.data);
    return false;
  }
  safe_free(cached_body.bytes.data);
  if (json_len(ctx->args) >= 1 && !c4_eth_matches_blocknumber(ctx, ctx->data, json_at(ctx->args, 0))) return false;
  if (!eth_check_latest_freshness(ctx, json_len(ctx->args) == 0 || eth_json_is_latest(json_at(ctx->args, 0)), true, eth_el_header_get_uint64(el_header, EL_TIMESTAMP))) return false;
  ctx->success = true;
  return ctx->success;
}
