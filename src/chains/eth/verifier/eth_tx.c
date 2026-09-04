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

#include "eth_tx.h"
#include "beacon_types.h"
#include "bytes.h"
#include "crypto.h"
#include "el_header.h"
#include "json.h"
#include "patricia.h"
#include "rlp.h"
#include "ssz.h"
#include "sync_committee.h"
#include "verify.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
  TX_TYPE_LEGACY    = 0,
  TX_TYPE_EIP2930   = 1,
  TX_TYPE_EIP1559   = 2,
  TX_TYPE_EIP4844   = 3,
  TX_TYPE_EIP7702   = 4,
  TX_TYPE_DEPOSITED = 0x7E, // Optimism Deposited Transaction
} tx_type_t;

typedef struct {
  const char* name;
  uint32_t    len; // -1 : uint with minimal length, 0 : uint with dynamic length, other values are fixed lengths, -20 : optional address
} rlp_def_t;

typedef struct {
  const rlp_def_t* defs;
  uint32_t         len;
} rlp_type_defs_t;

static const rlp_def_t tx_legacy_defs[] = {
    {"nonce", 1},
    {"gasPrice", 1},
    {"gas", 1},
    {"to", 20},
    {"value", 1},
    {"input", 0},
    {"v", 1},
    {"r", 1},
    {"s", 1},
};

static const rlp_def_t tx_1_defs[] = {
    {"chainId", 1},
    {"nonce", 1},
    {"gasPrice", 1},
    {"gas", 1},
    {"to", 20},
    {"value", 1},
    {"input", 0},
    {"accessList", 2},
    {"yParity", 1},
    {"r", 1},
    {"s", 1},
};

static const rlp_def_t tx_2_defs[] = {
    {"chainId", 1},
    {"nonce", 1},
    {"maxPriorityFeePerGas", 1},
    {"maxFeePerGas", 1},
    {"gas", 1},
    {"to", 20},
    {"value", 1},
    {"input", 0},
    {"accessList", 2},
    {"yParity", 1},
    {"r", 1},
    {"s", 1},
};

static const rlp_def_t tx_type3_defs[] = {
    {"chainId", 1},
    {"nonce", 1},
    {"maxPriorityFeePerGas", 1},
    {"maxFeePerGas", 1},
    {"gas", 1},
    {"to", 20},
    {"value", 1},
    {"input", 0},
    {"accessList", 2},
    {"maxFeePerBlobGas", 1},
    {"blobVersionedHashes", 2},
    {"yParity", 1},
    {"r", 1},
    {"s", 1},
};

static const rlp_def_t tx_eip7702_defs[] = {
    {"chainId", 1},
    {"nonce", 1},
    {"maxPriorityFeePerGas", 1},
    {"maxFeePerGas", 1},
    {"gas", 1},
    {"to", 20},
    {"value", 1},
    {"input", 0},
    {"accessList", 2},
    {"authorizationList", 2},
    {"yParity", 1},
    {"r", 1},
    {"s", 1},
};

// Optimism Deposited Transaction (0x7E) - RLP encoded according to spec
// Fields in order: sourceHash, from, to, mint, value, gas, isSystemTx, data
static const rlp_def_t tx_deposited_defs[] = {
    {"sourceHash", 32}, // bytes32 sourceHash - uniquely identifies the origin of the deposit
    {"from", 20},       // address from - sender account address
    {"to", -20},        // address to - recipient account (or null for contract creation)
    {"mint", 1},        // uint256 mint - ETH value to mint on L2
    {"value", 1},       // uint256 value - ETH value to send to recipient
    {"gas", 1},         // uint64 gas - gas limit for L2 transaction
    {"isSystemTx", 1},  // bool isSystemTx - system transaction flag (disabled after Regolith)
    {"input", 0},       // bytes data - calldata
};

static const rlp_type_defs_t tx_type_defs[] = {
    {tx_legacy_defs, sizeof(tx_legacy_defs) / sizeof(rlp_def_t)},
    {tx_1_defs, sizeof(tx_1_defs) / sizeof(rlp_def_t)},
    {tx_2_defs, sizeof(tx_2_defs) / sizeof(rlp_def_t)},
    {tx_type3_defs, sizeof(tx_type3_defs) / sizeof(rlp_def_t)},
    {tx_eip7702_defs, sizeof(tx_eip7702_defs) / sizeof(rlp_def_t)},
};

// Get RLP type definitions for a given transaction type
static const rlp_type_defs_t* get_tx_type_defs(tx_type_t type) {
  switch (type) {
    case TX_TYPE_LEGACY: return &tx_type_defs[0];
    case TX_TYPE_EIP2930: return &tx_type_defs[1];
    case TX_TYPE_EIP1559: return &tx_type_defs[2];
    case TX_TYPE_EIP4844: return &tx_type_defs[3];
    case TX_TYPE_EIP7702: return &tx_type_defs[4];
    case TX_TYPE_DEPOSITED: {
      static const rlp_type_defs_t deposited_defs = {tx_deposited_defs, sizeof(tx_deposited_defs) / sizeof(rlp_def_t)};
      return &deposited_defs;
    }
    default: return NULL;
  }
}

static bool get_and_remove_tx_type(verify_ctx_t* ctx, bytes_t* raw_tx, tx_type_t* type) {
  if (raw_tx->len < 1) RETURN_VERIFY_ERROR(ctx, "invalid tx data, missing type!");
  *type = raw_tx->data[0];

  if (*type >= 0x7f)
    *type = TX_TYPE_LEGACY; // legacy tx
  else {
    // Check if transaction type is supported by trying to get its definitions
    if (!get_tx_type_defs(*type)) RETURN_VERIFY_ERROR(ctx, "unsupported transaction type!");
    raw_tx->data++;
    raw_tx->len--;
  }
  return true;
}

INTERNAL bool c4_tx_create_from_address(verify_ctx_t* ctx, bytes_t raw_tx, uint8_t* address) {
  buffer_t  buf      = {0};
  bytes32_t raw_hash = {0};
  bytes_t   last_item;
  tx_type_t type = 0;
  if (!get_and_remove_tx_type(ctx, &raw_tx, &type)) RETURN_VERIFY_ERROR(ctx, "invalid tx data, missing type!");
  if (rlp_decode(&raw_tx, 0, &raw_tx) != RLP_LIST) RETURN_VERIFY_ERROR(ctx, "invalid tx data!");
  const rlp_type_defs_t* defs_ptr = get_tx_type_defs(type);
  if (!defs_ptr) RETURN_VERIFY_ERROR(ctx, "unsupported transaction type");
  rlp_type_defs_t defs = *defs_ptr;
  if (type == TX_TYPE_DEPOSITED) {
    bytes_t from = {0};
    if (rlp_decode(&raw_tx, 1, &from) != RLP_ITEM || from.len != 20) RETURN_VERIFY_ERROR(ctx, "invalid tx data, invalid from address!");
    memcpy(address, from.data, 20);
    return true;
  }
  rlp_decode(&raw_tx, defs.len - 4, &last_item);
  buffer_append(&buf, bytes(raw_tx.data, last_item.data + last_item.len - raw_tx.data));
  uint64_t v = 0;
  if (type == TX_TYPE_LEGACY) {
    v = rlp_get_uint64(raw_tx, 6);
    if (v > 28) {
      rlp_add_uint64(&buf, (v - 36 + v % 2) / 2);
      rlp_add_item(&buf, NULL_BYTES);
      rlp_add_item(&buf, NULL_BYTES);
    }
  }
  else
    v = rlp_get_uint64(raw_tx, defs.len - 3);

  rlp_to_list(&buf);

  if (type != TX_TYPE_LEGACY) {
    buffer_splice(&buf, 0, 0, bytes(NULL, 1));
    buf.data.data[0] = (uint8_t) type;
  }
  keccak(buf.data, raw_hash);
  buffer_free(&buf);

  //  if (type == TX_TYPE_EIP4844) RETURN_VERIFY_ERROR(ctx, "invalid tx data, EIP4844 not supported (yet)!");

  uint8_t sig[65]    = {0};
  uint8_t pubkey[64] = {0};
  rlp_decode(&raw_tx, defs.len - 2, &last_item);
  memcpy(sig + 32 - last_item.len, last_item.data, last_item.len);
  rlp_decode(&raw_tx, defs.len - 1, &last_item);
  memcpy(sig + 64 - last_item.len, last_item.data, last_item.len);
  sig[64] = (uint8_t) (v > 28 ? (v % 2 ? 27 : 28) : v);

  if (!secp256k1_recover(raw_hash, bytes(sig, 65), pubkey)) RETURN_VERIFY_ERROR(ctx, "invalid  signature!");

  keccak(bytes(pubkey, 64), sig);
  memcpy(address, sig + 12, 20);

  return true;
}

