
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

bool verify_account_proof(verify_ctx_t* ctx) {
  bytes32_t body_root                = {0};
  bytes32_t state_root               = {0};
  ssz_ob_t  state_proof              = ssz_get(&ctx->proof, "state_proof");
  ssz_ob_t  state_merkle_proof       = ssz_get(&state_proof, "state_proof");
  ssz_ob_t  header                   = ssz_get(&state_proof, "header");
  ssz_ob_t  sync_committee_bits      = ssz_get(&state_proof, "sync_committee_bits");
  ssz_ob_t  sync_committee_signature = ssz_get(&state_proof, "sync_committee_signature");
  bytes_t   verified_address         = ssz_get(&ctx->proof, "address").bytes;
  buffer_t  address_buf              = stack_buffer(body_root);

  if (!eth_verify_account_proof_exec(ctx, &ctx->proof, state_root)) RETURN_VERIFY_ERROR(ctx, "invalid account proof!");
  ssz_verify_single_merkle_proof(state_merkle_proof.bytes, state_root, STATE_ROOT_GINDEX, body_root);
  if (memcmp(body_root, ssz_get(&header, "bodyRoot").bytes.data, 32) != 0) RETURN_VERIFY_ERROR(ctx, "invalid body root!");
  if (!c4_verify_blockroot_signature(ctx, &header, &sync_committee_bits, &sync_committee_signature, 0)) RETURN_VERIFY_ERROR(ctx, "invalid blockhash signature!");

  bytes_t req_address = {0};
  if (ctx->method && strcmp(ctx->method, "eth_getBalance") == 0) req_address = json_as_bytes(json_at(ctx->args, 0), &address_buf);
  if (req_address.data && (req_address.len != 20 || memcmp(req_address.data, verified_address.data, 20) != 0)) RETURN_VERIFY_ERROR(ctx, "proof does not match the address in request");
  ctx->success = true;
  return true;
}