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
#include "patricia.h"
#include "rlp.h"
#include "ssz.h"
#include "sync_committee.h"
#include "verify_data_types.h" // For ETH_SIMULATION_* definitions
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declaration
c4_status_t eth_run_call_evmone_with_events(verify_ctx_t* ctx, call_code_t* call_codes, ssz_ob_t accounts, json_t tx, bytes_t* call_result, emitted_log_t** logs, bool capture_events);

// Function to build simulation result in SSZ format using ssz_builder_t (Tenderly-compatible)
static ssz_ob_t build_simulation_result_ssz(bytes_t call_result, emitted_log_t* logs, bool success, uint64_t gas_used, ssz_ob_t* execution_payload) {
  ssz_builder_t builder = ssz_builder_for_def(eth_ssz_verification_type(ETH_SSZ_DATA_SIMULATION));

  // Build with minimal mask - only essential fields will be shown in JSON
  ssz_add_uint32(&builder, ETH_SIMULATION_RESULT_MASK_GAS_USED | ETH_SIMULATION_RESULT_MASK_LOGS | ETH_SIMULATION_RESULT_MASK_STATUS | ETH_SIMULATION_RESULT_MASK_RETURN_VALUE); // _optmask - corrected bits: 3,4,6,9
  ssz_add_uint64(&builder, execution_payload ? ssz_get_uint64(execution_payload, "blockNumber") : 0);                                                                            // blockNumber (hidden by mask)
  ssz_add_uint64(&builder, gas_used);                                                                                                                                            // cumulativeGasUsed (hidden by mask)
  ssz_add_uint64(&builder, gas_used);                                                                                                                                            // gasUsed (visible)

  // 5. logs (Index 4) - List
  ssz_builder_t logs_builder = ssz_builder_for_def(ssz_get_def(builder.def, "logs"));

  // determine the size beforehand
  size_t log_count = 0;
  for (emitted_log_t* log = logs; log; log = log->next) log_count++;

  for (emitted_log_t* log = logs; log; log = log->next) {
    ssz_builder_t log_builder = ssz_builder_for_def(logs_builder.def->def.vector.type);

    // Build log with minimal mask - only 'raw' field will be shown in JSON
    ssz_add_uint16(&log_builder, ETH_SIMULATION_LOG_MASK_RAW); // _optmask - only raw field (bit 4)
    ssz_add_uint8(&log_builder, 0);                            // anonymous (hidden by mask)
    ssz_add_bytes(&log_builder, "inputs", NULL_BYTES);         // no inputs yet (hidden by mask)
    ssz_add_bytes(&log_builder, "name", NULL_BYTES);           // name (hidden by mask)

    // raw (visible) - the only field shown
    ssz_builder_t raw_builder = ssz_builder_for_def(ssz_get_def(log_builder.def, "raw"));
    ssz_add_bytes(&raw_builder, "address", bytes(log->address, 20));
    ssz_add_bytes(&raw_builder, "data", log->data);

    ssz_builder_t topics_builder = {0};
    topics_builder.def           = (ssz_def_t*) ssz_get_def(raw_builder.def, "topics");

    for (size_t i = 0; i < log->topics_count; i++)
      ssz_add_dynamic_list_bytes(&topics_builder, log->topics_count, bytes(log->topics[i], 32));
    ssz_add_builders(&raw_builder, "topics", topics_builder);
    ssz_add_builders(&log_builder, "raw", raw_builder);
    ssz_add_dynamic_list_builders(&logs_builder, log_count, log_builder);
  }

  // Add logs list to main builder
  ssz_add_builders(&builder, "logs", logs_builder);    // logs (visible)
  ssz_add_bytes(&builder, "logsBloom", NULL_BYTES);    // logsBloom (hidden by mask)
  ssz_add_uint8(&builder, success ? 1 : 0);            // status (visible)
  ssz_add_bytes(&builder, "trace", NULL_BYTES);        // trace (hidden by mask)
  ssz_add_uint8(&builder, 0);                          // type (hidden by mask)
  ssz_add_bytes(&builder, "returnValue", call_result); // returnValue (visible)

  // Build and return the SSZ object
  return ssz_builder_to_bytes(&builder);
}

// Function to verify simulate transaction proof
bool verify_simulate_proof(verify_ctx_t* ctx) {
  bytes32_t      body_root   = {0};
  bytes32_t      state_root  = {0};
  ssz_ob_t       state_proof = ssz_get(&ctx->proof, "state_proof");
  ssz_ob_t       accounts    = ssz_get(&ctx->proof, "accounts");
  ssz_ob_t       header      = ssz_get(&state_proof, "header");
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

  // Build simulation result using SSZ (Tenderly-compatible format)
  bool     success  = (call_status == C4_SUCCESS && ctx->state.error == NULL);
  uint64_t gas_used = 21000; // TODO: Get actual gas usage from EVM execution

  ssz_ob_t simulation_result = build_simulation_result_ssz(call_result, logs, success, gas_used, NULL);

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

  // Cleanup
  safe_free(call_result.data);
  free_emitted_logs(logs);
  eth_free_codes(call_codes);

  if (!match) RETURN_VERIFY_ERROR(ctx, "Simulation result mismatch");
  if (!c4_eth_verify_accounts(ctx, accounts, state_root)) RETURN_VERIFY_ERROR(ctx, "Failed to verify accounts");
  if (!eth_verify_state_proof(ctx, state_proof, state_root)) return false;
  if (c4_verify_header(ctx, header, state_proof) != C4_SUCCESS) return false;

  ctx->success = true;
  return ctx->success;
}