/*
 * Copyright (c) 2025,2026 corpus.core GmbH
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

#include "eth_account.h"
#include "beacon_types.h"
#include "bytes.h"
#include "call_ctx.h"
#include "crypto.h"
#include "eth_call_account.h"
#include "eth_tx.h"
#include "eth_verify.h"
#include "json.h"
#include "logger.h"
#include "patricia.h"
#include "plugin.h"
#include "rlp.h"
#include "ssz.h"
#include "state.h"
#include "sync_committee.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const uint8_t* EMPTY_HASH      = (uint8_t*) "\xc5\xd2\x46\x01\x86\xf7\x23\x3c\x92\x7e\x7d\xb2\xdc\xc7\x03\xc0\xe5\x00\xb6\x53\xca\x82\x27\x3b\x7b\xfa\xd8\x04\x5d\x85\xa4\x70";
const uint8_t* EMPTY_ROOT_HASH = (uint8_t*) "\x56\xe8\x1f\x17\x1b\xcc\x55\xa6\xff\x83\x45\xe6\x92\xc0\xf8\x6e\x5b\x48\xe0\x1b\x99\x6c\xad\xc0\x01\x62\x2f\xb5\xe3\x63\xb4\x21";

/**
 * Verifies the storage proofs of an account against its storage root.
 *
 * Runs before the account itself is authenticated against a block, so nothing read from
 * the proofs may be trusted here: every storage value is checked to be an RLP string of
 * at most 32 bytes before it is copied.
 *
 * @param ctx verification context (error sink)
 * @param storage_proofs the `storageProof` list of the account proof
 * @param storage_hash storage root taken from the account, or the empty root
 * @param values optional buffer of at least 32 bytes receiving the 32 byte big-endian value of proof `i` at offset `i * 32`; proofs beyond its length are verified but not stored
 * @return `true` if every storage proof is a valid inclusion or exclusion proof for its key
 */
static bool verify_storage(verify_ctx_t* ctx, ssz_ob_t storage_proofs, bytes32_t storage_hash, bytes_t values) {
  if (values.data) memset(values.data, 0, 32);
  uint32_t len      = ssz_len(storage_proofs);
  bool     is_empty = memcmp(storage_hash, EMPTY_ROOT_HASH, 32) == 0;
  //  if (len != 0 && memcmp(storage_hash, EMPTY_ROOT_HASH, 32) == 0) RETURN_VERIFY_ERROR(ctx, "invalid storage proof because an empty storage hash can not have values!");
  for (uint32_t i = 0; i < len; i++) {
    bytes32_t path    = {0};
    bytes32_t root    = {0};
    ssz_ob_t  storage = ssz_at(storage_proofs, i);
    ssz_ob_t  proof   = ssz_get(&storage, "proof");
    ssz_ob_t  key     = ssz_get(&storage, "key");
    bytes_t   leaf    = {0};
    bool      store   = values.data && values.len >= (i + 1) * 32;
    if (store) memset(values.data + i * 32, 0, 32); // a slot which does not exist holds zero
    if (is_empty) {
      if (ssz_len(proof) != 0) RETURN_VERIFY_ERROR(ctx, "invalid storage proof because an empty storage hash can not have values!");
      continue;
    }
    keccak(key.bytes, path);
    patricia_result_t result = patricia_verify(root, bytes(path, 32), proof, &leaf);
    if (result == PATRICIA_INVALID) RETURN_VERIFY_ERROR(ctx, "invalid storage proof!");
    if (memcmp(root, storage_hash, 32) != 0) RETURN_VERIFY_ERROR(ctx, "invalid storage root!");
    if (result == PATRICIA_NOT_EXISTING) continue;
    // a storage value is the RLP encoding of an integer of at most 32 bytes
    if (!leaf.data || rlp_decode(&leaf, 0, &leaf) != RLP_ITEM || leaf.len > 32) RETURN_VERIFY_ERROR(ctx, "invalid storage value!");
    if (store) memcpy(values.data + (i + 1) * 32 - leaf.len, leaf.data, leaf.len);
  }

  return true;
}

