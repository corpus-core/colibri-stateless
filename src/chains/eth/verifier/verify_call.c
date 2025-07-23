/*
 * Copyright (c) 2025 corpus.core
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

c4_status_t eth_run_call_evmone(verify_ctx_t* ctx, call_code_t* call_codes, ssz_ob_t accounts, json_t tx, bytes_t* call_result);

static bool verify_accounts(verify_ctx_t* ctx, ssz_ob_t accounts, bytes32_t state_root) {
  uint32_t  len                 = ssz_len(accounts);
  bytes32_t root                = {0};
  bytes32_t code_hash_exepected = {0};
  for (uint32_t i = 0; i < len; i++) {
    ssz_ob_t acc = ssz_at(accounts, i);
    if (!eth_verify_account_proof_exec(ctx, &acc, root, ETH_ACCOUNT_CODE_HASH, bytes(code_hash_exepected, 32))) RETURN_VERIFY_ERROR(ctx, "Failed to verify account proof");
    ssz_ob_t code = ssz_get(&acc, "code");
    if (code.def->type == SSZ_TYPE_LIST) {
      bytes32_t code_hash_passed = {0};
      keccak(code.bytes, code_hash_passed);
      if (memcmp(code_hash_exepected, code_hash_passed, 32) != 0) RETURN_VERIFY_ERROR(ctx, "Code hash mismatch");
    }

    if (bytes_all_zero(bytes(state_root, 32)))
      memcpy(state_root, root, 32);
    else if (memcmp(state_root, root, 32) != 0)
      RETURN_VERIFY_ERROR(ctx, "State root mismatch");
  }
  return true;
}
// Function to verify call proof
bool verify_call_proof(verify_ctx_t* ctx) {
  bytes32_t    body_root   = {0};
  bytes32_t    state_root  = {0};
  ssz_ob_t     state_proof = ssz_get(&ctx->proof, "state_proof");
  ssz_ob_t     accounts    = ssz_get(&ctx->proof, "accounts");
  ssz_ob_t     header      = ssz_get(&state_proof, "header");
  bytes_t      call_result = NULL_BYTES;
  call_code_t* call_codes  = NULL;
  bool         match       = false;
  CHECK_JSON_VERIFY(ctx->args, "[{to:address,data:bytes,gas?:hexuint,value?:hexuint,gasPrice?:hexuint,from?:address},block]", "Invalid transaction");

  if (eth_get_call_codes(ctx, &call_codes, accounts) != C4_SUCCESS) return false;
#ifdef EVMONE
  c4_status_t call_status = eth_run_call_evmone(ctx, call_codes, accounts, json_at(ctx->args, 0), &call_result);
#else
  c4_status_t call_status = c4_state_add_error(&ctx->state, "no EVM is enabled, build with -DEVMONE=1");
#endif
  if (call_result.data && (ctx->data.def == NULL || ctx->data.def->type == SSZ_TYPE_NONE)) {
    ctx->data = (ssz_ob_t) {.bytes = call_result, .def = eth_ssz_verification_type(ETH_SSZ_DATA_BYTES)};
    ctx->flags |= VERIFY_FLAG_FREE_DATA;
    match = true;
  }
  else {
    match = call_result.data && bytes_eq(call_result, ctx->data.bytes);
    safe_free(call_result.data);
  }
  eth_free_codes(call_codes);
  if (call_status != C4_SUCCESS) return false;
  if (!match) RETURN_VERIFY_ERROR(ctx, "Call result mismatch");
  if (!verify_accounts(ctx, accounts, state_root)) RETURN_VERIFY_ERROR(ctx, "Failed to verify accounts");
  if (!eth_verify_state_proof(ctx, state_proof, state_root)) false;
  if (c4_verify_header(ctx, header, state_proof) != C4_SUCCESS) return false;

  ctx->success = true;
  return ctx->success;
}