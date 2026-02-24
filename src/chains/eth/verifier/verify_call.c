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
#include "eth_call_cache.h"
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

/** Lazy allocator for per-call state (accounts list, EVM result across C4_PENDING). */
static pap_call_state_t* call_get_state(verify_ctx_t* ctx, bool create) {
  pap_call_state_t* state = (pap_call_state_t*) ctx->user_data;
  if (!state && create) {
    state               = safe_calloc(1, sizeof(pap_call_state_t));
    ctx->user_data      = state;
    ctx->user_data_free = pap_call_state_free;
  }
  return state;
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

// Build a deterministic eth_getProof payload for one account and its accessed storage keys.
// Returns number of accessed keys; out_payload and out_req_id are populated.
static uint32_t pap_build_proof_payload(cached_account_t* ac, buffer_t* out_payload, bytes32_t out_req_id, json_t block_id) {
  buffer_t keys_buf  = {0};
  uint32_t key_count = 0;
  for (uint32_t i = 0; i < ac->num_storage; i++) {
    if (ac->storage[i].accessed && ac->storage[i].verified_at == 0) {
      if (key_count > 0) buffer_append(&keys_buf, bytes((uint8_t*) ",", 1));
      bprintf(&keys_buf, "\"0x%x\"", bytes((uint8_t*) ac->storage[i].key, 32));
      key_count++;
    }
  }
  if (key_count == 0) {
    buffer_free(&keys_buf);
    return 0;
  }
  bprintf(out_payload, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_getProof\",\"params\":[\"0x%x\",[%s],%J]}",
          bytes((uint8_t*) ac->address, 20), (char*) keys_buf.data.data, block_id);
  buffer_free(&keys_buf);
  keccak(out_payload->data, out_req_id);
  return key_count;
}

// Verify a single eth_getProof response: init, type-check, account/state proof,
// sync update, header verification, and storage cache reconciliation.
// Returns true on success, false on error or C4_PENDING (errors propagated to ctx->state).
static bool pap_verify_proof_response(verify_ctx_t* ctx, cached_account_t* ac, bytes_t payload_data, bytes_t response, bool* values_changed) {
  json_t       payload_json = json_parse((char*) payload_data.data);
  json_t       proof_args   = json_get(payload_json, "params");
  verify_ctx_t proof_ctx    = {0};
  bool         result       = false;

  if (c4_verify_init(&proof_ctx, response, "eth_getProof", proof_args, ctx->chain_id, 0) != C4_SUCCESS) {
    if (proof_ctx.state.error) c4_state_add_error(&ctx->state, proof_ctx.state.error);
    goto cleanup;
  }

  if (!ssz_is_type(&proof_ctx.proof, eth_ssz_verification_type(ETH_SSZ_VERIFY_ACCOUNT_PROOF))) {
    c4_state_add_error(&ctx->state, "eth_getProof response has unexpected proof type");
    goto cleanup;
  }

  bytes32_t state_root   = {0};
  bytes32_t storage_hash = {0};
  ssz_ob_t  state_proof  = ssz_get(&proof_ctx.proof, "state_proof");

  if (!eth_verify_account_proof_exec(&proof_ctx, &proof_ctx.proof, state_root, ETH_ACCOUNT_STORAGE_HASH, bytes(storage_hash, 32))) {
    if (proof_ctx.state.error)
      c4_state_add_error(&ctx->state, proof_ctx.state.error);
    else
      c4_state_add_error(&ctx->state, "invalid account proof");
    goto cleanup;
  }

  if (!eth_verify_state_proof(&proof_ctx, state_proof, state_root)) {
    if (proof_ctx.state.error)
      c4_state_add_error(&ctx->state, proof_ctx.state.error);
    else
      c4_state_add_error(&ctx->state, "eth_getProof state proof verification failed");
    goto cleanup;
  }

  if (!c4_update_from_sync_data(&proof_ctx)) {
    if (proof_ctx.state.error) c4_state_add_error(&ctx->state, proof_ctx.state.error);
    goto cleanup;
  }

  // Header verification on MAIN ctx (data requests land on ctx->state for C4_PENDING handling).
  c4_status_t hdr_status = c4_verify_header(ctx, ssz_get(&state_proof, "header"), state_proof);
  if (hdr_status == C4_ERROR && !ctx->state.error) c4_state_add_error(&ctx->state, "header verification failed");
  if (hdr_status != C4_SUCCESS) goto cleanup;

  // Proof verified — reconcile proven storage values against cache.
  ssz_ob_t sp = ssz_get(&proof_ctx.proof, "storageProof");
  for (uint32_t j = 0; j < ssz_len(sp); j++) {
    ssz_ob_t  entry     = ssz_at(sp, j);
    bytes32_t proof_key = {0};
    bytes32_t proof_val = {0};
    bytes_t   pk        = ssz_get(&entry, "key").bytes;
    if (pk.data && pk.len == 32) memcpy(proof_key, pk.data, 32);
    if (!eth_get_storage_value(entry, proof_key, proof_val)) {
      c4_state_add_error(&ctx->state, "failed to extract storage value from proof");
      goto cleanup;
    }
    bytes32_t cached_val = {0};
    if (!eth_call_cache_get_storage(ac, proof_key, cached_val) || memcmp(cached_val, proof_val, 32) != 0)
      *values_changed = true;
    eth_call_cache_set_storage(ac, proof_key, proof_val, 1);
  }
  result = true;

cleanup:
  c4_verify_free_data(&proof_ctx);
  return result;
}

// Phase C + D: verify unverified accessed slots via eth_getProof, then persist and set result.
static bool verify_call_result_and_finish(verify_ctx_t* ctx, pap_call_state_t* state, bool is_simulate, bool is_estimate) {
  bool   all_verified   = true;
  bool   values_changed = false;
  json_t block_id       = json_at(ctx->args, 1);
  if (block_id.type != JSON_TYPE_STRING) block_id = json_parse("\"latest\"");

  for (cached_account_t* ac = state->accounts; ac; ac = ac->next) {
    buffer_t  payload = {0};
    bytes32_t req_id  = {0};
    if (pap_build_proof_payload(ac, &payload, req_id, block_id) == 0) continue;

    data_request_t* req = c4_state_get_data_request_by_id(&ctx->state, req_id);

    if (req && req->response.data) {
      if (!pap_verify_proof_response(ctx, ac, payload.data, req->response, &values_changed)) {
        buffer_free(&payload);
        return false;
      }
    }
    else if (!req) {
      data_request_t* new_req = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
      new_req->chain_id       = ctx->chain_id;
      new_req->encoding       = C4_DATA_ENCODING_SSZ;
      new_req->type           = C4_DATA_TYPE_PROVER;
      new_req->method         = C4_DATA_METHOD_POST;
      new_req->payload        = bytes_dup(payload.data);
      memcpy(new_req->id, req_id, 32);
      c4_state_add_request(&ctx->state, new_req);
      all_verified = false;
    }
    else {
      if (req->error) c4_state_add_error(&ctx->state, req->error);
      all_verified = false;
    }
    buffer_free(&payload);
    if (ctx->state.error) return false;
  }

  if (values_changed) {
    // we clear the result because we need to rerun it with the changed values.
    safe_free(state->call_result.data);
    state->call_result = NULL_BYTES;
    free_emitted_logs(state->logs);
    state->logs     = NULL;
    state->gas_used = 0;
    state->evm_done = false;
    return false;
  }
  if (!all_verified) return false;

  for (cached_account_t* ac = state->accounts; ac; ac = ac->next)
    eth_call_cache_save(ctx, ac->address, ac);

  evm_call_ctx_t evm = {
      .call_result = state->call_result,
      .logs        = state->logs,
      .gas_used    = state->gas_used,
  };

  bool match = is_simulate   ? match_simulate_result(ctx, &evm)
               : is_estimate ? match_estimate_result(ctx, &evm)
                             : match_call_result(ctx, &evm);

  state->call_result = evm.call_result;
  state->logs        = evm.logs;

  if (!match) RETURN_VERIFY_ERROR(ctx, is_simulate ? "Simulation result mismatch" : "Call result mismatch");
  ctx->success = true;
  return true;
}

bool verify_call_proof(verify_ctx_t* ctx) {
  bool is_simulate = ctx->method && strcmp(ctx->method, "colibri_simulateTransaction") == 0;
  bool is_estimate = ctx->method && strcmp(ctx->method, "eth_estimateGas") == 0;

  pap_call_state_t* state = call_get_state(ctx, false);
  if (state && state->evm_done)
    return verify_call_result_and_finish(ctx, state, is_simulate, is_estimate);

  state              = call_get_state(ctx, true);
  evm_call_ctx_t evm = {0};
  evm.accounts       = (ctx->proof.def && ctx->proof.def->type != SSZ_TYPE_NONE) ? ssz_get(&ctx->proof, "accounts") : (ssz_ob_t) {0};
  evm.pap_accounts   = &state->accounts;

  CHECK_JSON_VERIFY(ctx->args, "[{to:address,data:bytes,gas?:hexuint,value?:hexuint,gasPrice?:hexuint,from?:address},block?,{*:{balance?:hexuint,code?:bytes,state?:{*:bytes32},stateDiff?:{*:bytes32}}}?]", "Invalid transaction");
  json_t overrides_json = json_at(ctx->args, 2);
  if (overrides_json.type == JSON_TYPE_OBJECT && eth_parse_state_overrides(ctx, overrides_json, &evm.overrides) != C4_SUCCESS) return false;
  if (eth_get_call_codes(ctx, &evm.call_codes, evm.accounts) != C4_SUCCESS) {
    evm_call_ctx_free(&evm);
    return false;
  }

#ifdef EVMONE
  c4_status_t call_status = eth_run_call_evmone_with_events(ctx, &evm, is_simulate);
#else
  c4_status_t call_status = c4_state_add_error(&ctx->state, "no EVM is enabled, build with -DEVMONE=1");
#endif
  evm.gas_used += eth_intrinsic_gas(json_at(ctx->args, 0));

  // did we hit some missing values and wait to fetch them?
  if (call_status != C4_SUCCESS || c4_state_get_pending_request(&ctx->state)) {
    evm_call_ctx_free(&evm);
    return false;
  }

  // do we have a callproof so we verify the accounts and state proof?
  if (ctx->proof.def && ctx->proof.def->type != SSZ_TYPE_NONE) {
    ssz_ob_t state_proof = ssz_get(&ctx->proof, "state_proof");
    ssz_ob_t header      = ssz_get(&state_proof, "header");
    if (!c4_eth_verify_accounts(ctx, evm.accounts, evm.state_root, &evm.overrides)) {
      evm_call_ctx_free(&evm);
      return false;
    }
    if (!bytes_all_zero(bytes(evm.state_root, 32)) &&
        (!eth_verify_state_proof(ctx, state_proof, evm.state_root) || c4_verify_header(ctx, header, state_proof) != C4_SUCCESS)) {
      evm_call_ctx_free(&evm);
      return false;
    }
  }

  // cleanup and copy result to the state
  safe_free(state->call_result.data);
  free_emitted_logs(state->logs);
  state->call_result = bytes_dup(evm.call_result);
  state->logs        = evm.logs;
  evm.logs           = NULL;
  state->gas_used    = evm.gas_used;
  state->evm_done    = true;
  evm_call_ctx_free(&evm);

  return verify_call_result_and_finish(ctx, state, is_simulate, is_estimate);
}
