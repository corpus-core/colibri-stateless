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
#include "verify_data_types.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool c4_eth_verify_accounts(verify_ctx_t* ctx, ssz_ob_t accounts, bytes32_t state_root, const eth_state_overrides_t* overrides) {
  uint32_t  len                 = ssz_len(accounts);
  bytes32_t root                = {0};
  bytes32_t code_hash_exepected = {0};
  for (uint32_t i = 0; i < len; i++) {
    ssz_ob_t acc = ssz_at(accounts, i);
    if (!eth_verify_account_proof_exec(ctx, &acc, root, ETH_ACCOUNT_CODE_HASH, bytes(code_hash_exepected, 32))) RETURN_VERIFY_ERROR(ctx, "Failed to verify account proof");
    ssz_ob_t                      code = ssz_get(&acc, "code");
    uint8_t*                      addr = ssz_get(&acc, "address").bytes.data;
    const eth_account_override_t* ov   = overrides ? eth_state_overrides_find(overrides, addr) : NULL;
    if (ov && ov->has_code) {
      // The request contains an explicit code override, so the canonical code hash check is not applicable.
    }
    else if (code.def->type == SSZ_TYPE_LIST) {
      bytes32_t code_hash_passed = {0};
      keccak(code.bytes, code_hash_passed);
      if (memcmp(code_hash_exepected, code_hash_passed, 32) != 0) RETURN_VERIFY_ERROR(ctx, "Code hash mismatch");
    }

    // The first account proof establishes the state root; all subsequent proofs must agree.
    if (bytes_all_zero(bytes(state_root, 32)))
      memcpy(state_root, root, 32);
    else if (memcmp(state_root, root, 32) != 0)
      RETURN_VERIFY_ERROR(ctx, "State root mismatch");
  }
  return true;
}

// Synthesizes an ERC-20 style Transfer(address,address,uint256) event for the native
// value transfer of the transaction. This is prepended to the EVM-emitted logs so the
// simulation result reflects the full value flow visible to callers.
// Topic layout (ABI-encoded): [keccak(signature), from (padded), to (padded)]
static void add_simulate_value_transfer_event(verify_ctx_t* ctx, emitted_log_t** logs) {
  json_t tx       = json_at(ctx->args, 0);
  json_t tx_value = json_get(tx, "value");
  if (tx_value.type != JSON_TYPE_STRING || tx_value.len < 5 || strncmp(tx_value.start, "\"0x0\"", 5) == 0) return;

  bytes32_t value     = {0};
  buffer_t  value_buf = stack_buffer(value);

  emitted_log_t* log = safe_calloc(sizeof(emitted_log_t), 1);
  log->data          = bytes(safe_calloc(32, 1), 32);
  log->topics        = safe_calloc(3, sizeof(bytes32_t));
  log->topics_count  = 3;
  log->next          = *logs;
  *logs              = log;
  bytes_t  b         = json_as_bytes(tx_value, &value_buf);
  uint8_t* ptr       = (uint8_t*) log->topics;        // contiguous memory: [topic0][topic1][topic2], 32 bytes each
  memcpy(log->data.data + 32 - b.len, b.data, b.len); // right-align value into 32-byte data field
  json_t from = json_get(tx, "from");
  if (from.type == JSON_TYPE_STRING && from.len >= 5 && strncmp(from.start, "\"0x0\"", 5) != 0) {
    b = json_as_bytes(from, &value_buf);
    memcpy(ptr + 64 - b.len, b.data, b.len); // right-align 20-byte address into topic1 (offset 32..63)
  }
  json_t to = json_get(tx, "to");
  if (to.type == JSON_TYPE_STRING && to.len >= 5 && strncmp(to.start, "\"0x0\"", 5) != 0) {
    b = json_as_bytes(to, &value_buf);
    memcpy(ptr + 96 - b.len, b.data, b.len); // right-align 20-byte address into topic2 (offset 64..95)
  }
  const char* signature = "Transfer(address,address,uint256)";
  keccak(bytes((uint8_t*) signature, strlen(signature)), ptr); // topic0 = event signature hash
}

// Yellow Paper Appendix G – Fee Schedule
#define G_TRANSACTION     21000
#define G_TXDATA_ZERO     4
#define G_TXDATA_NON_ZERO 16

// Yellow Paper Section 6.2 (Eq. 64): G_transaction + per-byte calldata cost
static uint64_t eth_intrinsic_gas(json_t tx) {
  uint64_t gas  = G_TRANSACTION;
  json_t   data = json_get(tx, "data");
  if (data.type == JSON_TYPE_NOT_FOUND) data = json_get(tx, "input");
  if (data.type != JSON_TYPE_STRING || data.len < 5) return gas;

  buffer_t buf      = {0};
  bytes_t  calldata = json_as_bytes(data, &buf);
  for (uint32_t i = 0; i < calldata.len; i++)
    gas += calldata.data[i] == 0 ? G_TXDATA_ZERO : G_TXDATA_NON_ZERO;
  buffer_free(&buf);
  return gas;
}

void evm_call_ctx_free(evm_call_ctx_t* evm) {
  eth_state_overrides_free(&evm->overrides);
  eth_free_codes(evm->call_codes);
  safe_free(evm->call_result.data);
  free_emitted_logs(evm->logs);
}