INTERNAL bool eth_verify_account_proof_exec(verify_ctx_t* ctx, ssz_ob_t* proof, bytes32_t state_root, eth_account_field_t field, bytes_t values) {
  ssz_ob_t  account_proof = ssz_get(proof, "accountProof");
  ssz_ob_t  address       = ssz_get(proof, "address");
  bytes32_t address_hash  = {0};
  bytes_t   rlp_account   = {0};
  bytes_t   field_value   = {0};
  bytes32_t storage_hash  = {0};

  // an account which does not exist has no storage: every slot is proven absent by the empty root
  memcpy(storage_hash, EMPTY_ROOT_HASH, 32);
  keccak(address.bytes, address_hash);

  switch (field) {
    case ETH_ACCOUNT_CODE_HASH:
      memcpy(values.data, EMPTY_HASH, 32);
      break;
    case ETH_ACCOUNT_STORAGE_HASH:
      memcpy(values.data, EMPTY_ROOT_HASH, 32);
      break;
    default:
      memset(values.data, 0, 32);
      break;
  }

  patricia_result_t result = patricia_verify(state_root, bytes(address_hash, 32), account_proof, &rlp_account);
  if (result == PATRICIA_INVALID) RETURN_VERIFY_ERROR(ctx, "invalid account proof on execution layer!");
  if (result == PATRICIA_FOUND) { // 2 means not existing account
    if (!rlp_account.data) RETURN_VERIFY_ERROR(ctx, "invalid account proof on execution layer!");

    if (rlp_decode(&rlp_account, 0, &rlp_account) != RLP_LIST) RETURN_VERIFY_ERROR(ctx, "invalid account proof on execution layer!");
    if (rlp_decode(&rlp_account, 2, &field_value) != RLP_ITEM) RETURN_VERIFY_ERROR(ctx, "invalid account proof for storage hash!");
    // get the storage hash from the proof
    if (field_value.len > 32) RETURN_VERIFY_ERROR(ctx, "invalid account proof for storage hash!");
    memset(storage_hash, 0, 32);
    memcpy(storage_hash, field_value.data, field_value.len);

    // get the field value from the proof
    if (field) {
      if (rlp_decode(&rlp_account, field - 1, &field_value) != RLP_ITEM) RETURN_VERIFY_ERROR(ctx, "invalid account proof on execution layer!");
      if (field_value.len > 32) RETURN_VERIFY_ERROR(ctx, "invalid account proof on execution layer!");
      memcpy(values.data + 32 - field_value.len, field_value.data, field_value.len);
    }
  }

  if (!verify_storage(ctx, ssz_get(proof, "storageProof"), storage_hash, field == ETH_ACCOUNT_STORAGE_HASH ? values : NULL_BYTES)) RETURN_VERIFY_ERROR(ctx, "invalid storage proof!");

  return true;
}

bool eth_get_storage_value(ssz_ob_t storage, const bytes32_t key, bytes32_t value) {
  bytes32_t path = {0};
  bytes32_t root = {0};
  bytes_t   leaf = {0};
  keccak(bytes(key, 32), path);
  patricia_result_t result = patricia_verify(root, bytes(path, 32), ssz_get(&storage, "proof"), &leaf);
  if (result == PATRICIA_INVALID) return false;
  if (result == PATRICIA_NOT_EXISTING) return true; // value is 0x0
  if (rlp_decode(&leaf, 0, &leaf) != RLP_ITEM) return false;
  if (leaf.len > 32) return false;
  memcpy(value + 32 - leaf.len, leaf.data, leaf.len);
  return true;
}

// Number of fields of an RLP-encoded account: [nonce, balance, storageHash, codeHash].
#define ACCOUNT_RLP_FIELDS       4
#define ACCOUNT_RLP_STORAGE_HASH 2

/**
 * Verifies an account proof that carries exactly one storage proof and returns the proven
 * key and value. Shared core of `eth_verify_storage_slot` and `eth_verify_storage_proof`.
 *
 * @param state error sink
 * @param account `EthAccountProof` container (`accountProof`, `address`, `storageProof`); `elProof` is not read
 * @param address 20-byte address of the contract the proof must be about
 * @param expected_slot 32-byte storage key the proof must be about, or NULL to accept the key of the proof
 * @param state_root 32-byte state root of the block
 * @param slot receives the 32-byte storage key of the proof (may be NULL)
 * @param value receives the 32-byte big-endian storage value (left-padded)
 * @return `C4_SUCCESS` or `C4_ERROR`
 */