INTERNAL bool c4_tx_verify_tx_hash(verify_ctx_t* ctx, bytes_t raw) {
  if (ctx->method == NULL) return true;
  if (strcmp(ctx->method, "eth_getTransactionByHash") == 0 || strcmp(ctx->method, "eth_getTransactionReceipt") == 0) {
    json_t expected_hash = json_at(ctx->args, 0);
    if (expected_hash.type != JSON_TYPE_STRING || expected_hash.len > 68) RETURN_VERIFY_ERROR(ctx, "invalid transaction hash!");
    bytes32_t tmp             = {0};
    bytes32_t calculated_hash = {0};
    buffer_t  buf             = stack_buffer(tmp);
    bytes_t   expected        = json_as_bytes(expected_hash, &buf);
    keccak(raw, calculated_hash);
    if (expected.len != 32 || memcmp(expected.data, calculated_hash, 32) != 0) RETURN_VERIFY_ERROR(ctx, "invalid transaction hash!");
    return true;
  }
  RETURN_VERIFY_ERROR(ctx, "invalid method for tx proof!");
}

static bool matches(ssz_ob_t log, bytes_t logs) {
  bytes_t val = {0};
  if (rlp_decode(&logs, 0, &val) != RLP_ITEM || !bytes_eq(val, ssz_get(&log, "address").bytes)) return false;
  if (rlp_decode(&logs, 2, &val) != RLP_ITEM || !bytes_eq(val, ssz_get(&log, "data").bytes)) return false;

  log = ssz_get(&log, "topics");
  if (rlp_decode(&logs, 1, &logs) != RLP_LIST) return false;
  if (ssz_len(log) != rlp_decode(&logs, -1, &logs)) return false;
  for (uint32_t topic_index = 0; topic_index < ssz_len(log); topic_index++) {
    if (rlp_decode(&logs, topic_index, &val) != RLP_ITEM || !bytes_eq(val, ssz_at(log, topic_index).bytes)) return false;
  }

  return true;
}

INTERNAL bool c4_tx_verify_log_data(verify_ctx_t* ctx, ssz_ob_t log, bytes32_t block_hash, uint64_t block_number, uint32_t tx_index, bytes_t tx_raw, bytes_t receipt_raw) {
  bytes32_t tx_hash = {0};
  bytes_t   val     = {0};
  bytes_t   logs    = {0};
  tx_type_t type    = 0;
  keccak(tx_raw, tx_hash);
  if (!bytes_eq(bytes(tx_hash, 32), ssz_get(&log, "transactionHash").bytes)) RETURN_VERIFY_ERROR(ctx, "invalid transaction hash!");
  if (block_number != ssz_get_uint64(&log, "blockNumber")) RETURN_VERIFY_ERROR(ctx, "invalid block number!");
  if (!bytes_eq(ssz_get(&log, "blockHash").bytes, bytes(block_hash, 32))) RETURN_VERIFY_ERROR(ctx, "invalid block hash!");
  if (tx_index != ssz_get_uint32(&log, "transactionIndex")) RETURN_VERIFY_ERROR(ctx, "invalid transaction index!");
  if (!get_and_remove_tx_type(ctx, &receipt_raw, &type)) RETURN_VERIFY_ERROR(ctx, "invalid tx data, invalid type!");
  if (rlp_decode(&receipt_raw, 0, &receipt_raw) != RLP_LIST || rlp_decode(&receipt_raw, 3, &logs) != RLP_LIST) RETURN_VERIFY_ERROR(ctx, "invalid to data!");
  uint32_t logs_len = rlp_decode(&logs, -1, &logs);

  for (uint32_t i = 0; i < logs_len; i++) {
    bytes_t log_rlp = {0};
    rlp_decode(&logs, i, &log_rlp);
    if (matches(log, log_rlp)) return true;
  }
  RETURN_VERIFY_ERROR(ctx, "missing the log within the tx");
}

INTERNAL bool c4_tx_verify_receipt_data(verify_ctx_t* ctx, ssz_ob_t receipt_data, bytes32_t block_hash, uint64_t block_number, uint32_t tx_index, bytes_t tx_raw, bytes_t receipt_raw) {
  bytes32_t tmp     = {0};
  tx_type_t type    = 0;
  bytes_t   val     = {0};
  bytes32_t tx_hash = {0};
  keccak(tx_raw, tx_hash);
  // from
  if (!c4_tx_create_from_address(ctx, tx_raw, tmp) || memcmp(tmp, ssz_get(&receipt_data, "from").bytes.data, 20)) RETURN_VERIFY_ERROR(ctx, "invalid tx data, wrong from address!");
  // type
  if (!get_and_remove_tx_type(ctx, &tx_raw, &type) && type != (uint8_t) ssz_get_uint32(&receipt_data, "type")) RETURN_VERIFY_ERROR(ctx, "invalid tx data, invalid type!");
  // to
  int to_idx = type == TX_TYPE_DEPOSITED ? 2 : (type == TX_TYPE_EIP4844 ? 5 : min32(5, 3 + type));
  if (rlp_decode(&tx_raw, 0, &tx_raw) != RLP_LIST || rlp_decode(&tx_raw, to_idx, &val) != RLP_ITEM || !bytes_eq(val, ssz_get(&receipt_data, "to").bytes)) RETURN_VERIFY_ERROR(ctx, "invalid to address!");
  if (block_number != ssz_get_uint64(&receipt_data, "blockNumber")) RETURN_VERIFY_ERROR(ctx, "invalid block number!");
  if (!bytes_eq(ssz_get(&receipt_data, "blockHash").bytes, bytes(block_hash, 32))) RETURN_VERIFY_ERROR(ctx, "invalid block hash!");
  if (tx_index != ssz_get_uint32(&receipt_data, "transactionIndex")) RETURN_VERIFY_ERROR(ctx, "invalid transaction index!");
  if (!bytes_eq(bytes(tx_hash, 32), ssz_get(&receipt_data, "transactionHash").bytes)) RETURN_VERIFY_ERROR(ctx, "invalid transaction hash!");

  if (type != TX_TYPE_LEGACY) {
    if (receipt_raw.len < 1 || receipt_raw.data[0] != type) RETURN_VERIFY_ERROR(ctx, "invalid type!");
    receipt_raw.data++;
    receipt_raw.len--;
  }

  if (rlp_decode(&receipt_raw, 0, &tx_raw) != RLP_LIST) RETURN_VERIFY_ERROR(ctx, "invalid receipt data!");
  if (rlp_decode(&tx_raw, 0, &val) != RLP_ITEM || bytes_as_be(val) != ssz_get_uint64(&receipt_data, "status")) RETURN_VERIFY_ERROR(ctx, "invalid receipt data!");
  if (rlp_decode(&tx_raw, 1, &val) != RLP_ITEM || bytes_as_be(val) != ssz_get_uint64(&receipt_data, "cumulativeGasUsed")) RETURN_VERIFY_ERROR(ctx, "invalid receipt data!");
  if (rlp_decode(&tx_raw, 2, &val) != RLP_ITEM || !bytes_eq(val, ssz_get(&receipt_data, "logsBloom").bytes)) RETURN_VERIFY_ERROR(ctx, "invalid receipt data!");
  if (type == TX_TYPE_DEPOSITED) {
    if (rlp_decode(&tx_raw, 4, &val) != RLP_ITEM || bytes_as_be(val) != ssz_get_uint64(&receipt_data, "depositNonce")) RETURN_VERIFY_ERROR(ctx, "invalid receipt data!");
    if (rlp_decode(&tx_raw, 5, &val) != RLP_ITEM || bytes_as_be(val) != ssz_get_uint64(&receipt_data, "depositReceiptVersion")) RETURN_VERIFY_ERROR(ctx, "invalid receipt data!");
  }
  if (rlp_decode(&tx_raw, 3, &tx_raw) != RLP_LIST) RETURN_VERIFY_ERROR(ctx, "invalid receipt data!");
  ssz_ob_t logs = ssz_get(&receipt_data, "logs");
  if (ssz_len(logs) != rlp_decode(&tx_raw, -1, &tx_raw)) RETURN_VERIFY_ERROR(ctx, "invalid log len!");

  for (uint32_t log_index = 0; log_index < ssz_len(logs); log_index++) {
    ssz_ob_t log     = ssz_at(logs, log_index);
    bytes_t  log_rlp = {0};
    if (rlp_decode(&tx_raw, log_index, &log_rlp) != RLP_LIST) RETURN_VERIFY_ERROR(ctx, "invalid receipt data!");
    if (rlp_decode(&log_rlp, 0, &val) != RLP_ITEM || !bytes_eq(val, ssz_get(&log, "address").bytes)) RETURN_VERIFY_ERROR(ctx, "invalid receipt data!");
    if (rlp_decode(&log_rlp, 2, &val) != RLP_ITEM || !bytes_eq(val, ssz_get(&log, "data").bytes)) RETURN_VERIFY_ERROR(ctx, "invalid receipt data!");
    if (block_number != ssz_get_uint64(&log, "blockNumber")) RETURN_VERIFY_ERROR(ctx, "invalid block number!");
    if (!bytes_eq(ssz_get(&log, "blockHash").bytes, bytes(block_hash, 32))) RETURN_VERIFY_ERROR(ctx, "invalid block hash!");

    log = ssz_get(&log, "topics");
    if (rlp_decode(&log_rlp, 1, &log_rlp) != RLP_LIST) RETURN_VERIFY_ERROR(ctx, "invalid topics!");
    if (ssz_len(log) != rlp_decode(&log_rlp, -1, &log_rlp)) RETURN_VERIFY_ERROR(ctx, "invalid topic len!");
    for (uint32_t topic_index = 0; topic_index < ssz_len(log); topic_index++) {
      if (rlp_decode(&log_rlp, topic_index, &val) != RLP_ITEM || !bytes_eq(val, ssz_at(log, topic_index).bytes)) RETURN_VERIFY_ERROR(ctx, "invalid topic data!");
    }
  }

  return true;
}

