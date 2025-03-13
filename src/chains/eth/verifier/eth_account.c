
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
    ssz_ob_t  value   = ssz_get(&storage, "value");
    bytes_t   leaf    = {0};
    keccak(key.bytes, path);
    if (!patricia_verify(root, bytes(path, 32), proof, &leaf) || !leaf.len) RETURN_VERIFY_ERROR(ctx, "invalid storage proof!");
    if (!is_equal(value, &leaf, 0)) RETURN_VERIFY_ERROR(ctx, "invalid storage proof!");
    if (memcmp(root, storage_hash, 32) != 0) RETURN_VERIFY_ERROR(ctx, "invalid storage root!");
  }

  return true;
}

bool eth_verify_account_proof_exec(verify_ctx_t* ctx, ssz_ob_t* proof, bytes32_t state_root) {
  ssz_ob_t  account_proof = ssz_get(proof, "accountProof");
  ssz_ob_t  address       = ssz_get(proof, "address");
  ssz_ob_t  balance       = ssz_get(proof, "balance");
  ssz_ob_t  code_hash     = ssz_get(proof, "codeHash");
  ssz_ob_t  nonce         = ssz_get(proof, "nonce");
  ssz_ob_t  storage_hash  = ssz_get(proof, "storageHash");
  bytes32_t address_hash  = {0};
  bytes_t   rlp_account   = {0};
  keccak(address.bytes, address_hash);

  bool existing_account = !bytes_all_zero(balance.bytes) || memcmp(code_hash.bytes.data, EMPTY_HASH, 32) != 0 || memcmp(storage_hash.bytes.data, EMPTY_ROOT_HASH, 32) != 0 || !bytes_all_zero(nonce.bytes);

  if (!patricia_verify(state_root, bytes(address_hash, 32), account_proof, existing_account ? &rlp_account : NULL))
    RETURN_VERIFY_ERROR(ctx, "invalid account proof on execution layer!");

  if (existing_account) {
    if (!rlp_account.data) RETURN_VERIFY_ERROR(ctx, "invalid account proof on execution layer!");

    if (rlp_decode(&rlp_account, 0, &rlp_account) != RLP_LIST) RETURN_VERIFY_ERROR(ctx, "invalid account proof on execution layer!");
    if (!is_equal(nonce, &rlp_account, 0)) RETURN_VERIFY_ERROR(ctx, "invalid nonce");
    if (!is_equal(balance, &rlp_account, 1)) RETURN_VERIFY_ERROR(ctx, "invalid balance");
    if (!is_equal(storage_hash, &rlp_account, 2)) RETURN_VERIFY_ERROR(ctx, "invalid storage hash");
    if (!is_equal(code_hash, &rlp_account, 3)) RETURN_VERIFY_ERROR(ctx, "invalid code hash");
  }

  if (!verify_storage(ctx, ssz_get(proof, "storageProof"), storage_hash.bytes.data)) RETURN_VERIFY_ERROR(ctx, "invalid storage proof!");

  return true;
}