static c4_status_t verify_single_storage_proof(c4_state_t* state, ssz_ob_t account, const address_t address, const uint8_t* expected_slot, bytes_t state_root, uint8_t* slot, bytes32_t value) {
  bytes32_t path         = {0};
  bytes32_t root         = {0};
  bytes_t   rlp_account  = NULL_BYTES;
  bytes_t   storage_hash = NULL_BYTES;
  bytes_t   leaf         = NULL_BYTES;

  memset(value, 0, 32);
  if (!account.def) return c4_state_add_error(state, "invalid account proof!"); // `ssz_get` needs a definition
  ssz_ob_t proven_address = ssz_get(&account, "address");
  ssz_ob_t account_proof  = ssz_get(&account, "accountProof");
  ssz_ob_t storage_list   = ssz_get(&account, "storageProof");
  if (!state_root.data || state_root.len != 32) return c4_state_add_error(state, "missing state root for the storage proof!");
  if (!proven_address.def || !account_proof.def || !storage_list.def) return c4_state_add_error(state, "invalid account proof!");

  // 1. the proof must be about the expected contract, and about nothing but the expected slot
  if (proven_address.bytes.len != ADDRESS_SIZE || memcmp(proven_address.bytes.data, address, ADDRESS_SIZE) != 0)
    return c4_state_add_error(state, "account proof is not about the expected contract!");
  if (ssz_len(storage_list) != 1)
    return c4_state_add_error(state, "account proof must contain exactly one storage proof!");

  ssz_ob_t storage = ssz_at(storage_list, 0);
  ssz_ob_t key     = ssz_get(&storage, "key");
  ssz_ob_t proof   = ssz_get(&storage, "proof");
  if (!key.def || !proof.def || key.bytes.len != 32) return c4_state_add_error(state, "invalid storage proof!");
  if (expected_slot && memcmp(key.bytes.data, expected_slot, 32) != 0)
    return c4_state_add_error(state, "storage proof is not about the expected slot!");
  if (slot) memcpy(slot, key.bytes.data, 32);

  // 2. account proof against the state root. An exclusion proof is not acceptable:
  //    without the account there is no value.
  keccak(proven_address.bytes, path);
  if (patricia_verify(root, bytes(path, 32), account_proof, &rlp_account) != PATRICIA_FOUND || !rlp_account.data || !rlp_account.len)
    return c4_state_add_error(state, "invalid account proof of the contract!");
  if (memcmp(root, state_root.data, 32) != 0)
    return c4_state_add_error(state, "account proof of the contract does not match the state root!");
  if (rlp_decode(&rlp_account, 0, &rlp_account) != RLP_LIST || (int) rlp_decode(&rlp_account, -1, NULL) != ACCOUNT_RLP_FIELDS)
    return c4_state_add_error(state, "invalid account of the contract!");
  if (rlp_decode(&rlp_account, ACCOUNT_RLP_STORAGE_HASH, &storage_hash) != RLP_ITEM || storage_hash.len != 32)
    return c4_state_add_error(state, "invalid storage hash of the contract!");

  // 3. storage proof against the storage hash of the proven account. The value must exist.
  memset(root, 0, 32);
  keccak(key.bytes, path);
  if (patricia_verify(root, bytes(path, 32), proof, &leaf) != PATRICIA_FOUND || !leaf.data || !leaf.len)
    return c4_state_add_error(state, "invalid storage proof of the slot!");
  if (memcmp(root, storage_hash.data, 32) != 0)
    return c4_state_add_error(state, "storage proof of the slot does not match the storage hash of the contract!");
  if (rlp_decode(&leaf, 0, &leaf) != RLP_ITEM || !leaf.len || leaf.len > 32)
    return c4_state_add_error(state, "invalid storage value of the slot!");

  // storage values are stored without leading zeros
  memcpy(value + 32 - leaf.len, leaf.data, leaf.len);
  return C4_SUCCESS;
}

c4_status_t eth_verify_storage_slot(c4_state_t* state, ssz_ob_t account, const address_t address, const bytes32_t slot, bytes_t state_root, bytes32_t value) {
  return verify_single_storage_proof(state, account, address, slot, state_root, NULL, value);
}

