
#include "../util/bytes.h"
#include "../util/crypto.h"
#include "../util/json.h"
#include "../util/patricia.h"
#include "../util/rlp.h"
#include "../util/ssz.h"
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

static bool verify_tx_data(verify_ctx_t* ctx, ssz_ob_t tx_data, bytes_t raw_tx) {
  // check tx_type
  if (raw_tx.len < 1) RETURN_VERIFY_ERROR(ctx, "invalid tx data, missing type!");
  tx_type_t type = raw_tx.data[0];
  if (type >= 0x7f)
    type = TX_TYPE_LEGACY; // legacy tx
  else if (type > 3)
    RETURN_VERIFY_ERROR(ctx, "invalid tx type, must be 1,2,3 or legacy tx!");
  else {
    raw_tx.data++;
    raw_tx.len--;
  }

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
  return true;
}

bool verify_tx_proof(verify_ctx_t* ctx) {
  ctx->type = PROOF_TYPE_TRANSACTION;

  bytes32_t body_root                = {0};
  ssz_ob_t  raw                      = ssz_get(&ctx->proof, "transaction");
  ssz_ob_t  tx_proof                 = ssz_get(&ctx->proof, "proof");
  ssz_ob_t  header                   = ssz_get(&ctx->proof, "header");
  ssz_ob_t  sync_committee_bits      = ssz_get(&ctx->proof, "sync_committee_bits");
  ssz_ob_t  sync_committee_signature = ssz_get(&ctx->proof, "sync_committee_signature");

  if (ssz_is_error(header) || ssz_is_error(raw) || ssz_is_error(tx_proof)) RETURN_VERIFY_ERROR(ctx, "invalid proof, missing header or blockhash_proof!");
  if (ssz_is_error(sync_committee_bits) || sync_committee_bits.bytes.len != 64 || ssz_is_error(sync_committee_signature) || sync_committee_signature.bytes.len != 96) RETURN_VERIFY_ERROR(ctx, "invalid proof, missing sync committee bits or signature!");
  // if (!verified_address.data || verified_address.len != 20 || !ctx->data.def || !ssz_is_type(&ctx->data, &ssz_bytes32) || ctx->data.bytes.data == NULL || ctx->data.bytes.len != 32) RETURN_VERIFY_ERROR(ctx, "invalid data, data is not a bytes32!");

  if (!verify_tx_data(ctx, ctx->data, raw.bytes)) RETURN_VERIFY_ERROR(ctx, "invalid tx data!");

  //  if (!verify_account_proof_exec(ctx, &ctx->proof, state_root)) RETURN_VERIFY_ERROR(ctx, "invalid account proof!");
  //   ssz_verify_single_merkle_proof(state_merkle_proof.bytes, state_root, STATE_ROOT_GINDEX, body_root);
  //  if (memcmp(body_root, ssz_get(&header, "bodyRoot").bytes.data, 32) != 0) RETURN_VERIFY_ERROR(ctx, "invalid body root!");
  if (!c4_verify_blockroot_signature(ctx, &header, &sync_committee_bits, &sync_committee_signature, 0)) RETURN_VERIFY_ERROR(ctx, "invalid blockhash signature!");

  ctx->success = true;
  return true;
}