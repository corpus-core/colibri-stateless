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
static const char* el_header_field_names[] = {
    "H:parentHash",            // 0
    "H:sha3Uncles",            // 1
    "A:feeRecipient",          // 2
    "H:stateRoot",             // 3
    "H:transactionsRoot",      // 4
    "H:receiptsRoot",          // 5
    "B:logsBloom",             // 6
    "U:difficulty",            // 7
    "U:blockNumber",           // 8
    "U:gasLimit",              // 9
    "U:gasUsed",               // 10
    "U:timestamp",             // 11
    "B:extraData",             // 12
    "H:prevRandao",            // 13
    "B:nonce",                 // 14
    "U:baseFeePerGas",         // 15
    "H:withdrawalsRoot",       // 16
    "U:blobGasUsed",           // 17
    "U:excessBlobGas",         // 18
    "H:parentBeaconBlockRoot", // 19
    "H:requestsHash",          // 20
    "H:blockAccessListHash",   // 21
    "U:slotNumber",            // 22
};

const char* EMPTY_RLP_LIST = "\x1d\xcc\x4d\xe8\xde\xc7\x5d\x7a\xab\x85\xb5\x67\xb6\xcc\xd4\x1a\xd3\x12\x45\x1b\x94\x8a\x74\x13\xf0\xa1\x42\xfd\x40\xd4\x93\x47";

static c4_status_t eth_el_header_build(c4_state_t* state, bytes_t* el_header, fork_id_t fork, void* data, el_header_field_func get_field_func) {
  buffer_t buffer = {0};
  buffer_t result = {0};
  int      count  = sizeof(el_header_field_names) / sizeof(el_header_field_names[0]);
  if (fork < C4_FORK_GLOAS) count -= 2;   // no blockAccessListHash and slotNumber before Gloas
  if (fork < C4_FORK_ELECTRA) count -= 1; // no requestsHash before Prague/Electra

  for (int i = 0; i < count; i++) {
    char    type       = el_header_field_names[i][0];
    char*   field_name = el_header_field_names[i] + 2;
    bytes_t value      = {0};
    if (strcmp(field_name, "sha3Uncles") == 0)
      value = bytes(EMPTY_RLP_LIST, 32);
    else if (strcmp(field_name, "difficulty") == 0)
      value = bytes("\x00", 1);
    else if (strcmp(field_name, "nonce") == 0)
      value = bytes("\x00\x00\x00\x00\x00\x00\x00\x00", 8);
    else
      value = get_field_func(data, &buffer, field_name);
    switch (type) {
      case 'H':
      case 'A': {
        bytes32_t data     = {0};
        int       must_len = type == 'H' ? 32 : 20;
        int       copy_len = must_len < value.len ? must_len : value.len;
        memcpy(data + must_len - copy_len, value.data, copy_len);
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
  static const char* request_lists[] = {"deposits", "withdrawals", "consolidations"};
  uint8_t            hashes[sizeof(request_lists) / sizeof(request_lists[0]) * 32]; // one intermediate hash per non-empty request list
  uint32_t           hashes_len         = 0;
  ssz_ob_t           body               = ssz_get(&ctx->beacon_block, "body");
  ssz_ob_t           execution_requests = ssz_get(&body, "executionRequests");
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
static void get_withdrawals_root(bytes32_t out_hash, eth_el_header_ctx_t* ctx) {
  ssz_ob_t withdrawals = ssz_get(&ctx->execution_payload, "withdrawals");
  uint32_t len         = ssz_len(withdrawals);

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
    ssz_ob_t w       = ssz_at(withdrawals, i);
    ssz_ob_t address = ssz_get(&w, "address");
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

// computes the withdrawalsRoot as defined in EIP-4895: a Merkle Patricia Trie built over
// all withdrawals of the execution payload. The key for withdrawal i is the RLP-encoded
// index (same encoding as the transactions trie), the value is the RLP-encoded list
// [index, validatorIndex, address, amount] (all uint64 except address which is 20 bytes).
static void get_transactions_root(bytes32_t out_hash, eth_el_header_ctx_t* ctx) {
  ssz_ob_t txs = ssz_get(&ctx->execution_payload, "transactions");
  uint32_t len         = ssz_len(txs);

  if (len == 0) {
    memcpy(out_hash, EMPTY_ROOT_HASH, 32);
    return;
  }

  bytes32_t path_tmp = {0};
  buffer_t  path_buf = stack_buffer(path_tmp);
  node_t*   root      = NULL;

  for (uint32_t i = 0; i < len; i++) {
    ssz_ob_t tx       = ssz_at(txs, i);
    patricia_set_value(&root, c4_eth_create_tx_path(i, &path_buf), tx.bytes);
  }

  memcpy(out_hash, patricia_get_root(root).data, 32);
  patricia_node_free(root);
}

static bytes_t get_from_ep(void* data, buffer_t* buffer, char* name) {
  eth_el_header_ctx_t* ctx = (eth_el_header_ctx_t*) data;
  if (strcmp(name, "parentBeaconBlockRoot") == 0)
    return bytes(ctx->parent_root, 32);
    if (strcmp(name, "requestsHash") == 0) {
      buffer_reset(buffer);
      buffer_grow(buffer, 32);
      buffer->data.len = 32;
      get_requests_hash(buffer->data.data, ctx);
      return buffer->data;
    }
    if (strcmp(name, "withdrawalsRoot") == 0) {
      buffer_reset(buffer);
      buffer_grow(buffer, 32);
      buffer->data.len = 32;
      get_withdrawals_root(buffer->data.data, ctx);
      return buffer->data;
    }
    if (strcmp(name, "transactionsRoot") == 0) {
      buffer_reset(buffer);
      buffer_grow(buffer, 32);
      buffer->data.len = 32;
      get_transactions_root(buffer->data.data, ctx);
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