c4_status_t eth_verify_storage_proof(c4_state_t* state, ssz_ob_t account, const address_t address, bytes_t state_root, bytes32_t slot, bytes32_t value) {
  memset(slot, 0, 32);
  return verify_single_storage_proof(state, account, address, NULL, state_root, slot, value);
}

INTERNAL c4_status_t eth_fetch_account_code(verify_ctx_t* ctx, call_account_t* ac) {
  char             tmp[200];
  storage_plugin_t cache  = {0};
  c4_status_t      status = C4_SUCCESS;
  buffer_t         buf    = stack_buffer(tmp);

  c4_get_storage_config(&cache);
  bprintf(&buf, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\": \"eth_getCode\", \"params\": [\"0x%x\", \"latest\"]}", bytes(ac->address, 20));
  bytes32_t hash = {0};
  c4_chain_request_id(hash, ctx->chain_id, buf.data);
  data_request_t* req = c4_state_get_data_request_by_id(&ctx->state, hash);
  if (req && req->response.data) {
    buffer_reset(&buf);
    json_t response = json_parse((char*) req->response.data);
    json_t result   = json_get(response, "result");
    if (!req->validated) {
      CHECK_JSON(result, "bytes", "Invalid results for Code: ");
      req->validated = true;
    }
    if (result.type == JSON_TYPE_STRING) {
      buffer_t code_data = {0};
      ac->code           = json_as_bytes(result, &code_data);
      ac->flags |= ACCOUNT_HAS_CODE | ACCOUNT_FREE_CODE;

      keccak(ac->code, hash);
      if (ac->flags & ACCOUNT_HAS_CODE_HASH && memcmp(hash, ac->code_hash, 32) != 0) {
        safe_free(ac->code.data);
        ac->code = NULL_BYTES;
        ac->flags &= ~(ACCOUNT_HAS_CODE | ACCOUNT_FREE_CODE);
        status = c4_state_add_error(&ctx->state, "code hash mismatch");
      }
      else {
        if (!(ac->flags & ACCOUNT_HAS_CODE_HASH)) { // update the code hash in papmode
          ac->flags |= ACCOUNT_HAS_CODE_HASH;
          keccak(ac->code, ac->code_hash);
        }
        cache.set(bprintf(&buf, "code_%x", bytes(ac->code_hash, 32)), ac->code);
      }
    }
    else
      status = c4_state_add_error(&ctx->state, bprintf(&buf, "error fetching code from rpc: %s", req->response.data));
  }
  else if (req && req->error)
    status = c4_state_add_error(&ctx->state, req->error);
  else {
    data_request_t* new_req = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
    new_req->chain_id       = ctx->chain_id;
    new_req->encoding       = C4_DATA_ENCODING_JSON;
    new_req->type           = C4_DATA_TYPE_ETH_RPC;
    new_req->payload        = bytes_dup(buf.data);
    new_req->method         = C4_DATA_METHOD_POST;
    memcpy(new_req->id, hash, 32);
    c4_state_add_request(&ctx->state, new_req);
    status = C4_PENDING;
  }
  return status;
}

INTERNAL c4_status_t eth_resolve_account_codes(verify_ctx_t* ctx, call_account_t* accounts) {
  c4_status_t      status = C4_SUCCESS;
  storage_plugin_t cache  = {0};
  char             tmp[200];
  buffer_t         buf = stack_buffer(tmp);
  c4_get_storage_config(&cache);

  for (call_account_t* ac = accounts; ac; ac = ac->next) {
    if (ac->flags & ACCOUNT_HAS_CODE) continue;
    if (!(ac->flags & ACCOUNT_HAS_CODE_HASH)) continue;
    if (memcmp(ac->code_hash, EMPTY_HASH, 32) == 0) {
      ac->code = NULL_BYTES;
      ac->flags |= ACCOUNT_HAS_CODE;
      continue;
    }

    buffer_reset(&buf);
    buffer_t data = {0};
    if (cache.get && cache.get(bprintf(&buf, "code_%x", bytes(ac->code_hash, 32)), &data)) {
      bytes32_t got_hash = {0};
      keccak(data.data, got_hash); // reject cache entries that do not match code_hash
      if (memcmp(got_hash, ac->code_hash, 32) == 0) {
        ac->code = data.data;
        ac->flags |= ACCOUNT_HAS_CODE | ACCOUNT_FREE_CODE;
        continue;
      }
      log_warn("Rejected cached bytecode at %s: keccak mismatch (got 0x%x, expected 0x%x, len %d)",
               (char*) buf.data.data, bytes(got_hash, 32), bytes(ac->code_hash, 32), data.data.len);
      buffer_free(&data);
    }

    c4_status_t fetch_status = eth_fetch_account_code(ctx, ac);
    if (status != C4_ERROR) status = fetch_status;
  }

  return status;
}