INTERNAL bytes_t c4_eth_create_tx_path(uint32_t tx_index, buffer_t* buf) {
  bytes32_t tmp     = {0};
  buffer_t  val_buf = stack_buffer(tmp);
  bytes_t   path    = {.data = buf->data.data, .len = 0};

  // create_path
  if (tx_index > 0) {
    buffer_add_be(&val_buf, tx_index, 4);
    path = bytes_remove_leading_zeros(bytes(tmp, 4));
  }
  buf->data.len = 0;
  rlp_add_item(buf, path);
  return buf->data;
}

INTERNAL bool c4_verify_mpt_proof(verify_ctx_t* ctx, ssz_ob_t proof, uint32_t tx_index, bytes32_t expected_root, bytes_t* raw_value) {
  bytes32_t tmp             = {0};
  bytes32_t calculated_root = {0};
  buffer_t  path_buf        = stack_buffer(tmp);

  if (patricia_verify(calculated_root, c4_eth_create_tx_path(tx_index, &path_buf), proof, raw_value) != PATRICIA_FOUND)
    RETURN_VERIFY_ERROR(ctx, "invalid account proof on execution layer!");
  if (memcmp(calculated_root, expected_root, 32) != 0) RETURN_VERIFY_ERROR(ctx, "invalid merkle root!");
  return true;
}

// Returns the decoded bytes. On error, adds message to ctx->state and returns NULL_BYTES.
static bytes_t get_rlp_field(verify_ctx_t* ctx, bytes_t rlp_list, const rlp_type_defs_t* defs, const char* field_name, rlp_type_t expected_type) {
  bytes_t result_bytes = NULL_BYTES;
  for (int i = 0; i < defs->len; i++) {
    if (strcmp(defs->defs[i].name, field_name)) continue;
    rlp_type_t decoded_type = rlp_decode(&rlp_list, i, &result_bytes);

    // Check if decoded type matches expected type
    if (decoded_type != expected_type) {
      char     err_buf[120];
      buffer_t tmp_buf = stack_buffer(err_buf);
      if (decoded_type <= RLP_SUCCESS) { // Includes RLP_SUCCESS, RLP_OUT_OF_RANGE, RLP_NOT_FOUND
        bprintf(&tmp_buf, "RLP decode failed or type mismatch for field '%s': expected type %d, decode result %d", field_name, expected_type, decoded_type);
      }
      else { // Decoded type is RLP_ITEM or RLP_LIST but not the expected one
        bprintf(&tmp_buf, "RLP type mismatch for field '%s': expected %d, got %d", field_name, expected_type, decoded_type);
      }
      c4_state_add_error(&ctx->state, err_buf);
      return NULL_BYTES; // Return NULL_BYTES on error
    }
    // Success
    return result_bytes;
  }
  return NULL_BYTES; // Return NULL_BYTES on error
}

// Forward declaration or placed before c4_write_tx_data_from_raw
static ssz_builder_t build_access_list_ssz(verify_ctx_t* ctx, bytes_t rlp_access_list_field, const ssz_def_t* access_list_ssz_def);
static ssz_builder_t build_authorization_list_ssz(verify_ctx_t* ctx, bytes_t rlp_auth_list_field, const ssz_def_t* auth_list_ssz_def);
static bytes_t       build_blob_hashes_from_rlp(verify_ctx_t* ctx, bytes_t rlp_list, const rlp_type_defs_t* defs_ptr, tx_type_t type);

static bytes_t build_blob_hashes_from_rlp(verify_ctx_t* ctx, bytes_t rlp_list, const rlp_type_defs_t* defs_ptr, tx_type_t type) {
  bytes_t blob_hashes = {0};
  if (type != TX_TYPE_EIP4844) {
    return blob_hashes; // Return empty bytes if not EIP-4844
  }

  bytes_t inner_list = get_rlp_field(ctx, rlp_list, defs_ptr, "blobVersionedHashes", RLP_LIST);
  if (ctx->state.error != NULL) {
    return NULL_BYTES; // Error already set by get_rlp_field
  }

  // If inner_list.data is NULL (e.g. field not found but expected RLP_LIST), treat as no hashes or handle as error if mandatory.
  // For EIP-4844, blobVersionedHashes is a list, can be empty.
  if (inner_list.len == 0 || inner_list.data == NULL) {
    return blob_hashes; // Empty list of hashes
  }

  int num_hashes = rlp_decode(&inner_list, -1, NULL);
  if (num_hashes < 0) { // rlp_decode can return negative on error, or 0 for empty list payload
    c4_state_add_error(&ctx->state, "build_blob_hashes_from_rlp: Invalid RLP for blob hashes count");
    return NULL_BYTES;
  }
  if (num_hashes > 16) { // Max 16 hashes as per EIP-4844 spec (usually 1 to 6, but spec says up to MAX_BLOBS_PER_BLOCK)
    char     err_buf[100];
    buffer_t tmp_buf = stack_buffer(err_buf);
    bprintf(&tmp_buf, "build_blob_hashes_from_rlp: Too many blob hashes %d (max 16)", num_hashes);
    c4_state_add_error(&ctx->state, err_buf);
    return NULL_BYTES;
  }

  if (num_hashes > 0) {
    blob_hashes.data = safe_malloc(num_hashes * 32);
    blob_hashes.len  = num_hashes * 32;
    for (int h = 0; h < num_hashes; ++h) {
      bytes_t hash_item = {0};
      // inner_list was already modified by rlp_decode for count, now use it for items.
      if (rlp_decode(&inner_list, h, &hash_item) != RLP_ITEM || hash_item.len != 32) {
        safe_free(blob_hashes.data);
        c4_state_add_error(&ctx->state, "build_blob_hashes_from_rlp: Invalid blob hash item in RLP list");
        return NULL_BYTES;
      }
      memcpy(blob_hashes.data + h * 32, hash_item.data, 32);
    }
  }
  return blob_hashes;
}

