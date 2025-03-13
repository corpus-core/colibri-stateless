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

bool eth_run_call_evmone(verify_ctx_t* ctx, ssz_ob_t accounts, json_t tx, bytes_t* call_result);

static bool verify_accounts(verify_ctx_t* ctx, ssz_ob_t accounts, bytes32_t state_root) {
  uint32_t  len  = ssz_len(accounts);
  bytes32_t root = {0};
  for (uint32_t i = 0; i < len; i++) {
    ssz_ob_t acc = ssz_at(accounts, i);
    if (!eth_verify_account_proof_exec(ctx, &acc, root)) RETURN_VERIFY_ERROR(ctx, "Failed to verify account proof");
    if (bytes_all_zero(bytes(state_root, 32)))
      memcpy(state_root, root, 32);
    else if (memcmp(state_root, root, 32) != 0)
      RETURN_VERIFY_ERROR(ctx, "State root mismatch");
  }
  return true;
}

// Function to verify call proof
bool verify_call_proof(verify_ctx_t* ctx) {
  bytes32_t body_root                = {0};
  bytes32_t state_root               = {0};
  ssz_ob_t  state_proof              = ssz_get(&ctx->proof, "state_proof");
  ssz_ob_t  accounts                 = ssz_get(&ctx->proof, "accounts");
  ssz_ob_t  state_merkle_proof       = ssz_get(&state_proof, "state_proof");
  ssz_ob_t  header                   = ssz_get(&state_proof, "header");
  ssz_ob_t  sync_committee_bits      = ssz_get(&state_proof, "sync_committee_bits");
  ssz_ob_t  sync_committee_signature = ssz_get(&state_proof, "sync_committee_signature");
  bytes_t   call_result              = NULL_BYTES;

  CHECK_JSON_VERIFY(ctx->args, "[{to:address,data:bytes,gas?:hexuint,value?:hexuint,gasPrice?:hexuint,from?:address},block]", "Invalid transaction");

#ifdef EVMONE
  if (!eth_run_call_evmone(ctx, accounts, json_at(ctx->args, 0), &call_result)) RETURN_VERIFY_ERROR(ctx, "Failed to run call");
  if (!bytes_eq(call_result, ctx->data.bytes)) RETURN_VERIFY_ERROR(ctx, "Call result mismatch");
  free(call_result.data);
#endif

  if (!verify_accounts(ctx, accounts, state_root)) RETURN_VERIFY_ERROR(ctx, "Failed to verify accounts");
  ssz_verify_single_merkle_proof(state_merkle_proof.bytes, state_root, STATE_ROOT_GINDEX, body_root);
  if (memcmp(body_root, ssz_get(&header, "bodyRoot").bytes.data, 32) != 0) RETURN_VERIFY_ERROR(ctx, "invalid body root!");
  if (!c4_verify_blockroot_signature(ctx, &header, &sync_committee_bits, &sync_committee_signature, 0)) RETURN_VERIFY_ERROR(ctx, "invalid blockhash signature!");

  ctx->success = true;
  return ctx->success;
}