eth_account_field_t eth_account_get_field(verify_ctx_t* ctx) {
  if (ctx->method && strcmp(ctx->method, "eth_getBalance") == 0) return ETH_ACCOUNT_BALANCE;
  if (ctx->method && strcmp(ctx->method, "eth_getStorageAt") == 0) return ETH_ACCOUNT_STORAGE_HASH;
  if (ctx->method && strcmp(ctx->method, "eth_getCode") == 0) return ETH_ACCOUNT_CODE_HASH;
  if (ctx->method && strcmp(ctx->method, "eth_getTransactionCount") == 0) return ETH_ACCOUNT_NONCE;
  if (ctx->method && strcmp(ctx->method, "eth_getProof") == 0) return ETH_ACCOUNT_PROOF;
  return ETH_ACCOUNT_NONE;
}

/**
 * Returns the RLP encoded account an account proof holds for its address.
 *
 * The last node of an account proof is only the account if the proof is an inclusion
 * proof. The proof of an account which does not exist may end in the leaf of another
 * account, whose values must not be returned.
 *
 * @param proof the account proof (`accountProof` and `address`), already verified against the state root
 * @return the RLP encoded account, or `NULL_BYTES` if the account does not exist
 */
static bytes_t get_account(ssz_ob_t* proof) {
  bytes32_t path    = {0};
  bytes32_t root    = {0};
  bytes_t   account = {0};
  keccak(ssz_get(proof, "address").bytes, path);
  if (patricia_verify(root, bytes(path, 32), ssz_get(proof, "accountProof"), &account) != PATRICIA_FOUND) return NULL_BYTES;
  return account;
}

bool eth_account_verify_data(verify_ctx_t* ctx, address_t verified_address, eth_account_field_t field, bytes_t values) {
  ssz_ob_t  data           = ctx->data;
  bytes32_t expected_value = {0};
  buffer_t  address_buf    = stack_buffer(expected_value);
  bytes_t   req_address    = json_as_bytes(json_at(ctx->args, 0), &address_buf);
  // the address has to be readable: a value which can not be compared must not pass
  if (!req_address.data || req_address.len != 20 || memcmp(req_address.data, verified_address, 20) != 0) RETURN_VERIFY_ERROR(ctx, "proof does not match the address in request");
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
        bytes_t       account       = get_account(&ctx->proof);
        bytes_t       value         = {0};
        // Address is authoritative from the proof container (verified against the
        // trie path when the account leaf is checked).
        ssz_add_bytes(&builder, "address", ssz_get(&ctx->proof, "address").bytes);
        if (account.data && rlp_decode(&account, 0, &account) == RLP_LIST && rlp_decode(&account, -1, NULL) == 4) {
          ssz_builder_t storage_list_builder = ssz_builder_for_def(ssz_get_def(builder.def, "storageProof"));
          for (int i = 0; i < values.len / 32; i++) {
            ssz_builder_t storage_builder = ssz_builder_for_def(storage_list_builder.def->def.vector.type);
            ssz_ob_t      storage         = ssz_at(storage_proof, i);
            ssz_add_bytes(&storage_builder, "key", ssz_get(&storage, "key").bytes);
            // Storage value is a QUANTITY per eth_getProof spec, stored as UINT256 (little-endian in SSZ).
            // `values` is 32-byte big-endian (right-aligned trie leaf), ssz_add_uint256 performs the byte swap.
            ssz_add_uint256(&storage_builder, bytes(values.data + i * 32, 32));
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
  }
  else if (field == ETH_ACCOUNT_CODE_HASH) {
    keccak(data.bytes, expected_value);
    if (memcmp(expected_value, values.data, 32)) RETURN_VERIFY_ERROR(ctx, "invalid code hash!");
  }
  else
    RETURN_VERIFY_ERROR(ctx, "invalid usage of account proof data!");

  return true;
}
