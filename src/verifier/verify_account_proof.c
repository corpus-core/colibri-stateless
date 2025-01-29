
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

#define STATE_ROOT_GINDEX 802

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
static bool verify_account_proof_exec(verify_ctx_t* ctx, ssz_ob_t* proof, bytes32_t state_root) {
  ssz_ob_t account_proof = ssz_get(proof, "accountProof");
  ssz_ob_t address       = ssz_get(proof, "address");
  ssz_ob_t balance       = ssz_get(proof, "balance");
  ssz_ob_t code_hash     = ssz_get(proof, "codeHash");
  ssz_ob_t nonce         = ssz_get(proof, "nonce");
  ssz_ob_t storage_hash  = ssz_get(proof, "storageHash");

  if (ssz_is_error(balance) || ssz_is_error(code_hash) || ssz_is_error(nonce) || ssz_is_error(storage_hash)) RETURN_VERIFY_ERROR(ctx, "invalid account proof data!");
  if (ssz_is_error(account_proof) || ssz_is_error(address)) RETURN_VERIFY_ERROR(ctx, "invalid account proof data!");

  bytes32_t address_hash = {0};
  bytes_t   rlp_account  = {0};
  bytes_t   path         = bytes(address_hash, 32);
  keccak(address.bytes, address_hash);

  bool existing_account = !bytes_all_zero(balance.bytes) || memcmp(code_hash.bytes.data, EMPTY_HASH, 32) != 0 || memcmp(storage_hash.bytes.data, EMPTY_ROOT_HASH, 32) != 0 || !bytes_all_zero(nonce.bytes);

  if (!patricia_verify(state_root, &path, account_proof, existing_account ? &rlp_account : NULL))
    RETURN_VERIFY_ERROR(ctx, "invalid account proof on execution layer!");

  if (existing_account) {
    if (!rlp_account.data) RETURN_VERIFY_ERROR(ctx, "invalid account proof on execution layer!");

    if (rlp_decode(&rlp_account, 0, &rlp_account) != RLP_LIST) RETURN_VERIFY_ERROR(ctx, "invalid account proof on execution layer!");
    if (!is_equal(nonce, &rlp_account, 0)) RETURN_VERIFY_ERROR(ctx, "invalid nonce");
    if (!is_equal(balance, &rlp_account, 1)) RETURN_VERIFY_ERROR(ctx, "invalid balance");
    if (!is_equal(storage_hash, &rlp_account, 2)) RETURN_VERIFY_ERROR(ctx, "invalid storage hash");
    if (!is_equal(code_hash, &rlp_account, 3)) RETURN_VERIFY_ERROR(ctx, "invalid code hash");
  }
  return true;
}

bool verify_account_proof(verify_ctx_t* ctx) {
  ctx->type = PROOF_TYPE_ACCOUNT;

  bytes32_t body_root                = {0};
  bytes32_t state_root               = {0};
  ssz_ob_t  state_proof              = ssz_get(&ctx->proof, "state_proof");
  ssz_ob_t  state_merkle_proof       = ssz_get(&state_proof, "state_proof");
  ssz_ob_t  header                   = ssz_get(&state_proof, "header");
  ssz_ob_t  sync_committee_bits      = ssz_get(&state_proof, "sync_committee_bits");
  ssz_ob_t  sync_committee_signature = ssz_get(&state_proof, "sync_committee_signature");
  bytes_t   verified_address         = ssz_get(&ctx->proof, "address").bytes;
  buffer_t  address_buf              = stack_buffer(body_root);

  if (ssz_is_error(header) || ssz_is_error(state_proof) || ssz_is_error(state_merkle_proof)) RETURN_VERIFY_ERROR(ctx, "invalid proof, missing header or blockhash_proof!");
  if (ssz_is_error(sync_committee_bits) || sync_committee_bits.bytes.len != 64 || ssz_is_error(sync_committee_signature) || sync_committee_signature.bytes.len != 96) RETURN_VERIFY_ERROR(ctx, "invalid proof, missing sync committee bits or signature!");
  if (!verified_address.data || verified_address.len != 20 || !ctx->data.def || !ssz_is_type(&ctx->data, &ssz_bytes32) || ctx->data.bytes.data == NULL || ctx->data.bytes.len != 32) RETURN_VERIFY_ERROR(ctx, "invalid data, data is not a bytes32!");

  if (!verify_account_proof_exec(ctx, &ctx->proof, state_root)) RETURN_VERIFY_ERROR(ctx, "invalid account proof!");
  ssz_verify_merkle_proof(state_merkle_proof.bytes, state_root, STATE_ROOT_GINDEX, body_root);
  if (memcmp(body_root, ssz_get(&header, "bodyRoot").bytes.data, 32) != 0) RETURN_VERIFY_ERROR(ctx, "invalid body root!");
  if (!c4_verify_blockroot_signature(ctx, &header, &sync_committee_bits, &sync_committee_signature, 0)) RETURN_VERIFY_ERROR(ctx, "invalid blockhash signature!");

  bytes_t req_address = {0};
  if (ctx->method && strcmp(ctx->method, "eth_getBalance") == 0) req_address = json_as_bytes(json_at(ctx->args, 0), &address_buf);
  if (req_address.data && (req_address.len != 20 || memcmp(req_address.data, verified_address.data, 20) != 0)) RETURN_VERIFY_ERROR(ctx, "proof does not match the address in request");
  ctx->success = true;
  return true;
}