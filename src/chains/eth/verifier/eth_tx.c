#include "beacon_types.h"
#include "bytes.h"
#include "crypto.h"
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
  TX_TYPE_LEGACY  = 0,
  TX_TYPE_EIP2930 = 1,
  TX_TYPE_EIP1559 = 2,
  TX_TYPE_EIP4844 = 3,
  TX_TYPE_EIP7702 = 4,
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

static const rlp_type_defs_t tx_type_defs[] = {
    {tx_legacy_defs, sizeof(tx_legacy_defs) / sizeof(rlp_def_t)},
    {tx_1_defs, sizeof(tx_1_defs) / sizeof(rlp_def_t)},
    {tx_2_defs, sizeof(tx_2_defs) / sizeof(rlp_def_t)},
    {tx_type3_defs, sizeof(tx_type3_defs) / sizeof(rlp_def_t)},
    {tx_eip7702_defs, sizeof(tx_eip7702_defs) / sizeof(rlp_def_t)},
};

static bool get_and_remove_tx_type(verify_ctx_t* ctx, bytes_t* raw_tx, tx_type_t* type) {
  if (raw_tx->len < 1) RETURN_VERIFY_ERROR(ctx, "invalid tx data, missing type!");
  *type = raw_tx->data[0];
  if (*type >= 0x7f)
    *type = TX_TYPE_LEGACY; // legacy tx
  else if (*type > TX_TYPE_EIP7702)
    RETURN_VERIFY_ERROR(ctx, "invalid tx type, must be 1,2,3,4 or legacy tx!");
  else {
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
  rlp_type_defs_t defs = tx_type_defs[type];
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
  if (!c4_tx_create_from_address(ctx, tx_raw, tmp) || memcmp(tmp, ssz_get(&receipt_data, "from").bytes.data, 20)) RETURN_VERIFY_ERROR(ctx, "invalid tx data, wrong from address!");
  if (!get_and_remove_tx_type(ctx, &tx_raw, &type) && type != (uint8_t) ssz_get_uint32(&receipt_data, "type")) RETURN_VERIFY_ERROR(ctx, "invalid tx data, invalid type!");
  if (rlp_decode(&tx_raw, 0, &tx_raw) != RLP_LIST || rlp_decode(&tx_raw, type == TX_TYPE_EIP4844 ? 5 : (3 + type), &val) != RLP_ITEM || !bytes_eq(val, ssz_get(&receipt_data, "to").bytes)) RETURN_VERIFY_ERROR(ctx, "invalid to address!");
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

INTERNAL bool c4_tx_verify_receipt_proof(verify_ctx_t* ctx, ssz_ob_t receipt_proof, uint32_t tx_index, bytes32_t receipt_root, bytes_t* receipt_raw) {
  bytes32_t tmp      = {0};
  buffer_t  path_buf = stack_buffer(tmp);

  if (patricia_verify(receipt_root, c4_eth_create_tx_path(tx_index, &path_buf), receipt_proof, receipt_raw) != PATRICIA_FOUND)
    RETURN_VERIFY_ERROR(ctx, "invalid account proof on execution layer!");
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
      char err_buf[120];
      if (decoded_type <= RLP_SUCCESS) { // Includes RLP_SUCCESS, RLP_OUT_OF_RANGE, RLP_NOT_FOUND
        snprintf(err_buf, sizeof(err_buf), "RLP decode failed or type mismatch for field '%s': expected type %d, decode result %d", field_name, expected_type, decoded_type);
      }
      else { // Decoded type is RLP_ITEM or RLP_LIST but not the expected one
        snprintf(err_buf, sizeof(err_buf), "RLP type mismatch for field '%s': expected %d, got %d", field_name, expected_type, decoded_type);
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
    char err_buf[100];
    snprintf(err_buf, sizeof(err_buf), "build_blob_hashes_from_rlp: Too many blob hashes %d (max 16)", num_hashes);
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
      ssz_buffer_free(&access_list_builder);
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
        ssz_buffer_free(&entry_builder);
        ssz_buffer_free(&access_list_builder);
        return (ssz_builder_t) {0};
      }
      if (rlp_decode(&entry_rlp, 0, &address_rlp) != RLP_ITEM) {
        c4_state_add_error(&ctx->state, "build_access_list_ssz: Failed to decode access list address");
        ssz_buffer_free(&entry_builder);
        ssz_buffer_free(&access_list_builder);
        return (ssz_builder_t) {0};
      }
      ssz_add_bytes(&entry_builder, "address", address_rlp);

      if (rlp_decode(&entry_rlp, 1, &keys_rlp) != RLP_LIST) {
        c4_state_add_error(&ctx->state, "build_access_list_ssz: Failed to decode access list storage keys as RLP list");
        ssz_buffer_free(&entry_builder);
        ssz_buffer_free(&access_list_builder);
        return (ssz_builder_t) {0};
      }
      int num_keys = rlp_decode(&keys_rlp, -1, NULL);
      if (num_keys < 0) {
        c4_state_add_error(&ctx->state, "build_access_list_ssz: Failed to decode number of storage keys");
        ssz_buffer_free(&entry_builder);
        ssz_buffer_free(&access_list_builder);
        return (ssz_builder_t) {0};
      }

      bytes_t storage_keys_bytes = NULL_BYTES;
      if (num_keys > 0) {
        storage_keys_bytes.data = safe_malloc(num_keys * 32);
        if (!storage_keys_bytes.data) {
          c4_state_add_error(&ctx->state, "build_access_list_ssz: Malloc failed for storage_keys");
          ssz_buffer_free(&entry_builder);
          ssz_buffer_free(&access_list_builder);
          return (ssz_builder_t) {0};
        }
        storage_keys_bytes.len = num_keys * 32;
        for (int k = 0; k < num_keys; k++) {
          bytes_t key_rlp = {0};
          if (rlp_decode(&keys_rlp, k, &key_rlp) != RLP_ITEM || key_rlp.len != 32) {
            c4_state_add_error(&ctx->state, "build_access_list_ssz: Failed to decode storage key or invalid length");
            safe_free(storage_keys_bytes.data);
            ssz_buffer_free(&entry_builder);
            ssz_buffer_free(&access_list_builder);
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
      ssz_buffer_free(&authorization_list_builder);
      return (ssz_builder_t) {0};
    }

    const ssz_def_t* auth_entry_ssz_def = auth_list_ssz_def->def.vector.type;

    for (int i = 0; i < num_auth_entries; i++) {
      ssz_builder_t auth_entry_builder = ssz_builder_for_def(auth_entry_ssz_def);
      bytes_t       auth_tuple_rlp     = {0};

      bytes_t outer_list_for_decode = rlp_auth_list_field;

      if (rlp_decode(&outer_list_for_decode, i, &auth_tuple_rlp) != RLP_LIST) {
        c4_state_add_error(&ctx->state, "build_authorization_list_ssz: Failed to decode authorization entry as RLP list");
        ssz_buffer_free(&auth_entry_builder);
        ssz_buffer_free(&authorization_list_builder);
        return (ssz_builder_t) {0};
      }

      bytes_t rlp_item_addr, rlp_item_chain_id, rlp_item_nonce, rlp_item_y_parity, rlp_item_r, rlp_item_s;

      // SSZ Order for ETH_AUTHORIZATION_LIST_DATA: address, chainId, nonce, r, s, yParity
      // RLP tuple order: [chain_id (0), address (1), nonce (2), y_parity (3), r (4), s (5)]

      // Field 0 in SSZ: address (from RLP index 1)
      if (rlp_decode(&auth_tuple_rlp, 1, &rlp_item_addr) != RLP_ITEM || rlp_item_addr.len != 20) {
        c4_state_add_error(&ctx->state, "build_authorization_list_ssz: Failed to decode auth address or invalid length");
        ssz_buffer_free(&auth_entry_builder);
        ssz_buffer_free(&authorization_list_builder);
        return (ssz_builder_t) {0};
      }
      ssz_add_bytes(&auth_entry_builder, "address", rlp_item_addr);

      // Field 1 in SSZ: chainId (from RLP index 0)
      if (rlp_decode(&auth_tuple_rlp, 0, &rlp_item_chain_id) != RLP_ITEM) {
        c4_state_add_error(&ctx->state, "build_authorization_list_ssz: Failed to decode auth chain_id");
        ssz_buffer_free(&auth_entry_builder);
        ssz_buffer_free(&authorization_list_builder);
        return (ssz_builder_t) {0};
      }
      ssz_add_uint32(&auth_entry_builder, (uint32_t) bytes_as_be(rlp_item_chain_id));

      // Field 2 in SSZ: nonce (from RLP index 2)
      if (rlp_decode(&auth_tuple_rlp, 2, &rlp_item_nonce) != RLP_ITEM) {
        c4_state_add_error(&ctx->state, "build_authorization_list_ssz: Failed to decode auth nonce");
        ssz_buffer_free(&auth_entry_builder);
        ssz_buffer_free(&authorization_list_builder);
        return (ssz_builder_t) {0};
      }
      ssz_add_uint64(&auth_entry_builder, bytes_as_be(rlp_item_nonce));

      // Field 3 in SSZ: r (from RLP index 4)
      bytes32_t r_val = {0};
      if (rlp_decode(&auth_tuple_rlp, 4, &rlp_item_r) != RLP_ITEM || rlp_item_r.len > 32) {
        c4_state_add_error(&ctx->state, "build_authorization_list_ssz: Failed to decode auth r or r too long");
        ssz_buffer_free(&auth_entry_builder);
        ssz_buffer_free(&authorization_list_builder);
        return (ssz_builder_t) {0};
      }
      if (rlp_item_r.len > 0) memcpy(r_val + (32 - rlp_item_r.len), rlp_item_r.data, rlp_item_r.len);
      ssz_add_bytes(&auth_entry_builder, "r", bytes(r_val, 32));

      // Field 4 in SSZ: s (from RLP index 5)
      bytes32_t s_val = {0};
      if (rlp_decode(&auth_tuple_rlp, 5, &rlp_item_s) != RLP_ITEM || rlp_item_s.len > 32) {
        c4_state_add_error(&ctx->state, "build_authorization_list_ssz: Failed to decode auth s or s too long");
        ssz_buffer_free(&auth_entry_builder);
        ssz_buffer_free(&authorization_list_builder);
        return (ssz_builder_t) {0};
      }
      if (rlp_item_s.len > 0) memcpy(s_val + (32 - rlp_item_s.len), rlp_item_s.data, rlp_item_s.len);
      ssz_add_bytes(&auth_entry_builder, "s", bytes(s_val, 32));

      // Field 5 in SSZ: yParity (from RLP index 3)
      if (rlp_decode(&auth_tuple_rlp, 3, &rlp_item_y_parity) != RLP_ITEM) {
        c4_state_add_error(&ctx->state, "build_authorization_list_ssz: Failed to decode auth y_parity");
        ssz_buffer_free(&auth_entry_builder);
        ssz_buffer_free(&authorization_list_builder);
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
                                        uint32_t transaction_index, uint64_t base_fee) {
  if (!ctx || !buffer || !buffer->def || !raw_tx.data || raw_tx.len == 0) return false;
  address_t from_address  = {0};
  tx_type_t type          = 0;
  bytes_t   serialized_tx = raw_tx;
  if (!get_and_remove_tx_type(ctx, &raw_tx, &type)) return false;

  bytes_t                rlp_list_payload = raw_tx;              // Renamed to avoid confusion with rlp_list var below
  const rlp_type_defs_t* defs_ptr         = &tx_type_defs[type]; // get the specific typedef for the tx type
  if (rlp_decode(&rlp_list_payload, 0, &rlp_list_payload) != RLP_LIST) RETURN_VERIFY_ERROR(ctx, "c4_write_tx_data_from_raw: invalid RLP list payload");
  int num_fields = rlp_decode(&rlp_list_payload, -1, NULL);
  if (num_fields != defs_ptr->len) RETURN_VERIFY_ERROR(ctx, "c4_write_tx_data_from_raw: RLP field count mismatch with definition");

  if (!c4_tx_create_from_address(ctx, serialized_tx, from_address)) return false;

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

  bytes_t  rlp_tx_sig_y_parity              = get_rlp_field(ctx, rlp_list_payload, defs_ptr, "yParity", RLP_ITEM);
  bytes_t  rlp_v_field                      = get_rlp_field(ctx, rlp_list_payload, defs_ptr, "v", RLP_ITEM);
  uint8_t  tx_sig_y_parity                  = rlp_tx_sig_y_parity.len ? rlp_tx_sig_y_parity.data[0] : 0;
  uint8_t  v_for_ssz                        = 0;
  uint32_t chain_id                         = (uint32_t) bytes_as_be(get_rlp_field(ctx, rlp_list_payload, defs_ptr, "chainId", RLP_ITEM));
  uint64_t gas_price_rlp_val                = bytes_as_be(get_rlp_field(ctx, rlp_list_payload, defs_ptr, "gasPrice", RLP_ITEM));
  uint64_t max_priority_fee_per_gas_rlp_val = 0;
  uint64_t max_fee_per_gas_rlp_val          = 0;

  if (type >= TX_TYPE_EIP1559) {
    max_priority_fee_per_gas_rlp_val = bytes_as_be(get_rlp_field(ctx, rlp_list_payload, defs_ptr, "maxPriorityFeePerGas", RLP_ITEM));
    max_fee_per_gas_rlp_val          = bytes_as_be(get_rlp_field(ctx, rlp_list_payload, defs_ptr, "maxFeePerGas", RLP_ITEM));
  }

  // Centralized error check after all get_rlp_field calls for primary tx fields
  if (ctx->state.error != NULL) {
    if (blob_hashes.data) safe_free(blob_hashes.data);
    ssz_buffer_free(&access_list_builder);
    ssz_buffer_free(&authorization_list_builder);
    return false;
  }

  // Calculate v_for_ssz, chain_id (legacy overwrite), and gas_price
  if (type == TX_TYPE_LEGACY) {
    uint8_t rlp_v_val = rlp_v_field.len ? rlp_v_field.data[0] : 0;
    tx_sig_y_parity   = (rlp_v_val - 1) % 2;
    chain_id          = rlp_v_val < 28 ? 1 : (rlp_v_val - 35 - tx_sig_y_parity) / 2;
    v_for_ssz         = rlp_v_val;
  }
  else {
    v_for_ssz = tx_sig_y_parity;
  }

  uint64_t final_gas_price = gas_price_rlp_val;
  if (type >= TX_TYPE_EIP1559) {
    final_gas_price = base_fee + min64(max_priority_fee_per_gas_rlp_val, max_fee_per_gas_rlp_val - base_fee);
  }

  // --- Add fields to SSZ Builder IN ORDER OF ETH_TX_DATA DEFINITION ---
  ssz_add_bytes(buffer, "blockHash", bytes(block_hash, 32));
  ssz_add_uint64(buffer, block_number);
  ssz_add_bytes(buffer, "hash", bytes(tx_hash, 32));
  ssz_add_uint32(buffer, transaction_index);
  ssz_add_uint8(buffer, (uint8_t) type);
  ssz_add_uint64(buffer, bytes_as_be(get_rlp_field(ctx, rlp_list_payload, defs_ptr, "nonce", RLP_ITEM)));
  ssz_add_bytes(buffer, "input", get_rlp_field(ctx, rlp_list_payload, defs_ptr, "input", RLP_ITEM));
  ssz_add_bytes(buffer, "r", get_rlp_field(ctx, rlp_list_payload, defs_ptr, "r", RLP_ITEM));
  ssz_add_bytes(buffer, "s", get_rlp_field(ctx, rlp_list_payload, defs_ptr, "s", RLP_ITEM));
  ssz_add_uint32(buffer, chain_id);
  ssz_add_uint8(buffer, v_for_ssz);
  ssz_add_uint64(buffer, bytes_as_be(get_rlp_field(ctx, rlp_list_payload, defs_ptr, "gas", RLP_ITEM)));
  ssz_add_bytes(buffer, "from", bytes(from_address, 20));
  ssz_add_bytes(buffer, "to", get_rlp_field(ctx, rlp_list_payload, defs_ptr, "to", RLP_ITEM));
  ssz_add_uint256(buffer, get_rlp_field(ctx, rlp_list_payload, defs_ptr, "value", RLP_ITEM));
  ssz_add_uint64(buffer, final_gas_price);
  ssz_add_uint64(buffer, bytes_as_be(get_rlp_field(ctx, rlp_list_payload, defs_ptr, "maxFeePerGas", RLP_ITEM)));
  ssz_add_uint64(buffer, bytes_as_be(get_rlp_field(ctx, rlp_list_payload, defs_ptr, "maxPriorityFeePerGas", RLP_ITEM)));
  ssz_add_builders(buffer, "accessList", access_list_builder);
  ssz_add_builders(buffer, "authorizationList", authorization_list_builder);
  ssz_add_bytes(buffer, "blobVersionedHashes", blob_hashes);
  ssz_add_uint8(buffer, tx_sig_y_parity);

  if (blob_hashes.data) safe_free(blob_hashes.data);

  return true;
}