static ssz_builder_t build_access_list_ssz(verify_ctx_t* ctx, bytes_t rlp_access_list_field, const ssz_def_t* access_list_ssz_def) {
  ssz_builder_t access_list_builder = ssz_builder_for_def(access_list_ssz_def);
  if (!access_list_ssz_def) {
    c4_state_add_error(&ctx->state, "build_access_list_ssz: NULL SSZ definition provided");
    return (ssz_builder_t) {0}; // Return empty/invalid builder
  }

  // If rlp_access_list_field.data is NULL (e.g. from get_rlp_field if field not found, but type was RLP_LIST)
  // or if len is 0, it's an empty list, which is valid. The builder will be empty.
  if (rlp_access_list_field.len > 0 && rlp_access_list_field.data != NULL) {
    bytes_t current_rlp_list_data = rlp_access_list_field;                        // Work on a copy if rlp_decode modifies it.
    int     entries               = rlp_decode(&current_rlp_list_data, -1, NULL); // current_rlp_list_data is modified here
    if (entries < 0) {
      c4_state_add_error(&ctx->state, "build_access_list_ssz: Failed to decode number of access list entries");
      ssz_builder_free(&access_list_builder);
      return (ssz_builder_t) {0};
    }

    const ssz_def_t* entry_ssz_def = access_list_ssz_def->def.vector.type;

    for (int i = 0; i < entries; i++) {
      ssz_builder_t entry_builder = ssz_builder_for_def(entry_ssz_def);
      bytes_t       entry_rlp     = {0};
      bytes_t       address_rlp   = {0};
      bytes_t       keys_rlp      = {0};

      // Use rlp_access_list_field for decoding individual items, as current_rlp_list_data points to the *content* of the list after the count.
      // No, rlp_decode takes a pointer and updates it. So current_rlp_list_data is correct.
      // The previous code was: rlp_decode(&access_list_rlp, i, &entry_rlp); access_list_rlp was the top-level list.
      // If rlp_decode(list, index, out) is used, then current_rlp_list_data is the *list itself*.
      // Let's stick to the original pattern of decoding from the outer list for each index.
      bytes_t outer_list_for_decode = rlp_access_list_field; // Use the original RLP list field bytes for indexed access

      if (rlp_decode(&outer_list_for_decode, i, &entry_rlp) != RLP_LIST) { // Decoding the i-th entry (which is a list)
        c4_state_add_error(&ctx->state, "build_access_list_ssz: Failed to decode access list entry as RLP list");
        ssz_builder_free(&entry_builder);
        ssz_builder_free(&access_list_builder);
        return (ssz_builder_t) {0};
      }
      if (rlp_decode(&entry_rlp, 0, &address_rlp) != RLP_ITEM) {
        c4_state_add_error(&ctx->state, "build_access_list_ssz: Failed to decode access list address");
        ssz_builder_free(&entry_builder);
        ssz_builder_free(&access_list_builder);
        return (ssz_builder_t) {0};
      }
      ssz_add_bytes(&entry_builder, "address", address_rlp);

      if (rlp_decode(&entry_rlp, 1, &keys_rlp) != RLP_LIST) {
        c4_state_add_error(&ctx->state, "build_access_list_ssz: Failed to decode access list storage keys as RLP list");
        ssz_builder_free(&entry_builder);
        ssz_builder_free(&access_list_builder);
        return (ssz_builder_t) {0};
      }
      int num_keys = rlp_decode(&keys_rlp, -1, NULL);
      if (num_keys < 0) {
        c4_state_add_error(&ctx->state, "build_access_list_ssz: Failed to decode number of storage keys");
        ssz_builder_free(&entry_builder);
        ssz_builder_free(&access_list_builder);
        return (ssz_builder_t) {0};
      }

      bytes_t storage_keys_bytes = NULL_BYTES;
      if (num_keys > 0) {
        storage_keys_bytes.data = safe_malloc(num_keys * 32);
        if (!storage_keys_bytes.data) {
          c4_state_add_error(&ctx->state, "build_access_list_ssz: Malloc failed for storage_keys");
          ssz_builder_free(&entry_builder);
          ssz_builder_free(&access_list_builder);
          return (ssz_builder_t) {0};
        }
        storage_keys_bytes.len = num_keys * 32;
        for (int k = 0; k < num_keys; k++) {
          bytes_t key_rlp = {0};
          if (rlp_decode(&keys_rlp, k, &key_rlp) != RLP_ITEM || key_rlp.len != 32) {
            c4_state_add_error(&ctx->state, "build_access_list_ssz: Failed to decode storage key or invalid length");
            safe_free(storage_keys_bytes.data);
            ssz_builder_free(&entry_builder);
            ssz_builder_free(&access_list_builder);
            return (ssz_builder_t) {0};
          }
          memcpy(storage_keys_bytes.data + k * 32, key_rlp.data, 32);
        }
      }
      ssz_add_bytes(&entry_builder, "storageKeys", storage_keys_bytes);
      if (storage_keys_bytes.data) {
        safe_free(storage_keys_bytes.data);
      }
      ssz_add_dynamic_list_builders(&access_list_builder, entries, entry_builder);
    }
  }
  return access_list_builder;
}

static ssz_builder_t build_authorization_list_ssz(verify_ctx_t* ctx, bytes_t rlp_auth_list_field, const ssz_def_t* auth_list_ssz_def) {
  ssz_builder_t authorization_list_builder = ssz_builder_for_def(auth_list_ssz_def);
  if (!auth_list_ssz_def) {
    c4_state_add_error(&ctx->state, "build_authorization_list_ssz: NULL SSZ definition provided");
    return (ssz_builder_t) {0};
  }

  if (rlp_auth_list_field.len > 0 && rlp_auth_list_field.data != NULL) {
    bytes_t current_rlp_list_data = rlp_auth_list_field;
    int     num_auth_entries      = rlp_decode(&current_rlp_list_data, -1, NULL);
    if (num_auth_entries < 0) {
      c4_state_add_error(&ctx->state, "build_authorization_list_ssz: Failed to decode number of authorization entries");
      ssz_builder_free(&authorization_list_builder);
      return (ssz_builder_t) {0};
    }

    const ssz_def_t* auth_entry_ssz_def = auth_list_ssz_def->def.vector.type;

    for (int i = 0; i < num_auth_entries; i++) {
      ssz_builder_t auth_entry_builder = ssz_builder_for_def(auth_entry_ssz_def);
      bytes_t       auth_tuple_rlp     = {0};

      bytes_t outer_list_for_decode = rlp_auth_list_field;

      if (rlp_decode(&outer_list_for_decode, i, &auth_tuple_rlp) != RLP_LIST) {
        c4_state_add_error(&ctx->state, "build_authorization_list_ssz: Failed to decode authorization entry as RLP list");
        ssz_builder_free(&auth_entry_builder);
        ssz_builder_free(&authorization_list_builder);
        return (ssz_builder_t) {0};
      }

      bytes_t rlp_item_addr, rlp_item_chain_id, rlp_item_nonce, rlp_item_y_parity, rlp_item_r, rlp_item_s;

      // SSZ Order for ETH_AUTHORIZATION_LIST_DATA: address, chainId, nonce, r, s, yParity
      // RLP tuple order: [chain_id (0), address (1), nonce (2), y_parity (3), r (4), s (5)]

      // Field 0 in SSZ: address (from RLP index 1)
      if (rlp_decode(&auth_tuple_rlp, 1, &rlp_item_addr) != RLP_ITEM || rlp_item_addr.len != 20) {
        c4_state_add_error(&ctx->state, "build_authorization_list_ssz: Failed to decode auth address or invalid length");
        ssz_builder_free(&auth_entry_builder);
        ssz_builder_free(&authorization_list_builder);
        return (ssz_builder_t) {0};
      }
      ssz_add_bytes(&auth_entry_builder, "address", rlp_item_addr);

      // Field 1 in SSZ: chainId (from RLP index 0)
      if (rlp_decode(&auth_tuple_rlp, 0, &rlp_item_chain_id) != RLP_ITEM) {
        c4_state_add_error(&ctx->state, "build_authorization_list_ssz: Failed to decode auth chain_id");
        ssz_builder_free(&auth_entry_builder);
        ssz_builder_free(&authorization_list_builder);
        return (ssz_builder_t) {0};
      }
      ssz_add_uint32(&auth_entry_builder, (uint32_t) bytes_as_be(rlp_item_chain_id));

      // Field 2 in SSZ: nonce (from RLP index 2)
      if (rlp_decode(&auth_tuple_rlp, 2, &rlp_item_nonce) != RLP_ITEM) {
        c4_state_add_error(&ctx->state, "build_authorization_list_ssz: Failed to decode auth nonce");
        ssz_builder_free(&auth_entry_builder);
        ssz_builder_free(&authorization_list_builder);
        return (ssz_builder_t) {0};
      }
      ssz_add_uint64(&auth_entry_builder, bytes_as_be(rlp_item_nonce));

      // Field 3 in SSZ: r (from RLP index 4)
      if (rlp_decode(&auth_tuple_rlp, 4, &rlp_item_r) != RLP_ITEM || rlp_item_r.len > 32) {
        c4_state_add_error(&ctx->state, "build_authorization_list_ssz: Failed to decode auth r or r too long");
        ssz_builder_free(&auth_entry_builder);
        ssz_builder_free(&authorization_list_builder);
        return (ssz_builder_t) {0};
      }
      // RLP is big-endian, SSZ UINT256 stores little-endian; ssz_add_uint256 performs the swap.
      ssz_add_uint256(&auth_entry_builder, rlp_item_r);

      // Field 4 in SSZ: s (from RLP index 5)
      if (rlp_decode(&auth_tuple_rlp, 5, &rlp_item_s) != RLP_ITEM || rlp_item_s.len > 32) {
        c4_state_add_error(&ctx->state, "build_authorization_list_ssz: Failed to decode auth s or s too long");
        ssz_builder_free(&auth_entry_builder);
        ssz_builder_free(&authorization_list_builder);
        return (ssz_builder_t) {0};
      }
      ssz_add_uint256(&auth_entry_builder, rlp_item_s);

      // Field 5 in SSZ: yParity (from RLP index 3)
      if (rlp_decode(&auth_tuple_rlp, 3, &rlp_item_y_parity) != RLP_ITEM) {
        c4_state_add_error(&ctx->state, "build_authorization_list_ssz: Failed to decode auth y_parity");
        ssz_builder_free(&auth_entry_builder);
        ssz_builder_free(&authorization_list_builder);
        return (ssz_builder_t) {0};
      }
      ssz_add_uint8(&auth_entry_builder, rlp_item_y_parity.len > 0 ? rlp_item_y_parity.data[0] : 0);

      ssz_add_dynamic_list_builders(&authorization_list_builder, num_auth_entries, auth_entry_builder);
    }
  }
  return authorization_list_builder;
}

