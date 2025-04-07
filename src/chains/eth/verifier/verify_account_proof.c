
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
#include <alloca.h>
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
  if (ctx->method && strcmp(ctx->method, "eth_getProof") == 0) return ETH_ACCOUNT_PROOF;
  return ETH_ACCOUNT_NONE;
}

static bytes_t get_leaf(ssz_ob_t proof) {
  bytes_t node = ssz_at(proof, ssz_len(proof) - 1).bytes;
  if (rlp_decode(&node, 0, &node) != RLP_LIST) return NULL_BYTES;
  int len = rlp_decode(&node, -1, NULL);

  if (len == 17)
    rlp_decode(&node, 16, &node);
  else if (len == 2)
    rlp_decode(&node, 1, &node);
  else
    return NULL_BYTES;

  print_hex(stderr, node, "LEAF: ", "\n");
  return node;
}

static bool verify_data(verify_ctx_t* ctx, address_t verified_address, eth_account_field_t field, bytes_t values) {
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
        buffer_append(&builder.fixed, values);
        ctx->data = ssz_builder_to_bytes(&builder);
        break;
      }
      case ETH_ACCOUNT_BALANCE:
      case ETH_ACCOUNT_NONCE: {
        ssz_builder_t builder = ssz_builder_for_type(ETH_SSZ_DATA_UINT256);
        ssz_add_uint256(&builder, values);
        ctx->data = ssz_builder_to_bytes(&builder);
        break;
      }
      case ETH_ACCOUNT_PROOF: {
        ssz_builder_t builder       = ssz_builder_for_type(ETH_SSZ_DATA_PROOF);
        ssz_ob_t      storage_proof = ssz_get(&ctx->proof, "storageProof");
        bytes_t       account       = get_leaf(ssz_get(&ctx->proof, "accountProof"));
        bytes_t       value         = {0};
        if (account.data && rlp_decode(&account, 0, &account) == RLP_LIST && rlp_decode(&account, -1, NULL) == 4) {
          ssz_builder_t storage_list_builder = ssz_builder_for_def(ssz_get_def(builder.def, "storageProof"));
          for (int i = 0; i < values.len / 32; i++) {
            ssz_builder_t storage_builder = ssz_builder_for_def(storage_list_builder.def->def.vector.type);
            ssz_ob_t      storage         = ssz_at(storage_proof, i);
            ssz_add_bytes(&storage_builder, "key", ssz_get(&storage, "key").bytes);
            ssz_add_bytes(&storage_builder, "value", bytes(values.data + i * 32, 32));
            ssz_add_bytes(&storage_builder, "proof", ssz_get(&storage, "proof").bytes);
            ssz_add_dynamic_list_builders(&storage_list_builder, values.len / 32, storage_builder);
          }
          rlp_decode(&account, 1, &value);
          ssz_add_uint256(&builder, value); // balance
          rlp_decode(&account, 3, &value);
          ssz_add_bytes(&builder, "codeHash", value);
          rlp_decode(&account, 0, &value);
          ssz_add_uint256(&builder, value); // nonce
          rlp_decode(&account, 2, &value);
          ssz_add_bytes(&builder, "storageHash", value);
          ssz_add_bytes(&builder, "accountProof", ssz_get(&ctx->proof, "accountProof").bytes);
          ssz_add_builders(&builder, "storageProof", storage_list_builder);
        }
        else {
          ssz_add_bytes(&builder, "balance", bytes(0, 32));
          ssz_add_bytes(&builder, "codeHash", bytes(EMPTY_HASH, 32));
          ssz_add_bytes(&builder, "nonce", bytes(0, 32));
          ssz_add_bytes(&builder, "storageHash", bytes(EMPTY_ROOT_HASH, 32));
          ssz_add_bytes(&builder, "accountProof", ssz_get(&ctx->proof, "accountProof").bytes);
          ssz_add_bytes(&builder, "storageProof", NULL_BYTES);
        }

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
  else if (field == ETH_ACCOUNT_PROOF) // we already took the proof from the verifified account.
    return true;
  else if (data.bytes.len > 32)
    RETURN_VERIFY_ERROR(ctx, "invalid data!");
  else if (data.def->type == SSZ_TYPE_UINT) {
    for (int i = 0; i < data.bytes.len; i++)
      expected_value[31 - i] = data.bytes.data[i];
  }
  else
    memcpy(expected_value + 32 - data.bytes.len, data.bytes.data, data.bytes.len);

  return memcmp(expected_value, values.data, 32) == 0;
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
  uint32_t            storage_keys_len         = ssz_len(ssz_get(&ctx->proof, "storageProof"));
  bytes_t             values                   = field == ETH_ACCOUNT_PROOF ? bytes(alloca(32 * storage_keys_len), 32 * storage_keys_len) : bytes(value, 32);

  if (!eth_verify_account_proof_exec(ctx, &ctx->proof, state_root, field == ETH_ACCOUNT_PROOF ? ETH_ACCOUNT_STORAGE_HASH : field, values)) RETURN_VERIFY_ERROR(ctx, "invalid account proof!");
  ssz_verify_single_merkle_proof(state_merkle_proof.bytes, state_root, STATE_ROOT_GINDEX, body_root);
  if (memcmp(body_root, ssz_get(&header, "bodyRoot").bytes.data, 32) != 0) RETURN_VERIFY_ERROR(ctx, "invalid body root!");
  if (c4_verify_blockroot_signature(ctx, &header, &sync_committee_bits, &sync_committee_signature, 0) != C4_SUCCESS) return false;
  if (field && !verify_data(ctx, verified_address.data, field, values)) RETURN_VERIFY_ERROR(ctx, "invalid account data!");

  ctx->success = true;
  return true;
}