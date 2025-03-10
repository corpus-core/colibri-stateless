
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
    {"v", 1},
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
    {"v", 1},
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

static const rlp_type_defs_t tx_type_defs[] = {
    {tx_legacy_defs, sizeof(tx_legacy_defs) / sizeof(rlp_def_t)},
    {tx_1_defs, sizeof(tx_1_defs) / sizeof(rlp_def_t)},
    {tx_2_defs, sizeof(tx_2_defs) / sizeof(rlp_def_t)},
    {tx_type3_defs, sizeof(tx_type3_defs) / sizeof(rlp_def_t)},
};

static bool get_and_remove_tx_type(verify_ctx_t* ctx, bytes_t* raw_tx, tx_type_t* type) {
  if (raw_tx->len < 1) RETURN_VERIFY_ERROR(ctx, "invalid tx data, missing type!");
  *type = raw_tx->data[0];
  if (*type >= 0x7f)
    *type = TX_TYPE_LEGACY; // legacy tx
  else if (*type > 3)
    RETURN_VERIFY_ERROR(ctx, "invalid tx type, must be 1,2,3 or legacy tx!");
  else {
    raw_tx->data++;
    raw_tx->len--;
  }
  return true;
}

bool c4_tx_create_from_address(verify_ctx_t* ctx, bytes_t raw_tx, uint8_t* address) {
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

  if (type == TX_TYPE_EIP4844) RETURN_VERIFY_ERROR(ctx, "invalid tx data, EIP4844 not supported (yet)!");

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

bool c4_tx_verify_tx_data(verify_ctx_t* ctx, ssz_ob_t tx_data, bytes_t serialized_tx, bytes32_t block_hash, uint64_t block_number) {
  // check tx_type

  bytes_t   raw_tx = serialized_tx;
  tx_type_t type   = 0;
  if (!get_and_remove_tx_type(ctx, &raw_tx, &type)) RETURN_VERIFY_ERROR(ctx, "invalid tx data, missing type!");

  // check data
  rlp_type_defs_t defs = tx_type_defs[type];
  if (rlp_decode(&raw_tx, 0, &raw_tx) != RLP_LIST) RETURN_VERIFY_ERROR(ctx, "invalid tx data!");
  int len = rlp_decode(&raw_tx, -1, &raw_tx);
  if (len != defs.len) RETURN_VERIFY_ERROR(ctx, "invalid tx data, missing fields!");
  bytes32_t tmp = {0};

  for (int i = 0; i < len; i++) {
    rlp_def_t  def       = defs.defs[i];
    bytes_t    rlp_value = {0};
    ssz_ob_t   ssz_value = ssz_get(&tx_data, def.name);
    rlp_type_t rlp_type  = rlp_decode(&raw_tx, i, &rlp_value);
    if (rlp_type != (def.len == 2 ? RLP_LIST : RLP_ITEM)) RETURN_VERIFY_ERROR(ctx, "invalid tx data, missing fields!");
    switch (def.len) {
      case 20:
      case 0:
        if (ssz_value.bytes.len != rlp_value.len || memcmp(ssz_value.bytes.data, rlp_value.data, rlp_value.len) != 0) RETURN_VERIFY_ERROR(ctx, "invalid tx data, missing fields!");
        break;
      case 1:
        memset(tmp, 0, 32);
        if (ssz_value.def->type == SSZ_TYPE_VECTOR && ssz_value.bytes.len <= 32)
          memcpy(tmp, rlp_value.data, rlp_value.len);
        else {
          for (int i = 0; i < rlp_value.len; i++)
            tmp[i] = rlp_value.data[rlp_value.len - i - 1];
        }
        if (memcmp(ssz_value.bytes.data, tmp, ssz_value.bytes.len) != 0) RETURN_VERIFY_ERROR(ctx, "invalid tx data, wrong uint!");
        break;
      case 2: // list TODO
        break;
    }
  }
  if (((uint64_t) type) != ssz_get(&tx_data, "type").bytes.data[0]) RETURN_VERIFY_ERROR(ctx, "invalid tx data, type mismatch!");

  // check blocknumber and blockHash
  uint64_t exp_block_number = ssz_get_uint64(&tx_data, "blockNumber");
  bytes_t  exp_block_hash   = ssz_get(&tx_data, "blockHash").bytes;
  if (exp_block_number != block_number) RETURN_VERIFY_ERROR(ctx, "invalid tx data, block number mismatch!");
  if (exp_block_hash.len != 32 || memcmp(exp_block_hash.data, block_hash, 32)) RETURN_VERIFY_ERROR(ctx, "invalid tx data, block number mismatch!");

  uint8_t address[20]  = {0};
  bytes_t from_address = ssz_get(&tx_data, "from").bytes;
  if (!c4_tx_create_from_address(ctx, serialized_tx, address)) RETURN_VERIFY_ERROR(ctx, "invalid tx data, wrong signature!");
  if (from_address.len != 20 || memcmp(from_address.data, address, 20) != 0) RETURN_VERIFY_ERROR(ctx, "invalid from address!");
  // TODO verify signature (from-address)
  return true;
}

bool c4_tx_verify_tx_hash(verify_ctx_t* ctx, bytes_t raw) {
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

bool c4_tx_verify_log_data(verify_ctx_t* ctx, ssz_ob_t log, bytes32_t block_hash, uint64_t block_number, uint32_t tx_index, bytes_t tx_raw, bytes_t receipt_raw) {
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

bool c4_tx_verify_receipt_data(verify_ctx_t* ctx, ssz_ob_t receipt_data, bytes32_t block_hash, uint64_t block_number, uint32_t tx_index, bytes_t tx_raw, bytes_t receipt_raw) {
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

bytes_t c4_eth_create_tx_path(uint32_t tx_index, buffer_t* buf) {
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

bool c4_tx_verify_receipt_proof(verify_ctx_t* ctx, ssz_ob_t receipt_proof, uint32_t tx_index, bytes32_t receipt_root, bytes_t* receipt_raw) {
  bytes32_t tmp      = {0};
  buffer_t  path_buf = stack_buffer(tmp);
  bytes_t   path     = c4_eth_create_tx_path(tx_index, &path_buf);

  if (!patricia_verify(receipt_root, &path, receipt_proof, receipt_raw)) RETURN_VERIFY_ERROR(ctx, "invalid account proof on execution layer!");
  return true;
}