INTERNAL bool c4_write_tx_data_from_raw(verify_ctx_t* ctx, ssz_builder_t* buffer, bytes_t raw_tx,
                                        bytes32_t tx_hash, bytes32_t block_hash, uint64_t block_number,
                                        uint32_t transaction_index, uint64_t base_fee, uint64_t block_timestamp) {
  if (!ctx || !buffer || !buffer->def || !raw_tx.data || raw_tx.len == 0) return false;
  address_t from_address  = {0};
  tx_type_t type          = 0;
  bytes_t   serialized_tx = raw_tx;
  if (!get_and_remove_tx_type(ctx, &raw_tx, &type)) return false;

  // Deposited Transactions (0x7E) are also RLP encoded, just with different fields

  bytes_t                rlp_list_payload = raw_tx;                 // Renamed to avoid confusion with rlp_list var below
  const rlp_type_defs_t* defs_ptr         = get_tx_type_defs(type); // get the specific typedef for the tx type
  if (!defs_ptr) RETURN_VERIFY_ERROR(ctx, "unsupported transaction type");
  if (rlp_decode(&rlp_list_payload, 0, &rlp_list_payload) != RLP_LIST) RETURN_VERIFY_ERROR(ctx, "c4_write_tx_data_from_raw: invalid RLP list payload");
  int num_fields = rlp_decode(&rlp_list_payload, -1, NULL);
  if (num_fields != defs_ptr->len) RETURN_VERIFY_ERROR(ctx, "c4_write_tx_data_from_raw: RLP field count mismatch with definition");

  // Deposited transactions don't have signatures, so we extract 'from' from RLP directly
  if (type == TX_TYPE_DEPOSITED) {
    bytes_t from_field = get_rlp_field(ctx, rlp_list_payload, defs_ptr, "from", RLP_ITEM);
    if (from_field.len == 20) {
      memcpy(from_address, from_field.data, 20);
    }
  }
  else {
    if (!c4_tx_create_from_address(ctx, serialized_tx, from_address)) return false;
  }

  // Initialize builders and blob_hashes
  bytes_t       blob_hashes                = {0};
  ssz_builder_t access_list_builder        = ssz_builder_for_def(ssz_get_def(buffer->def, "accessList"));
  ssz_builder_t authorization_list_builder = ssz_builder_for_def(ssz_get_def(buffer->def, "authorizationList"));

  // Build blob hashes
  blob_hashes = build_blob_hashes_from_rlp(ctx, rlp_list_payload, defs_ptr, type);

  // Build access list
  if (ctx->state.error == NULL)
    access_list_builder = build_access_list_ssz(
        ctx, get_rlp_field(ctx, rlp_list_payload, defs_ptr, "accessList", RLP_LIST),
        access_list_builder.def);

  // Build authorization list (only for EIP-7702)
  if (type == TX_TYPE_EIP7702 && ctx->state.error == NULL)
    authorization_list_builder = build_authorization_list_ssz(
        ctx, get_rlp_field(ctx, rlp_list_payload, defs_ptr, "authorizationList", RLP_LIST),
        authorization_list_builder.def);

  // --- All auxiliary data built, or error handled. Proceed with remaining fields ---

  bytes_t  rlp_tx_sig_y_parity              = {0};
  bytes_t  rlp_v_field                      = {0};
  uint8_t  tx_sig_y_parity                  = 0;
  uint64_t v_for_ssz                        = 0;
  uint32_t chain_id                         = 0;
  uint64_t gas_price_rlp_val                = 0;
  uint64_t max_priority_fee_per_gas_rlp_val = 0;
  uint64_t max_fee_per_gas_rlp_val          = 0;

  // Deposited transactions don't have signatures or gas pricing fields
  if (type != TX_TYPE_DEPOSITED) {
    rlp_tx_sig_y_parity = get_rlp_field(ctx, rlp_list_payload, defs_ptr, "yParity", RLP_ITEM);
    rlp_v_field         = get_rlp_field(ctx, rlp_list_payload, defs_ptr, "v", RLP_ITEM);
    tx_sig_y_parity     = rlp_tx_sig_y_parity.len ? rlp_tx_sig_y_parity.data[0] : 0;
    chain_id            = (uint32_t) bytes_as_be(get_rlp_field(ctx, rlp_list_payload, defs_ptr, "chainId", RLP_ITEM));
    gas_price_rlp_val   = bytes_as_be(get_rlp_field(ctx, rlp_list_payload, defs_ptr, "gasPrice", RLP_ITEM));
  }

  if (type >= TX_TYPE_EIP1559 && type != TX_TYPE_DEPOSITED) {
    max_priority_fee_per_gas_rlp_val = bytes_as_be(get_rlp_field(ctx, rlp_list_payload, defs_ptr, "maxPriorityFeePerGas", RLP_ITEM));
    max_fee_per_gas_rlp_val          = bytes_as_be(get_rlp_field(ctx, rlp_list_payload, defs_ptr, "maxFeePerGas", RLP_ITEM));
  }

  // Centralized error check after all get_rlp_field calls for primary tx fields
  if (ctx->state.error != NULL) {
    if (blob_hashes.data) safe_free(blob_hashes.data);
    ssz_builder_free(&access_list_builder);
    ssz_builder_free(&authorization_list_builder);
    return false;
  }

  // Calculate v_for_ssz, chain_id (legacy overwrite), and gas_price
  if (type == TX_TYPE_LEGACY) {
    uint64_t rlp_v_val = rlp_v_field.len && rlp_v_field.len <= 8 ? bytes_as_be(rlp_v_field) : 0;
    tx_sig_y_parity    = (uint8_t) ((rlp_v_val - 1) % 2);
    chain_id           = rlp_v_val < 28 ? 1 : (rlp_v_val - 35 - tx_sig_y_parity) / 2;
    v_for_ssz          = rlp_v_val;
  }
  else if (type != TX_TYPE_DEPOSITED)
    v_for_ssz = (uint64_t) tx_sig_y_parity;

  uint64_t final_gas_price = gas_price_rlp_val;
  if (type >= TX_TYPE_EIP1559 && type != TX_TYPE_DEPOSITED)
    final_gas_price = base_fee + min64(max_priority_fee_per_gas_rlp_val, max_fee_per_gas_rlp_val - base_fee);

  // --- Add fields to SSZ Builder IN ORDER OF ETH_TX_DATA DEFINITION ---

  // Build the SSZ opt_mask so ssz_dump only emits fields that are actually defined for
  // this transaction type per the Ethereum JSON-RPC / Execution API spec (issue #343).
  // We always write every SSZ slot (SSZ containers require it); the mask just controls
  // JSON visibility.
  //
  // Common base for every signed transaction type (0..4). Legacy transactions do have
  // v/r/s and a gasPrice, so those bits belong to the base too.
  const uint32_t base_mask =
      TX_BLOCK_HASH | TX_BLOCK_NUMBER | TX_HASH | TX_TRANSACTION_INDEX | TX_TYPE |
      TX_NONCE | TX_INPUT | TX_R | TX_S | TX_V | TX_GAS | TX_FROM | TX_TO | TX_VALUE |
      TX_GAS_PRICE;

  uint32_t field_mask = 0;
  switch (type) {
    case TX_TYPE_DEPOSITED:
      // Optimism deposit txs have no signature and different fields entirely.
      field_mask = TX_BLOCK_HASH | TX_BLOCK_NUMBER | TX_HASH | TX_TRANSACTION_INDEX |
                   TX_TYPE | TX_NONCE | TX_INPUT | TX_GAS | TX_FROM | TX_TO | TX_VALUE |
                   TX_GAS_PRICE | TX_SOURCE_HASH | TX_MINT | TX_IS_SYSTEM_TX |
                   TX_DEPOSIT_RECEIPT_VERSION;
      break;
    case TX_TYPE_LEGACY:
      // Legacy transactions signed post-EIP-155 have chain_id derived from v.
      // If the derivation gave chain_id == 0 the tx is pre-EIP-155 and we must not
      // report chainId.
      field_mask = base_mask;
      if (chain_id != 0) field_mask |= TX_CHAIN_ID;
      break;
    case TX_TYPE_EIP2930:
      field_mask = base_mask | TX_CHAIN_ID | TX_Y_PARITY | TX_ACCESS_LIST;
      break;
    case TX_TYPE_EIP1559:
      field_mask = base_mask | TX_CHAIN_ID | TX_Y_PARITY | TX_ACCESS_LIST |
                   TX_MAX_FEE_PER_GAS | TX_MAX_PRIORITY_FEE_PER_GAS;
      break;
    case TX_TYPE_EIP4844:
      field_mask = base_mask | TX_CHAIN_ID | TX_Y_PARITY | TX_ACCESS_LIST |
                   TX_MAX_FEE_PER_GAS | TX_MAX_PRIORITY_FEE_PER_GAS |
                   TX_BLOB_VERSIONED_HASHES | TX_MAX_FEE_PER_BLOB_GAS;
      break;
    case TX_TYPE_EIP7702:
      field_mask = base_mask | TX_CHAIN_ID | TX_Y_PARITY | TX_ACCESS_LIST |
                   TX_MAX_FEE_PER_GAS | TX_MAX_PRIORITY_FEE_PER_GAS |
                   TX_AUTHORIZATION_LIST;
      break;
    default:
      // Unknown / future transaction types: fall back to the union of the currently
      // known bits so we don't accidentally drop information.
      field_mask = base_mask | TX_CHAIN_ID | TX_Y_PARITY | TX_ACCESS_LIST |
                   TX_MAX_FEE_PER_GAS | TX_MAX_PRIORITY_FEE_PER_GAS |
                   TX_BLOB_VERSIONED_HASHES | TX_MAX_FEE_PER_BLOB_GAS |
                   TX_AUTHORIZATION_LIST;
      break;
  }
  // blockTimestamp is only meaningful once the transaction has been mined and we know
  // the block. Callers that operate on a not-yet-mined tx (pending, local reconstruction)
  // pass block_timestamp == 0 and we suppress the field.
  if (block_timestamp != 0)
    field_mask |= TX_BLOCK_TIMESTAMP;
  ssz_add_uint32(buffer, field_mask);

  ssz_add_bytes(buffer, "blockHash", bytes(block_hash, 32));
  ssz_add_uint64(buffer, block_number);
  ssz_add_bytes(buffer, "hash", bytes(tx_hash, 32));
  ssz_add_uint32(buffer, transaction_index);
  ssz_add_uint8(buffer, (uint8_t) type);
  // Note: For Deposited Transactions (0x7E), nonce handling depends on configuration:
  // - Fast mode (default): nonce = 0 (avoids receipt proof requirement)
  // - Accurate mode: nonce = depositNonce from receipt (post-Regolith fork, requires receipt proof)
  // TODO: Add configuration option to enable accurate nonce for deposited transactions
  ssz_add_uint64(buffer, type == TX_TYPE_DEPOSITED ? 0 : bytes_as_be(get_rlp_field(ctx, rlp_list_payload, defs_ptr, "nonce", RLP_ITEM)));
  ssz_add_bytes(buffer, "input", get_rlp_field(ctx, rlp_list_payload, defs_ptr, "input", RLP_ITEM));

  // Handle signature fields - deposited transactions don't have signatures.
  // r/s are UINT256 (little-endian in SSZ, rendered as QUANTITY in JSON per RPC spec).
  // ssz_add_uint256 converts big-endian RLP input to little-endian SSZ storage.
  if (type == TX_TYPE_DEPOSITED) {
    ssz_add_uint256(buffer, NULL_BYTES);
    ssz_add_uint256(buffer, NULL_BYTES);
  }
  else {
    ssz_add_uint256(buffer, get_rlp_field(ctx, rlp_list_payload, defs_ptr, "r", RLP_ITEM));
    ssz_add_uint256(buffer, get_rlp_field(ctx, rlp_list_payload, defs_ptr, "s", RLP_ITEM));
  }

  ssz_add_uint32(buffer, chain_id);
  ssz_add_uint64(buffer, v_for_ssz);
  ssz_add_uint64(buffer, bytes_as_be(get_rlp_field(ctx, rlp_list_payload, defs_ptr, "gas", RLP_ITEM)));
  ssz_add_bytes(buffer, "from", bytes(from_address, 20));
  ssz_add_bytes(buffer, "to", get_rlp_field(ctx, rlp_list_payload, defs_ptr, "to", RLP_ITEM));
  ssz_add_uint256(buffer, get_rlp_field(ctx, rlp_list_payload, defs_ptr, "value", RLP_ITEM));
  ssz_add_uint64(buffer, final_gas_price);

  // Handle gas pricing fields - not applicable for deposited transactions
  if (type == TX_TYPE_DEPOSITED) {
    ssz_add_uint64(buffer, 0); // maxFeePerGas
    ssz_add_uint64(buffer, 0); // maxPriorityFeePerGas
  }
  else {
    ssz_add_uint64(buffer, bytes_as_be(get_rlp_field(ctx, rlp_list_payload, defs_ptr, "maxFeePerGas", RLP_ITEM)));
    ssz_add_uint64(buffer, bytes_as_be(get_rlp_field(ctx, rlp_list_payload, defs_ptr, "maxPriorityFeePerGas", RLP_ITEM)));
  }

  ssz_add_builders(buffer, "accessList", access_list_builder);
  ssz_add_builders(buffer, "authorizationList", authorization_list_builder);
  ssz_add_bytes(buffer, "blobVersionedHashes", blob_hashes);
  ssz_add_uint8(buffer, tx_sig_y_parity);

  // Optional Optimism fields - always add them (SSZ requires all fields)
  // They will be omitted in JSON output if empty due to SSZ_FLAG_OPTIONAL
  if (type == TX_TYPE_DEPOSITED) {
    uint8_t system_tx_true     = 0;
    bytes_t is_system_tx_field = get_rlp_field(ctx, rlp_list_payload, defs_ptr, "isSystemTx", RLP_ITEM);
    if (is_system_tx_field.len > 0 && is_system_tx_field.data[0] != 0) system_tx_true = 1;

    // Populate with actual values for deposited transactions
    ssz_add_bytes(buffer, "sourceHash", get_rlp_field(ctx, rlp_list_payload, defs_ptr, "sourceHash", RLP_ITEM));

    // mint is UINT256; ssz_add_uint256 performs the big-endian -> little-endian swap.
    ssz_add_uint256(buffer, get_rlp_field(ctx, rlp_list_payload, defs_ptr, "mint", RLP_ITEM));
    ssz_add_bytes(buffer, "isSystemTx", bytes(&system_tx_true, 1));

    // Add depositReceiptVersion (always 1 for current Optimism version)
    uint8_t deposit_receipt_version = 1;
    ssz_add_bytes(buffer, "depositReceiptVersion", bytes(&deposit_receipt_version, 1));
  }
  else {
    // Add empty values for non-deposited transactions.
    // sourceHash is BYTES32 (32 fixed bytes), mint is UINT256 (32 little-endian bytes),
    // isSystemTx is BOOLEAN (1 byte), depositReceiptVersion is UINT8 (1 byte).
    ssz_add_bytes(buffer, "sourceHash", NULL_BYTES);
    ssz_add_uint256(buffer, NULL_BYTES);
    ssz_add_bytes(buffer, "isSystemTx", NULL_BYTES);
    ssz_add_bytes(buffer, "depositReceiptVersion", NULL_BYTES);
  }

  // maxFeePerBlobGas: only present in the raw tx for EIP-4844 (type 3). For other
  // types we still have to serialize the SSZ slot, but the field mask will hide it.
  if (type == TX_TYPE_EIP4844)
    ssz_add_uint256(buffer, get_rlp_field(ctx, rlp_list_payload, defs_ptr, "maxFeePerBlobGas", RLP_ITEM));
  else
    ssz_add_uint256(buffer, NULL_BYTES);

  // blockTimestamp: caller supplies 0 for pending / not-yet-mined transactions.
  ssz_add_uint64(buffer, block_timestamp);

  if (blob_hashes.data) safe_free(blob_hashes.data);

  return true;
}

