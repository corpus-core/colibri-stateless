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

#include "el_header.h"
#include "beacon_types.h"
#include "crypto.h"
#include "eth_account.h"
#include "eth_tx.h"
#include "patricia.h"
#include "rlp.h"
typedef bytes_t (*el_header_field_func)(void*, buffer_t*, char*);
// prefix defines the RLP encoding: H = 32-byte hash, A = 20-byte address,
// B = raw bytes, U = uint (big-endian, no leading zeros)
static const char* el_header_field_names[] = {
    "H:" EL_PARENT_HASH,              // 0
    "H:" EL_SHA3_UNCLES,              // 1
    "A:" EL_FEE_RECIPIENT,            // 2
    "H:" EL_STATE_ROOT,               // 3
    "H:" EL_TRANSACTIONS_ROOT,        // 4
    "H:" EL_RECEIPTS_ROOT,            // 5
    "B:" EL_LOGS_BLOOM,               // 6
    "U:" EL_DIFFICULTY,               // 7
    "U:" EL_BLOCK_NUMBER,             // 8
    "U:" EL_GAS_LIMIT,                // 9
    "U:" EL_GAS_USED,                 // 10
    "U:" EL_TIMESTAMP,                // 11
    "B:" EL_EXTRA_DATA,               // 12
    "H:" EL_PREV_RANDAO,              // 13
    "B:" EL_NONCE,                    // 14
    "U:" EL_BASE_FEE_PER_GAS,         // 15
    "H:" EL_WITHDRAWALS_ROOT,         // 16
    "U:" EL_BLOB_GAS_USED,            // 17
    "U:" EL_EXCESS_BLOB_GAS,          // 18
    "H:" EL_PARENT_BEACON_BLOCK_ROOT, // 19
    "H:" EL_REQUESTS_HASH,            // 20
    "H:" EL_BLOCK_ACCESS_LIST_HASH,   // 21
    "U:" EL_SLOT_NUMBER,              // 22
};

// keccak256(rlp.encode([])) = keccak256(0xc0); sha3Uncles of no uncles and EIP-7928 empty BAL hash
const char* EMPTY_RLP_LIST = "\x1d\xcc\x4d\xe8\xde\xc7\x5d\x7a\xab\x85\xb5\x67\xb6\xcc\xd4\x1a\xd3\x12\x45\x1b\x94\x8a\x74\x13\xf0\xa1\x42\xfd\x40\xd4\x93\x47";

static c4_status_t eth_el_header_build(c4_state_t* state, bytes_t* el_header, fork_id_t fork, void* data, el_header_field_func get_field_func) {
  buffer_t buffer = {0};
  buffer_t result = {0};
  int      count  = sizeof(el_header_field_names) / sizeof(el_header_field_names[0]);
  if (fork < C4_FORK_GLOAS) count -= 2;   // no blockAccessListHash and slotNumber before Gloas
  if (fork < C4_FORK_ELECTRA) count -= 1; // no requestsHash before Prague/Electra

  for (int i = 0; i < count; i++) {
    const char  type       = el_header_field_names[i][0];
    const char* field_name = el_header_field_names[i] + 2;
    bytes_t     value      = {0};
    if (strcmp(field_name, EL_SHA3_UNCLES) == 0)
      value = bytes(EMPTY_RLP_LIST, 32);
    else if (strcmp(field_name, EL_DIFFICULTY) == 0)
      value = bytes("\x00", 1);
    else if (strcmp(field_name, EL_NONCE) == 0)
      value = bytes("\x00\x00\x00\x00\x00\x00\x00\x00", 8);
    else
      value = get_field_func(data, &buffer, field_name);
    switch (type) {
      case 'H':
      case 'A': {
        bytes32_t data     = {0};
        int       must_len = type == 'H' ? 32 : 20;
        int       copy_len = must_len < value.len ? must_len : value.len;
        if (value.data) memcpy(data + must_len - copy_len, value.data, copy_len);
        rlp_add_item(&result, bytes(data, must_len));
        break;
      }

      case 'B':
        rlp_add_item(&result, value);
        break;
      case 'U':
        rlp_add_uint(&result, value);
        break;
      default:
        break;
    }
  }
  rlp_to_list(&result);
  *el_header = result.data;
  buffer_free(&buffer);

  return C4_SUCCESS;
}

