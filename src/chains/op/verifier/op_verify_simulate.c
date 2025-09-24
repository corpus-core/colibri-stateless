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
#include "call_ctx.h"
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

// Forward declaration
c4_status_t eth_run_call_evmone_with_events(verify_ctx_t* ctx, call_code_t* call_codes, ssz_ob_t accounts, json_t tx, bytes_t* call_result, emitted_log_t** logs, bool capture_events);

// Function to verify simulate transaction proof for OP Stack
bool op_verify_simulate_proof(verify_ctx_t* ctx) {
  bytes32_t      state_root  = {0};
  ssz_ob_t       accounts    = ssz_get(&ctx->proof, "accounts");
  ssz_ob_t       block_proof = ssz_get(&ctx->proof, "block_proof");
  bytes_t        call_result = NULL_BYTES;
  emitted_log_t* logs        = NULL;
  call_code_t*   call_codes  = NULL;
  bool           match       = false;

  CHECK_JSON_VERIFY(ctx->args, "[{to:address,data:bytes,gas?:hexuint,value?:hexuint,gasPrice?:hexuint,from?:address},block]", "Invalid transaction");

  if (eth_get_call_codes(ctx, &call_codes, accounts) != C4_SUCCESS) return false;

#ifdef EVMONE
  c4_status_t call_status = eth_run_call_evmone_with_events(ctx, call_codes, accounts, json_at(ctx->args, 0), &call_result, &logs, true);
#else
  c4_status_t call_status = c4_state_add_error(&ctx->state, "no EVM is enabled, build with -DEVMONE=1");
#endif

  if (call_status != C4_SUCCESS) {
    free_emitted_logs(logs);
    eth_free_codes(call_codes);
    return false;
  }

  // Extract and verify execution payload (OP Stack specific)
  ssz_ob_t* execution_payload = op_extract_verified_execution_payload(ctx, block_proof, NULL, NULL);
  if (!execution_payload) {
    safe_free(call_result.data);
    free_emitted_logs(logs);
    eth_free_codes(call_codes);
    return false;
  }

  // Build simulation result using shared function (Tenderly-compatible format)
  bool     success  = (call_status == C4_SUCCESS && ctx->state.error == NULL);
  uint64_t gas_used = 21000; // TODO: Get actual gas usage from EVM execution

  ssz_ob_t simulation_result = eth_build_simulation_result_ssz(call_result, logs, success, gas_used, execution_payload);

  // Set the result
  if (ctx->data.def == NULL || ctx->data.def->type == SSZ_TYPE_NONE) {
    ctx->data = simulation_result;
    ctx->flags |= VERIFY_FLAG_FREE_DATA;
    match = true;
  }
  else {
    match = simulation_result.bytes.data && bytes_eq(simulation_result.bytes, ctx->data.bytes);
    if (simulation_result.bytes.data) safe_free(simulation_result.bytes.data);
  }

  // Verify accounts against execution payload state root (OP Stack specific)
  if (!c4_eth_verify_accounts(ctx, accounts, state_root)) {
    safe_free(call_result.data);
    free_emitted_logs(logs);
    eth_free_codes(call_codes);
    safe_free(execution_payload);
    RETURN_VERIFY_ERROR(ctx, "Failed to verify accounts");
  }

  // Verify state root matches execution payload
  bool state_root_match = memcmp(state_root, ssz_get(execution_payload, "stateRoot").bytes.data, 32) == 0;
  safe_free(execution_payload);

  if (!state_root_match) {
    safe_free(call_result.data);
    free_emitted_logs(logs);
    eth_free_codes(call_codes);
    RETURN_VERIFY_ERROR(ctx, "State root mismatch");
  }

  // Cleanup
  safe_free(call_result.data);
  free_emitted_logs(logs);
  eth_free_codes(call_codes);

  if (!match) RETURN_VERIFY_ERROR(ctx, "Simulation result mismatch");

  ctx->success = true;
  return ctx->success;
}
