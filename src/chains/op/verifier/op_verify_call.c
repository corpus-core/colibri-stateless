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
#include "op_types.h"
#include "op_verify.h"
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

// Function to verify call proof
bool op_verify_call_proof(verify_ctx_t* ctx) {
  bytes32_t    state_root  = {0};
  ssz_ob_t     accounts    = ssz_get(&ctx->proof, "accounts");
  ssz_ob_t     block_proof = ssz_get(&ctx->proof, "block_proof");
  bytes_t      call_result = NULL_BYTES;
  call_code_t* call_codes  = NULL;
  bool         match       = false;
  CHECK_JSON_VERIFY(ctx->args, "[{to:address,data:bytes,gas?:hexuint,value?:hexuint,gasPrice?:hexuint,from?:address},block]", "Invalid transaction");

  // make sure we have all the code data we need
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
  if (!c4_eth_verify_accounts(ctx, accounts, state_root)) RETURN_VERIFY_ERROR(ctx, "Failed to verify accounts");
  ssz_ob_t* execution_payload = op_extract_verified_execution_payload(ctx, block_proof, NULL, NULL);
  if (!execution_payload) return false;
  match = memcmp(state_root, ssz_get(execution_payload, "stateRoot").bytes.data, 32) == 0;
  safe_free(execution_payload);
  if (!match) RETURN_VERIFY_ERROR(ctx, "State root mismatch");

  ctx->success = true;
  return ctx->success;
}