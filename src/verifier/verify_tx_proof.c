
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

bool verify_tx_proof(verify_ctx_t* ctx) {
  ctx->type = PROOF_TYPE_TRANSACTION;

  bytes32_t body_root                = {0};
  ssz_ob_t  tx_proof                 = ssz_get(&ctx->proof, "proof");
  ssz_ob_t  header                   = ssz_get(&ctx->proof, "header");
  ssz_ob_t  sync_committee_bits      = ssz_get(&ctx->proof, "sync_committee_bits");
  ssz_ob_t  sync_committee_signature = ssz_get(&ctx->proof, "sync_committee_signature");

  if (ssz_is_error(header) || ssz_is_error(tx_proof)) RETURN_VERIFY_ERROR(ctx, "invalid proof, missing header or blockhash_proof!");
  if (ssz_is_error(sync_committee_bits) || sync_committee_bits.bytes.len != 64 || ssz_is_error(sync_committee_signature) || sync_committee_signature.bytes.len != 96) RETURN_VERIFY_ERROR(ctx, "invalid proof, missing sync committee bits or signature!");
  // if (!verified_address.data || verified_address.len != 20 || !ctx->data.def || !ssz_is_type(&ctx->data, &ssz_bytes32) || ctx->data.bytes.data == NULL || ctx->data.bytes.len != 32) RETURN_VERIFY_ERROR(ctx, "invalid data, data is not a bytes32!");

  //  if (!verify_account_proof_exec(ctx, &ctx->proof, state_root)) RETURN_VERIFY_ERROR(ctx, "invalid account proof!");
  //   ssz_verify_single_merkle_proof(state_merkle_proof.bytes, state_root, STATE_ROOT_GINDEX, body_root);
  //  if (memcmp(body_root, ssz_get(&header, "bodyRoot").bytes.data, 32) != 0) RETURN_VERIFY_ERROR(ctx, "invalid body root!");
  if (!c4_verify_blockroot_signature(ctx, &header, &sync_committee_bits, &sync_committee_signature, 0)) RETURN_VERIFY_ERROR(ctx, "invalid blockhash signature!");

  ctx->success = true;
  return true;
}