// Each match_* function serves dual purpose: in prover mode (ctx->data not set) it
// populates ctx->data with the result; in verifier mode it compares against the proof.

static bool match_simulate_result(verify_ctx_t* ctx, evm_call_ctx_t* evm) {
  add_simulate_value_transfer_event(ctx, &evm->logs);

  ssz_ob_t simulation_result = eth_build_simulation_result_ssz(evm->call_result, evm->logs, ctx->state.error == NULL, evm->gas_used, NULL);

  if (ctx->data.def == NULL || ctx->data.def->type == SSZ_TYPE_NONE) {
    ctx->data = simulation_result;
    ctx->flags |= VERIFY_FLAG_FREE_DATA;
    return true;
  }
  bool match = bytes_eq(simulation_result.bytes, ctx->data.bytes);
  if (simulation_result.bytes.data) safe_free(simulation_result.bytes.data);
  return match;
}

static bool match_estimate_result(verify_ctx_t* ctx, evm_call_ctx_t* evm) {
  if (ctx->data.def == NULL || ctx->data.def->type == SSZ_TYPE_NONE) {
    ssz_builder_t builder = ssz_builder_for_type(ETH_SSZ_DATA_UINT256);
    ssz_add_uint64(&builder, evm->gas_used);
    buffer_append(&builder.fixed, bytes(NULL, 32 - 8));
    ctx->data = ssz_builder_to_bytes(&builder);
    ctx->flags |= VERIFY_FLAG_FREE_DATA;
    return true;
  }
  uint8_t gas_le[32] = {0};
  uint64_to_le(gas_le, evm->gas_used);
  return bytes_eq(bytes(gas_le, 32), ctx->data.bytes);
}

static bool match_call_result(verify_ctx_t* ctx, evm_call_ctx_t* evm) {
  if (evm->call_result.data && (ctx->data.def == NULL || ctx->data.def->type == SSZ_TYPE_NONE)) {
    ctx->data = (ssz_ob_t) {.bytes = evm->call_result, .def = eth_ssz_verification_type(ETH_SSZ_DATA_BYTES)};
    ctx->flags |= VERIFY_FLAG_FREE_DATA;
    evm->call_result = NULL_BYTES; // ownership transferred to ctx->data
    return true;
  }
  return evm->call_result.data && bytes_eq(evm->call_result, ctx->data.bytes);
}

bool verify_evm_call(verify_ctx_t* ctx, evm_call_ctx_t* evm) {
  bool is_simulate = ctx->method && strcmp(ctx->method, "colibri_simulateTransaction") == 0;
  bool is_estimate = ctx->method && strcmp(ctx->method, "eth_estimateGas") == 0;

  CHECK_JSON_VERIFY(ctx->args, "[{to:address,data:bytes,gas?:hexuint,value?:hexuint,gasPrice?:hexuint,from?:address},block?,{*:{balance?:hexuint,code?:bytes,state?:{*:bytes32},stateDiff?:{*:bytes32}}}?]", "Invalid transaction");

  json_t overrides_json = json_at(ctx->args, 2);
  if (overrides_json.type == JSON_TYPE_OBJECT && eth_parse_state_overrides(ctx, overrides_json, &evm->overrides) != C4_SUCCESS) return false;
  if (eth_get_call_codes(ctx, &evm->call_codes, evm->accounts) != C4_SUCCESS) return false;

#ifdef EVMONE
  c4_status_t call_status = eth_run_call_evmone_with_events(ctx, evm, is_simulate);
#else
  c4_status_t call_status = c4_state_add_error(&ctx->state, "no EVM is enabled, build with -DEVMONE=1");
#endif

  evm->gas_used += eth_intrinsic_gas(json_at(ctx->args, 0));
  if (call_status != C4_SUCCESS) return false;

  bool match = is_simulate   ? match_simulate_result(ctx, evm)
               : is_estimate ? match_estimate_result(ctx, evm)
                             : match_call_result(ctx, evm);

  if (!match) RETURN_VERIFY_ERROR(ctx, is_simulate ? "Simulation result mismatch" : "Call result mismatch");
  if (!c4_eth_verify_accounts(ctx, evm->accounts, evm->state_root, &evm->overrides)) RETURN_VERIFY_ERROR(ctx, "Failed to verify accounts");
  return true;
}

bool verify_call_proof(verify_ctx_t* ctx) {
  evm_call_ctx_t evm         = {0};
  ssz_ob_t       state_proof = ssz_get(&ctx->proof, "state_proof");
  ssz_ob_t       header      = ssz_get(&state_proof, "header");
  evm.accounts               = ssz_get(&ctx->proof, "accounts");

  bool success = verify_evm_call(ctx, &evm);
  // state_root is derived from the account proofs during EVM setup. If present,
  // anchor it to the beacon chain by verifying the state proof and block header.
  if (success && !bytes_all_zero(bytes(evm.state_root, 32)))
    success = eth_verify_state_proof(ctx, state_proof, evm.state_root) && c4_verify_header(ctx, header, state_proof) == C4_SUCCESS;

  evm_call_ctx_free(&evm);
  ctx->success = success;
  return ctx->success;
}
