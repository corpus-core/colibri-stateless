
#include "../util/bytes.h"
#include "../util/crypto.h"
#include "../util/json.h"
#include "../util/patricia.h"
#include "../util/rlp.h"
#include "../util/ssz.h"
#include "sync_committee.h"
#include "types_verify.h"
#include "verify.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GINDEX_BLOCKUMBER 806
#define GINDEX_BLOCHASH   812
#define GINDEX_TXINDEX_G  1704984576L // gindex of the first tx

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
    // [chain_id, nonce, max_priority_fee_per_gas, max_fee_per_gas, gas_limit, to, value, data, access_list, max_fee_per_blob_gas, blob_versioned_hashes, y_parity, r, s]
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
static bool create_from_address(verify_ctx_t* ctx, bytes_t raw_tx, uint8_t* address) {
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

static bool verify_tx_data(verify_ctx_t* ctx, ssz_ob_t tx_data, bytes_t serialized_tx, bytes32_t block_hash, uint64_t block_number) {
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

  // check blocknumber and blockHash
  uint64_t exp_block_number = ssz_get_uint64(&tx_data, "blockNumber");
  bytes_t  exp_block_hash   = ssz_get(&tx_data, "blockHash").bytes;
  if (exp_block_number != block_number) RETURN_VERIFY_ERROR(ctx, "invalid tx data, block number mismatch!");
  if (exp_block_hash.len != 32 || memcmp(exp_block_hash.data, block_hash, 32)) RETURN_VERIFY_ERROR(ctx, "invalid tx data, block number mismatch!");

  uint8_t address[20]  = {0};
  bytes_t from_address = ssz_get(&tx_data, "from").bytes;
  if (!create_from_address(ctx, serialized_tx, address)) RETURN_VERIFY_ERROR(ctx, "invalid tx data, wrong signature!");
  if (from_address.len != 20 || memcmp(from_address.data, address, 20) != 0) RETURN_VERIFY_ERROR(ctx, "invalid from address!");
  // TODO verify signature (from-address)
  return true;
}

static bool verify_tx_hash(verify_ctx_t* ctx, bytes_t raw) {
  if (ctx->method == NULL) return true;
  if (strcmp(ctx->method, "eth_getTransactionByHash") == 0) {
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

static bool verify_merkle_proof(verify_ctx_t* ctx, ssz_ob_t proof, bytes_t block_hash, bytes_t block_number, bytes_t raw, uint32_t tx_index, bytes32_t body_root) {
  uint8_t   leafes[96] = {0};                                                               // 3 leafes, 32 bytes each
  bytes32_t root_hash  = {0};                                                               // calculated body root hash
  gindex_t  gindexes[] = {GINDEX_BLOCKUMBER, GINDEX_BLOCHASH, GINDEX_TXINDEX_G + tx_index}; // calculate the gindexes for the proof

  // copy leaf data
  memcpy(leafes, block_number.data, block_number.len);
  memcpy(leafes + 32, block_hash.data, block_hash.len);
  ssz_hash_tree_root(ssz_ob(ssz_transactions_bytes, raw), leafes + 64);

  if (!ssz_verify_multi_merkle_proof(proof.bytes, bytes(leafes, sizeof(leafes)), gindexes, root_hash)) RETURN_VERIFY_ERROR(ctx, "invalid tx proof, missing nodes!");
  if (memcmp(root_hash, body_root, 32) != 0) RETURN_VERIFY_ERROR(ctx, "invalid tx proof, body root mismatch!");
  return true;
}

bool verify_tx_proof(verify_ctx_t* ctx) {
  ctx->type = PROOF_TYPE_TRANSACTION;

  ssz_ob_t raw                      = ssz_get(&ctx->proof, "transaction");
  ssz_ob_t tx_proof                 = ssz_get(&ctx->proof, "proof");
  ssz_ob_t tx_index                 = ssz_get(&ctx->proof, "transactionIndex");
  ssz_ob_t header                   = ssz_get(&ctx->proof, "header");
  ssz_ob_t sync_committee_bits      = ssz_get(&ctx->proof, "sync_committee_bits");
  ssz_ob_t sync_committee_signature = ssz_get(&ctx->proof, "sync_committee_signature");
  ssz_ob_t block_hash               = ssz_get(&ctx->proof, "blockHash");
  ssz_ob_t block_number             = ssz_get(&ctx->proof, "blockNumber");
  ssz_ob_t body_root                = ssz_get(&header, "bodyRoot");

  if (ssz_is_error(header) || ssz_is_error(raw) || ssz_is_error(tx_index) || ssz_is_error(body_root) || body_root.bytes.len != 32 || ssz_is_error(tx_proof) || ssz_is_error(block_hash) || block_hash.bytes.len != 32 || ssz_is_error(block_number)) RETURN_VERIFY_ERROR(ctx, "invalid proof, missing header or blockhash_proof!");
  if (ssz_is_error(sync_committee_bits) || sync_committee_bits.bytes.len != 64 || ssz_is_error(sync_committee_signature) || sync_committee_signature.bytes.len != 96) RETURN_VERIFY_ERROR(ctx, "invalid proof, missing sync committee bits or signature!");

  if (!verify_tx_data(ctx, ctx->data, raw.bytes, block_hash.bytes.data, ssz_uint64(block_number))) RETURN_VERIFY_ERROR(ctx, "invalid tx data!");
  if (!verify_tx_hash(ctx, raw.bytes)) RETURN_VERIFY_ERROR(ctx, "invalid tx hash!");
  if (!verify_merkle_proof(ctx, tx_proof, block_hash.bytes, block_number.bytes, raw.bytes, ssz_uint32(tx_index), body_root.bytes.data)) RETURN_VERIFY_ERROR(ctx, "invalid tx proof!");
  if (!c4_verify_blockroot_signature(ctx, &header, &sync_committee_bits, &sync_committee_signature, 0)) RETURN_VERIFY_ERROR(ctx, "invalid blockhash signature!");

  ctx->success = true;
  return true;
}