// computes the requests_hash as defined in EIP-7685:
// sha256( sha256(0x00 ++ deposits) ++ sha256(0x01 ++ withdrawals) ++ sha256(0x02 ++ consolidations) )
// where requests with empty request_data are skipped. Since all request containers are
// fixed-size, the raw SSZ list bytes are exactly the flat request_data encoding.
static void get_requests_hash(bytes32_t out_hash, eth_el_header_ctx_t* ctx) {
  // EIP-7685: no request lists → sha256(''). keccak('') is a different value
  // (empty-code hash) and must not be used here.
  if (ctx->beacon_block.def == NULL) {
    sha256(NULL_BYTES, out_hash);
    return;
  }
  ssz_ob_t           body               = ssz_get(&ctx->beacon_block, "body");
  ssz_ob_t           execution_requests = ctx->execution_requests.def ? ctx->execution_requests : ssz_get(&body, "executionRequests");
  static const char* request_lists[]    = {"deposits", "withdrawals", "consolidations"};
  uint8_t            hashes[sizeof(request_lists) / sizeof(request_lists[0]) * 32]; // one intermediate hash per non-empty request list
  uint32_t           hashes_len = 0;
  for (uint8_t type = 0; type < sizeof(request_lists) / sizeof(request_lists[0]); type++) {
    ssz_ob_t list = ssz_get(&execution_requests, request_lists[type]);
    if (list.def == NULL || list.bytes.len == 0) continue; // empty requests are excluded per EIP-7685
    sha256_merkle(bytes(&type, 1), list.bytes, hashes + hashes_len);
    hashes_len += 32;
  }
  sha256(bytes(hashes, hashes_len), out_hash);
}

// computes the withdrawalsRoot as defined in EIP-4895: a Merkle Patricia Trie built over
// all withdrawals of the execution payload. The key for withdrawal i is the RLP-encoded
// index (same encoding as the transactions trie), the value is the RLP-encoded list
// [index, validatorIndex, address, amount] (all uint64 except address which is 20 bytes).
void eth_get_withdrawals_root(bytes32_t out_hash, ssz_ob_t withdrawals) {
  uint32_t len = ssz_len(withdrawals);

  if (len == 0) {
    memcpy(out_hash, EMPTY_ROOT_HASH, 32);
    return;
  }

  bytes32_t path_tmp = {0};
  buffer_t  path_buf = stack_buffer(path_tmp);
  uint8_t   value_tmp[64]; // 4 uint64 (each up to 9 bytes with RLP prefix) + 20-byte address + list header well under 64
  buffer_t  value_buf = stack_buffer(value_tmp);
  node_t*   root      = NULL;

  for (uint32_t i = 0; i < len; i++) {
    ssz_ob_t w         = ssz_at(withdrawals, i);
    ssz_ob_t address   = ssz_get(&w, "address");
    value_buf.data.len = 0;
    rlp_add_uint64(&value_buf, ssz_get_uint64(&w, "index"));
    rlp_add_uint64(&value_buf, ssz_get_uint64(&w, "validatorIndex"));
    rlp_add_item(&value_buf, address.bytes);
    rlp_add_uint64(&value_buf, ssz_get_uint64(&w, "amount"));
    rlp_to_list(&value_buf);
    patricia_set_value(&root, c4_eth_create_tx_path(i, &path_buf), value_buf.data);
  }

  memcpy(out_hash, patricia_get_root(root).data, 32);
  patricia_node_free(root);
}

// EIP-7928: block_access_list_hash = keccak256(rlp.encode(BAL)).
// The payload field is already RLP; an empty ProgressiveByteList means rlp.encode([]) = 0xc0.
void eth_get_block_access_list_hash(bytes32_t out_hash, bytes_t rlp_encoded_bal) {
  uint8_t empty_rlp = 0xc0;
  bytes_t input     = (rlp_encoded_bal.len && rlp_encoded_bal.data) ? rlp_encoded_bal : bytes(&empty_rlp, 1);
  keccak(input, out_hash);
}

// EIP-4895: transactionsRoot is a Merkle Patricia Trie over raw transaction bytes, keyed by RLP index.
void eth_get_transactions_root(bytes32_t out_hash, ssz_ob_t txs) {
  uint32_t len = ssz_len(txs);

  if (len == 0) {
    memcpy(out_hash, EMPTY_ROOT_HASH, 32);
    return;
  }

  bytes32_t path_tmp = {0};
  buffer_t  path_buf = stack_buffer(path_tmp);
  node_t*   root     = NULL;

  for (uint32_t i = 0; i < len; i++) {
    ssz_ob_t tx = ssz_at(txs, i);
    patricia_set_value(&root, c4_eth_create_tx_path(i, &path_buf), tx.bytes);
  }

  memcpy(out_hash, patricia_get_root(root).data, 32);
  patricia_node_free(root);
}

