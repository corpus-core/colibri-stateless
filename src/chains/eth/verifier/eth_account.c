
#include "eth_account.h"
#include "bytes.h"
#include "crypto.h"
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
static const uint8_t* EMPTY_HASH      = (uint8_t*) "\xc5\xd2\x46\x01\x86\xf7\x23\x3c\x92\x7e\x7d\xb2\xdc\xc7\x03\xc0\xe5\x00\xb6\x53\xca\x82\x27\x3b\x7b\xfa\xd8\x04\x5d\x85\xa4\x70";
static const uint8_t* EMPTY_ROOT_HASH = (uint8_t*) "\x56\xe8\x1f\x17\x1b\xcc\x55\xa6\xff\x83\x45\xe6\x92\xc0\xf8\x6e\x5b\x48\xe0\x1b\x99\x6c\xad\xc0\x01\x62\x2f\xb5\xe3\x63\xb4\x21";
static void           remove_leading_zeros(bytes_t* value) {
  while (value->len > 0 && value->data[0] == 0) {
    value->data++;
    value->len--;
  }
}
static bool is_equal(ssz_ob_t expect, bytes_t* list, int index) {
  bytes_t value;
  if (rlp_decode(list, index, &value) != RLP_ITEM) return false;
  bytes_t exp = expect.bytes;
  remove_leading_zeros(&value);
  remove_leading_zeros(&exp);
  return value.len == exp.len && memcmp(exp.data, value.data, exp.len) == 0;
}

static bool verify_storage(verify_ctx_t* ctx, ssz_ob_t storage_proofs, bytes32_t storage_hash) {
  int len = ssz_len(storage_proofs);
  if (len != 0 && memcmp(storage_hash, EMPTY_ROOT_HASH, 32) == 0) RETURN_VERIFY_ERROR(ctx, "invalid storage proof because an empty storage hash can not have values!");
  for (int i = 0; i < len; i++) {
    bytes32_t path    = {0};
    bytes32_t root    = {0};
    ssz_ob_t  storage = ssz_at(storage_proofs, i);
    ssz_ob_t  proof   = ssz_get(&storage, "proof");
    ssz_ob_t  key     = ssz_get(&storage, "key");
    bytes_t   leaf    = {0};
    keccak(key.bytes, path);
    if (patricia_verify(root, bytes(path, 32), proof, &leaf) == PATRICIA_INVALID) RETURN_VERIFY_ERROR(ctx, "invalid storage proof!");
    if (memcmp(root, storage_hash, 32) != 0) RETURN_VERIFY_ERROR(ctx, "invalid storage root!");
  }

  return true;
}

bool eth_verify_account_proof_exec(verify_ctx_t* ctx, ssz_ob_t* proof, bytes32_t state_root, eth_account_field_t field, bytes32_t value) {
  ssz_ob_t  account_proof = ssz_get(proof, "accountProof");
  ssz_ob_t  address       = ssz_get(proof, "address");
  bytes32_t address_hash  = {0};
  bytes_t   rlp_account   = {0};
  bytes_t   field_value   = {0};
  bytes32_t storage_hash  = {0};

  keccak(address.bytes, address_hash);

  switch (field) {
    case ETH_ACCOUNT_CODE_HASH:
      memcpy(value, EMPTY_HASH, 32);
      break;
    case ETH_ACCOUNT_STORAGE_HASH:
      memcpy(value, EMPTY_ROOT_HASH, 32);
      break;
    default:
      memset(value, 0, 32);
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
    memcpy(storage_hash, field_value.data, field_value.len);

    // get the field value from the proof
    if (field) {
      if (rlp_decode(&rlp_account, field - 1, &field_value) != RLP_ITEM) RETURN_VERIFY_ERROR(ctx, "invalid account proof on execution layer!");
      if (field_value.len > 32) RETURN_VERIFY_ERROR(ctx, "invalid account proof on execution layer!");
      memcpy(value + 32 - field_value.len, field_value.data, field_value.len);
    }
  }

  if (!verify_storage(ctx, ssz_get(proof, "storageProof"), storage_hash)) RETURN_VERIFY_ERROR(ctx, "invalid storage proof!");

  return true;
}

static bytes_t get_last_value(ssz_ob_t proof) {
  bytes_t last_value = ssz_at(proof, ssz_len(proof) - 1).bytes;
  if (!last_value.data) return NULL_BYTES;
  if (rlp_decode(&last_value, 0, &last_value) != RLP_LIST) return NULL_BYTES;
  switch ((int) rlp_decode(&last_value, -1, &last_value)) {
    case 2: // must be a leaf (otherwise the verification would have failed)
      if (rlp_decode(&last_value, 1, &last_value) != RLP_ITEM) return NULL_BYTES;
      break;
    case 17: // branch noch with the value
      if (rlp_decode(&last_value, 16, &last_value) != RLP_ITEM) return NULL_BYTES;
      break;
    default:
      return NULL_BYTES;
  }
  return last_value;
}

bool eth_get_storage_value(ssz_ob_t storage, bytes32_t value) {
  bytes_t last_value = get_last_value(ssz_get(&storage, "proof"));
  if (!last_value.data) return false;
  if (rlp_decode(&last_value, 0, &last_value) != RLP_ITEM) return false;
  if (last_value.len > 32) return false;
  memcpy(value + 32 - last_value.len, last_value.data, last_value.len);
  return true;
}

void eth_get_account_value(ssz_ob_t account, eth_account_field_t field, bytes32_t value) {
  bytes_t last_value = get_last_value(ssz_get(&account, "accountProof"));
  if (!last_value.data) return;
  if (rlp_decode(&last_value, 0, &last_value) != RLP_LIST) return;
  if (rlp_decode(&last_value, field - 1, &last_value) != RLP_ITEM) return;
  if (last_value.len > 32) return;
  memcpy(value + 32 - last_value.len, last_value.data, last_value.len);
}