bool c4_write_receipt_data_from_raw(verify_ctx_t* ctx, ssz_builder_t* buffer, bytes_t tx_raw, bytes_t receipt_raw,
                                    bytes32_t block_hash, uint64_t block_number, uint32_t tx_index,
                                    uint64_t base_fee, uint64_t excess_blob_gas, uint64_t block_timestamp,
                                    uint64_t* out_cumulative_gas,
                                    uint32_t* out_log_index) {
  bytes_t                val             = {0};
  bytes_t                receipt_list    = receipt_raw;
  bytes_t                tx_list_payload = tx_raw;
  tx_type_t              type            = 0;
  bytes32_t              tx_hash         = {0};
  address_t              from_address    = {0};
  uint64_t               status_u64      = 0;
  uint64_t               cumulative_gas  = 0;
  uint64_t               gas_used        = 0;
  uint64_t               effective_gas   = 0;
  uint64_t               deposit_nonce   = 0;
  uint32_t               deposit_ver     = 0;
  uint64_t               tx_nonce        = 0;
  uint32_t               num_blobs       = 0;
  const rlp_type_defs_t* defs_ptr        = NULL;

  if (!buffer || !buffer->def || !out_cumulative_gas || !out_log_index) return false;
  keccak(tx_raw, tx_hash);
  if (!get_and_remove_tx_type(ctx, &tx_list_payload, &type)) return false;
  if (rlp_decode(&tx_list_payload, 0, &tx_list_payload) != RLP_LIST) RETURN_VERIFY_ERROR(ctx, "write_receipt_data: invalid tx list");
  defs_ptr = get_tx_type_defs(type);
  if (!defs_ptr) RETURN_VERIFY_ERROR(ctx, "write_receipt_data: unsupported tx type");

  if (type != TX_TYPE_LEGACY) {
    if (receipt_raw.len < 1 || receipt_raw.data[0] != type) RETURN_VERIFY_ERROR(ctx, "write_receipt_data: receipt type mismatch");
    receipt_list.data = receipt_raw.data + 1;
    receipt_list.len  = receipt_raw.len - 1;
  }
  if (rlp_decode(&receipt_list, 0, &receipt_list) != RLP_LIST) RETURN_VERIFY_ERROR(ctx, "write_receipt_data: invalid receipt list");
  if (rlp_decode(&receipt_list, 0, &val) != RLP_ITEM) RETURN_VERIFY_ERROR(ctx, "write_receipt_data: status");
  status_u64 = bytes_as_be(val);
  if (rlp_decode(&receipt_list, 1, &val) != RLP_ITEM) RETURN_VERIFY_ERROR(ctx, "write_receipt_data: cumulativeGasUsed");
  cumulative_gas = bytes_as_be(val);
  gas_used       = cumulative_gas - *out_cumulative_gas;
  if (rlp_decode(&receipt_list, 2, &val) != RLP_ITEM || val.len != 256) RETURN_VERIFY_ERROR(ctx, "write_receipt_data: logsBloom");
  bytes_t logs_bloom = val;

  if (type == TX_TYPE_DEPOSITED) {
    if (rlp_decode(&receipt_list, 4, &val) != RLP_ITEM) RETURN_VERIFY_ERROR(ctx, "write_receipt_data: depositNonce");
    deposit_nonce = bytes_as_be(val);
    if (rlp_decode(&receipt_list, 5, &val) != RLP_ITEM) RETURN_VERIFY_ERROR(ctx, "write_receipt_data: depositReceiptVersion");
    deposit_ver = (uint32_t) bytes_as_be(val);
  }

  if (type == TX_TYPE_DEPOSITED) {
    bytes_t from_field = get_rlp_field(ctx, tx_list_payload, defs_ptr, "from", RLP_ITEM);
    if (from_field.len == 20) memcpy(from_address, from_field.data, 20);
  }
  else if (!c4_tx_create_from_address(ctx, tx_raw, from_address))
    return false;

  bytes_t  to_field          = get_rlp_field(ctx, tx_list_payload, defs_ptr, "to", RLP_ITEM);
  uint64_t gas_price_rlp_val = 0, max_priority_fee_per_gas_rlp_val = 0, max_fee_per_gas_rlp_val = 0;
  if (type != TX_TYPE_DEPOSITED)
    gas_price_rlp_val = bytes_as_be(get_rlp_field(ctx, tx_list_payload, defs_ptr, "gasPrice", RLP_ITEM));
  if (type >= TX_TYPE_EIP1559 && type != TX_TYPE_DEPOSITED) {
    max_priority_fee_per_gas_rlp_val = bytes_as_be(get_rlp_field(ctx, tx_list_payload, defs_ptr, "maxPriorityFeePerGas", RLP_ITEM));
    max_fee_per_gas_rlp_val          = bytes_as_be(get_rlp_field(ctx, tx_list_payload, defs_ptr, "maxFeePerGas", RLP_ITEM));
  }
  if (ctx->state.error != NULL) return false;

  effective_gas = gas_price_rlp_val;
  if (type >= TX_TYPE_EIP1559 && type != TX_TYPE_DEPOSITED)
    effective_gas = base_fee + (max_priority_fee_per_gas_rlp_val < (max_fee_per_gas_rlp_val - base_fee) ? max_priority_fee_per_gas_rlp_val : (max_fee_per_gas_rlp_val - base_fee));

  // Extract tx nonce (deposit txs also encode a `nonce`, but we prefer the receipt
  // depositNonce for them, so read only for signed txs).
  if (type != TX_TYPE_DEPOSITED)
    tx_nonce = bytes_as_be(get_rlp_field(ctx, tx_list_payload, defs_ptr, "nonce", RLP_ITEM));

  // Blob count for EIP-4844: number of blob versioned hashes in the tx.
  if (type == TX_TYPE_EIP4844) {
    bytes_t blob_hashes = get_rlp_field(ctx, tx_list_payload, defs_ptr, "blobVersionedHashes", RLP_LIST);
    if (blob_hashes.data) {
      int n = rlp_decode(&blob_hashes, -1, NULL);
      if (n > 0) num_blobs = (uint32_t) n;
    }
  }
  if (ctx->state.error != NULL) return false;

  // Blob gas price is priced by the EL from the block's excess_blob_gas.
  uint64_t blob_gas_price = 0;
  uint64_t blob_gas_used  = (uint64_t) num_blobs * ETH_GAS_PER_BLOB;
  if (type == TX_TYPE_EIP4844)
    blob_gas_price = eth_fake_exponential(ETH_MIN_BLOB_BASE_FEE, excess_blob_gas, ETH_BLOB_BASE_FEE_UPDATE_FRACTION);

  // Contract creation address: for successful signed txs where `to` is empty,
  // the created contract address is keccak256(rlp([sender, nonce]))[12:32].
  // CREATE2-style deployments produce their own address inside the EVM and are
  // NOT set on the outer receipt.  Deposit txs never create contracts here.
  bytes32_t contract_hash_buf   = {0};
  bytes_t   contract_address    = NULL_BYTES;
  bool      has_contract_addr   = false;
  if (type != TX_TYPE_DEPOSITED && status_u64 == 1 && to_field.len == 0) {
    uint8_t  rlp_tmp[64] = {0};
    buffer_t rlp_buf     = {.data = {.data = rlp_tmp, .len = 0}, .allocated = -(int32_t) sizeof(rlp_tmp)};
    rlp_add_item(&rlp_buf, bytes(from_address, 20));
    rlp_add_uint64(&rlp_buf, tx_nonce);
    rlp_to_list(&rlp_buf);
    keccak(rlp_buf.data, contract_hash_buf);
    contract_address  = bytes(contract_hash_buf + 12, 20);
    has_contract_addr = true;
  }

  // Receipt opt_mask: expose only fields that actually belong to this receipt's
  // transaction type (issue #343). The bit indices track the order of fields in
  // ETH_RECEIPT_DATA (see verify_data_types.h) - bit 0 is the mask itself,
  // bit N corresponds to the Nth container element after the mask.
  const uint32_t receipt_base_mask =
      (1u << 1) |  // blockHash
      (1u << 2) |  // blockNumber
      (1u << 3) |  // transactionHash
      (1u << 4) |  // transactionIndex
      (1u << 5) |  // type
      (1u << 6) |  // from
      (1u << 7) |  // to
      (1u << 8) |  // cumulativeGasUsed
      (1u << 9) |  // gasUsed
      (1u << 10) | // logs
      (1u << 11) | // logsBloom
      (1u << 12) | // status
      (1u << 13);  // effectiveGasPrice
  uint32_t receipt_mask = receipt_base_mask;
  if (type == TX_TYPE_DEPOSITED)
    receipt_mask |= (1u << 14) | (1u << 15); // depositNonce, depositReceiptVersion (OP Stack)
  if (has_contract_addr)
    receipt_mask |= (1u << 16); // contractAddress
  if (type == TX_TYPE_EIP4844)
    receipt_mask |= (1u << 17) | (1u << 18); // blobGasUsed, blobGasPrice
  ssz_add_uint32(buffer, receipt_mask);
  ssz_add_bytes(buffer, "blockHash", bytes(block_hash, 32));
  ssz_add_uint64(buffer, block_number);
  ssz_add_bytes(buffer, "transactionHash", bytes(tx_hash, 32));
  ssz_add_uint32(buffer, tx_index);
  ssz_add_uint8(buffer, (uint8_t) type);
  ssz_add_bytes(buffer, "from", bytes(from_address, 20));
  // `to` is SSZ_NULLABLE_BYTES(20): empty payload => rendered as `null` (contract creation).
  if (to_field.len == 0) {
    ssz_add_bytes(buffer, "to", NULL_BYTES);
  }
  else {
    uint8_t to_pad[20] = {0};
    if (to_field.len <= 20) memcpy(to_pad, to_field.data, to_field.len);
    ssz_add_bytes(buffer, "to", bytes(to_pad, 20));
  }
  ssz_add_uint64(buffer, cumulative_gas);
  ssz_add_uint64(buffer, gas_used);

  bytes_t logs_rlp = {0};
  if (rlp_decode(&receipt_list, 3, &logs_rlp) != RLP_LIST) RETURN_VERIFY_ERROR(ctx, "write_receipt_data: logs");
  int              num_logs     = rlp_decode(&logs_rlp, -1, &logs_rlp);
  const ssz_def_t* logs_def     = ssz_get_def(buffer->def, "logs");
  ssz_builder_t    logs_builder = ssz_builder_for_def(logs_def);
  for (int log_index = 0; log_index < num_logs; log_index++) {
    bytes_t log_rlp = {0};
    if (rlp_decode(&logs_rlp, log_index, &log_rlp) != RLP_LIST) RETURN_VERIFY_ERROR(ctx, "write_receipt_data: log entry");
    bytes_t addr_bytes = {0}, data_bytes = {0}, topics_rlp = {0};
    if (rlp_decode(&log_rlp, 0, &addr_bytes) != RLP_ITEM) RETURN_VERIFY_ERROR(ctx, "write_receipt_data: log address");
    if (rlp_decode(&log_rlp, 1, &topics_rlp) != RLP_LIST) RETURN_VERIFY_ERROR(ctx, "write_receipt_data: log topics");
    if (rlp_decode(&log_rlp, 2, &data_bytes) != RLP_ITEM) RETURN_VERIFY_ERROR(ctx, "write_receipt_data: log data");
    int           num_topics  = rlp_decode(&topics_rlp, -1, &topics_rlp);
    ssz_builder_t log_builder = ssz_builder_for_def(logs_def->def.vector.type);
    ssz_add_bytes(&log_builder, "blockHash", bytes(block_hash, 32));
    ssz_add_uint64(&log_builder, block_number);
    ssz_add_bytes(&log_builder, "transactionHash", bytes(tx_hash, 32));
    ssz_add_uint32(&log_builder, tx_index);
    uint8_t addr_20[20] = {0};
    if (addr_bytes.len <= 20) memcpy(addr_20 + (20 - addr_bytes.len), addr_bytes.data, addr_bytes.len);
    ssz_add_bytes(&log_builder, "address", bytes(addr_20, 20));
    ssz_add_uint32(&log_builder, *out_log_index + (uint32_t) log_index);
    ssz_add_uint8(&log_builder, 0);
    ssz_builder_t topics_builder = ssz_builder_for_def(ssz_get_def(log_builder.def, "topics"));
    for (int t = 0; t < num_topics; t++) {
      bytes_t topic       = {0};
      uint8_t topic32[32] = {0};
      if (rlp_decode(&topics_rlp, t, &topic) != RLP_ITEM) RETURN_VERIFY_ERROR(ctx, "write_receipt_data: topic");
      if (topic.len <= 32) memcpy(topic32 + (32 - topic.len), topic.data, topic.len);
      ssz_add_dynamic_list_bytes(&topics_builder, num_topics, bytes(topic32, 32));
    }
    ssz_add_builders(&log_builder, "topics", topics_builder);
    ssz_add_bytes(&log_builder, "data", data_bytes);
    ssz_add_dynamic_list_builders(&logs_builder, num_logs, log_builder);
  }
  ssz_add_builders(buffer, "logs", logs_builder);
  ssz_add_bytes(buffer, "logsBloom", logs_bloom);
  ssz_add_uint8(buffer, (uint8_t) status_u64);
  ssz_add_uint64(buffer, effective_gas);
  ssz_add_uint64(buffer, deposit_nonce);
  ssz_add_uint32(buffer, deposit_ver);
  // contractAddress: nullable list; empty payload -> `null` in JSON.
  ssz_add_bytes(buffer, "contractAddress", has_contract_addr ? contract_address : NULL_BYTES);
  ssz_add_uint64(buffer, blob_gas_used);
  ssz_add_uint64(buffer, blob_gas_price);
  *out_cumulative_gas = cumulative_gas;
  *out_log_index      = *out_log_index + (uint32_t) num_logs;
  return true;
}