static bytes_t get_from_ep(void* data, buffer_t* buffer, char* name) {
  eth_el_header_ctx_t* ctx = (eth_el_header_ctx_t*) data;
  if (strcmp(name, EL_PARENT_BEACON_BLOCK_ROOT) == 0)
    return bytes(ctx->parent_root, 32);
  if (strcmp(name, EL_REQUESTS_HASH) == 0) {
    if (ctx->has_requests_hash) return bytes(ctx->requests_hash, 32);
    buffer_reset(buffer);
    buffer_grow(buffer, 32);
    buffer->data.len = 32;
    get_requests_hash(buffer->data.data, ctx);
    return buffer->data;
  }
  if (strcmp(name, EL_SLOT_NUMBER) == 0) {
    if (ctx->has_slot && !ssz_get_def(ctx->execution_payload.def, name)) {
      buffer_reset(buffer);
      buffer_grow(buffer, 8);
      uint64_to_be(buffer->data.data, ctx->slot);
      buffer->data.len = 8;
      return buffer->data;
    }
    if (!ssz_get_def(ctx->execution_payload.def, name)) return NULL_BYTES;
  }
  if (strcmp(name, EL_WITHDRAWALS_ROOT) == 0) {
    if (ssz_get_def(ctx->execution_payload.def, name)) {
      ssz_ob_t from_payload = ssz_get(&ctx->execution_payload, name);
      if (from_payload.bytes.len == 32) return from_payload.bytes;
    }
    buffer_reset(buffer);
    buffer_grow(buffer, 32);
    buffer->data.len = 32;
    eth_get_withdrawals_root(buffer->data.data, ssz_get(&ctx->execution_payload, "withdrawals"));
    return buffer->data;
  }
  if (strcmp(name, EL_TRANSACTIONS_ROOT) == 0) {
    buffer_reset(buffer);
    buffer_grow(buffer, 32);
    buffer->data.len = 32;
    eth_get_transactions_root(buffer->data.data, ssz_get(&ctx->execution_payload, "transactions"));
    return buffer->data;
  }
  if (strcmp(name, EL_BLOCK_ACCESS_LIST_HASH) == 0) {
    bytes_t bal = NULL_BYTES;
    if (ssz_get_def(ctx->execution_payload.def, "blockAccessList"))
      bal = ssz_get(&ctx->execution_payload, "blockAccessList").bytes;
    buffer_reset(buffer);
    buffer_grow(buffer, 32);
    buffer->data.len = 32;
    eth_get_block_access_list_hash(buffer->data.data, bal);
    return buffer->data;
  }

  ssz_ob_t field = ssz_get(&ctx->execution_payload, name);
  if (field.def == NULL) return NULL_BYTES;
  if (field.def->type == SSZ_TYPE_UINT) {
    buffer_reset(buffer);
    buffer_grow(buffer, field.bytes.len);
    for (int i = 0; i < field.bytes.len; i++) buffer->data.data[i] = field.bytes.data[field.bytes.len - i - 1];
    buffer->data.len = field.bytes.len;
    return buffer->data;
  }
  return field.bytes;
}

c4_status_t eth_el_header_build_from_ep(bytes_t* el_header, eth_el_header_ctx_t* ctx) {
  return eth_el_header_build(ctx->state, el_header, ctx->fork, ctx, get_from_ep);
}

static bytes_t get_from_json(void* data, buffer_t* buffer, char* name) {
  json_t* json  = (json_t*) data;
  json_t  field = json_get(*json, name);
  // some fields use execution-payload naming internally, but standard JSON-RPC block
  // responses (eth_getBlockByHash etc.) use the legacy header names: fall back to those.
  if (field.type == JSON_TYPE_NOT_FOUND) {
    if (strcmp(name, EL_FEE_RECIPIENT) == 0)
      field = json_get(*json, "miner");
    else if (strcmp(name, EL_PREV_RANDAO) == 0)
      field = json_get(*json, "mixHash");
    else if (strcmp(name, EL_BLOCK_NUMBER) == 0)
      field = json_get(*json, "number");
  }
  if (field.type == JSON_TYPE_NOT_FOUND) return NULL_BYTES;
  return json_as_bytes(field, buffer);
}

c4_status_t eth_el_header_build_from_json(c4_state_t* state, bytes_t* el_header, fork_id_t fork, json_t block) {
  return eth_el_header_build(state, el_header, fork, &block, get_from_json);
}

