
#include "beacon_types.h"
#include "bytes.h"
#include "crypto.h"
#include "eth_account.h"
#include "eth_verify.h"
#include "json.h"
#include "patricia.h"
#include "rlp.h"
#include "ssz.h"
#include "sync_committee.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static eth_account_field_t get_field(verify_ctx_t* ctx) {
  if (ctx->method && strcmp(ctx->method, "eth_getBalance") == 0) return ETH_ACCOUNT_BALANCE;
  if (ctx->method && strcmp(ctx->method, "eth_getStorageAt") == 0) return ETH_ACCOUNT_STORAGE_HASH;
  if (ctx->method && strcmp(ctx->method, "eth_getCode") == 0) return ETH_ACCOUNT_CODE_HASH;
  if (ctx->method && strcmp(ctx->method, "eth_getTransactionCount") == 0) return ETH_ACCOUNT_NONCE;
  return ETH_ACCOUNT_NONE;
}

static bool verify_data(verify_ctx_t* ctx, address_t verified_address, eth_account_field_t field, bytes32_t value) {
  ssz_ob_t  data           = ctx->data;
  bytes32_t expected_value = {0};
  buffer_t  address_buf    = stack_buffer(expected_value);
  bytes_t   req_address    = json_as_bytes(json_at(ctx->args, 0), &address_buf);
  if (req_address.data && (req_address.len != 20 || memcmp(req_address.data, verified_address, 20) != 0)) RETURN_VERIFY_ERROR(ctx, "proof does not match the address in request");
  if (!data.def) RETURN_VERIFY_ERROR(ctx, "invalid data!");
  if (data.def->type == SSZ_TYPE_NONE) {
    switch (field) {
      case ETH_ACCOUNT_CODE_HASH: RETURN_VERIFY_ERROR(ctx, "no code included!");
      case ETH_ACCOUNT_STORAGE_HASH: {

        ssz_builder_t builder = ssz_builder_for_type(ETH_SSZ_DATA_HASH32);
        buffer_append(&builder.fixed, bytes(value, 32));
        ctx->data = ssz_builder_to_bytes(&builder);
        break;
      }
      case ETH_ACCOUNT_BALANCE:
      case ETH_ACCOUNT_NONCE: {
        ssz_builder_t builder = ssz_builder_for_type(ETH_SSZ_DATA_UINT256);
        ssz_add_uint256(&builder, bytes(value, 32));
        ctx->data = ssz_builder_to_bytes(&builder);
        break;
      }
      default:
        RETURN_VERIFY_ERROR(ctx, "invalid data!");
    }
    ctx->flags |= VERIFY_FLAG_FREE_DATA;
    data = ctx->data;
  }

  memset(expected_value, 0, 32);
  if (field == ETH_ACCOUNT_CODE_HASH)
    keccak(data.bytes, expected_value);
  else if (data.bytes.len > 32)
    RETURN_VERIFY_ERROR(ctx, "invalid data!");
  else if (data.def->type == SSZ_TYPE_UINT) {
    for (int i = 0; i < data.bytes.len; i++)
      expected_value[31 - i] = data.bytes.data[i];
  }
  else
    memcpy(expected_value + 32 - data.bytes.len, data.bytes.data, data.bytes.len);

  return memcmp(expected_value, value, 32) == 0;
}

bool verify_account_proof(verify_ctx_t* ctx) {
  bytes32_t           body_root                = {0};
  bytes32_t           state_root               = {0};
  ssz_ob_t            state_proof              = ssz_get(&ctx->proof, "state_proof");
  ssz_ob_t            state_merkle_proof       = ssz_get(&state_proof, "state_proof");
  ssz_ob_t            header                   = ssz_get(&state_proof, "header");
  ssz_ob_t            sync_committee_bits      = ssz_get(&state_proof, "sync_committee_bits");
  ssz_ob_t            sync_committee_signature = ssz_get(&state_proof, "sync_committee_signature");
  bytes_t             verified_address         = ssz_get(&ctx->proof, "address").bytes;
  eth_account_field_t field                    = get_field(ctx);
  bytes32_t           value                    = {0};

  if (!eth_verify_account_proof_exec(ctx, &ctx->proof, state_root, field, value)) RETURN_VERIFY_ERROR(ctx, "invalid account proof!");
  ssz_verify_single_merkle_proof(state_merkle_proof.bytes, state_root, STATE_ROOT_GINDEX, body_root);
  if (memcmp(body_root, ssz_get(&header, "bodyRoot").bytes.data, 32) != 0) RETURN_VERIFY_ERROR(ctx, "invalid body root!");
  if (c4_verify_blockroot_signature(ctx, &header, &sync_committee_bits, &sync_committee_signature, 0) != C4_SUCCESS) return false;
  if (field && !verify_data(ctx, verified_address.data, field, value)) RETURN_VERIFY_ERROR(ctx, "invalid account data!");

  ctx->success = true;
  return true;
}