bytes_t eth_el_header_get(bytes_t header, char* name) {
  int count = sizeof(el_header_field_names) / sizeof(el_header_field_names[0]);
  for (int i = 0; i < count; i++) {
    char* field_name = el_header_field_names[i] + 2;
    if (strcmp(name, field_name) == 0) {
      if (rlp_decode(&header, 0, &header) != RLP_LIST) return NULL_BYTES;
      if (rlp_decode(&header, i, &header) != RLP_ITEM) return NULL_BYTES;
      return header;
    }
  }
  return NULL_BYTES;
}

uint64_t eth_el_header_get_uint64(bytes_t header, char* name) {
  bytes_t value = eth_el_header_get(header, name);
  if (value.len == 0) return 0;
  return bytes_as_be(value);
}

c4_status_t eth_el_header_get_from_raw_block(c4_state_t* state, bytes_t raw_block, bytes_t* el_header, ssz_builder_t* body_builder) {
  buffer_t buffer       = {0};
  bytes_t  transactions = NULL_BYTES;
  bytes_t  withdrawals  = NULL_BYTES;

  if (rlp_decode(&raw_block, 0, &raw_block) != RLP_LIST) return c4_state_add_error(state, "Invalid RLP list");
  if (rlp_decode(&raw_block, 0, el_header) != RLP_LIST) return c4_state_add_error(state, "Invalid RLP header");
  if (rlp_decode(&raw_block, 1, &transactions) != RLP_LIST) return c4_state_add_error(state, "Invalid RLP transactions");
  if (rlp_decode(&raw_block, 3, &withdrawals) != RLP_LIST) return c4_state_add_error(state, "Invalid RLP withdrawals");

  // encode as list for the raw block header
  buffer_append(&buffer, *el_header);
  rlp_to_list(&buffer);
  *el_header = buffer.data;

  ssz_builder_t tx_builder          = ssz_builder_for_def(ssz_get_def(body_builder->def, "transactions"));
  ssz_builder_t withdrawals_builder = ssz_builder_for_def(ssz_get_def(body_builder->def, "withdrawals"));
  buffer_t      tx_buffer           = {0};
  int           tx_count            = rlp_decode(&transactions, -1, &transactions);
  int           withdrawals_count   = rlp_decode(&withdrawals, -1, &withdrawals);

  for (int i = 0; i < tx_count; i++) {
    bytes_t tx = NULL_BYTES;
    if (rlp_decode(&transactions, i, &tx) == RLP_LIST) { // legacy transactions are encoded as list
      buffer_reset(&tx_buffer);
      buffer_append(&tx_buffer, tx);
      rlp_to_list(&tx_buffer);
      tx = tx_buffer.data;
    }
    ssz_add_dynamic_list_bytes(&tx_builder, tx_count, tx);
  }

  for (int i = 0; i < withdrawals_count; i++) {
    uint8_t       val[8]         = {0};
    ssz_builder_t builder        = ssz_builder_for_def(withdrawals_builder.def->def.vector.type);
    bytes_t       w              = {0};
    bytes_t       index          = {0};
    bytes_t       validatorIndex = {0};
    bytes_t       address        = {0};
    bytes_t       amount         = {0};
    rlp_decode(&withdrawals, i, &w);
    rlp_decode(&w, 0, &index);
    rlp_decode(&w, 1, &validatorIndex);
    rlp_decode(&w, 2, &address);
    rlp_decode(&w, 3, &amount);
    if (index.len > 8 || validatorIndex.len > 8 || amount.len > 8) {
      buffer_free(&tx_builder.fixed);
      buffer_free(&tx_builder.dynamic);
      buffer_free(&withdrawals_builder.fixed);
      buffer_free(&withdrawals_builder.dynamic);
      buffer_free(&tx_buffer);
      buffer_free(&buffer);
      return c4_state_add_error(state, "Invalid RLP length");
    }

    memcpy(val + 8 - index.len, index.data, index.len);
    ssz_add_uint64(&builder, uint64_from_be(val));
    memset(val, 0, 8);
    memcpy(val + 8 - validatorIndex.len, validatorIndex.data, validatorIndex.len);
    ssz_add_uint64(&builder, uint64_from_be(val));
    ssz_add_bytes(&builder, "address", address);
    memset(val, 0, 8);
    memcpy(val + 8 - amount.len, amount.data, amount.len);
    ssz_add_uint64(&builder, uint64_from_be(val));
    ssz_add_dynamic_list_builders(&withdrawals_builder, withdrawals_count, builder);
  }

  ssz_add_builders(body_builder, "transactions", tx_builder);
  ssz_add_builders(body_builder, "withdrawals", withdrawals_builder);

  buffer_free(&tx_buffer);
  return C4_SUCCESS;
}
