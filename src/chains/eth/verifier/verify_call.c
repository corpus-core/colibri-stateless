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

// :: Build phase: materialize call_account_t list from SSZ proof accounts

static call_account_t* call_accounts_from_ssz(ssz_ob_t ssz_accounts) {
  call_account_t* list = NULL;
  uint32_t        len  = ssz_len(ssz_accounts);
  for (uint32_t i = 0; i < len; i++) {
    ssz_ob_t        acc = ssz_at(ssz_accounts, i);
    call_account_t* ca  = safe_calloc(1, sizeof(call_account_t));

    bytes_t addr = ssz_get(&acc, "address").bytes;
    if (addr.data && addr.len >= 20) memcpy(ca->address, addr.data, 20);

    eth_get_account_value(acc, ETH_ACCOUNT_BALANCE, ca->balance);
    ca->flags |= ACCOUNT_HAS_BALANCE;

    eth_get_account_value(acc, ETH_ACCOUNT_CODE_HASH, ca->code_hash);
    ca->flags |= ACCOUNT_HAS_CODE_HASH;

    eth_get_account_value(acc, ETH_ACCOUNT_STORAGE_HASH, ca->storage_root);
    ca->flags |= ACCOUNT_HAS_STORAGE_ROOT;

    bytes32_t nonce_be = {0};
    eth_get_account_value(acc, ETH_ACCOUNT_NONCE, nonce_be);
    ca->nonce = uint64_from_be(nonce_be + 24);
    ca->flags |= ACCOUNT_HAS_NONCE;

    ssz_ob_t code = ssz_get(&acc, "code");
    if (code.def && code.def->type == SSZ_TYPE_LIST && code.bytes.len > 0) {
      ca->code = code.bytes;
      ca->flags |= ACCOUNT_HAS_CODE;
    }

    ssz_ob_t sp = ssz_get(&acc, "storageProof");
    for (uint32_t j = 0; j < ssz_len(sp); j++) {
      ssz_ob_t        entry = ssz_at(sp, j);
      call_storage_t* cs    = safe_calloc(1, sizeof(call_storage_t));
      bytes_t         pk    = ssz_get(&entry, "key").bytes;
      if (pk.data && pk.len == 32) memcpy(cs->key, pk.data, 32);
      if (eth_get_storage_value(entry, cs->key, cs->src_value))
        memcpy(cs->post_value, cs->src_value, 32);
      cs->source      = STORAGE_SRC_PROOF;
      cs->verified_at = 1;
      cs->next        = ca->storage;
      ca->storage     = cs;
    }

    ca->verified_at = 1;
    ca->next        = list;
    list            = ca;
  }
  return list;
}

bool c4_eth_verify_accounts(verify_ctx_t* ctx, ssz_ob_t accounts, bytes32_t state_root) {
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
  uint8_t* ptr       = (uint8_t*) log->topics;
  memcpy(log->data.data + 32 - b.len, b.data, b.len);
  json_t from = json_get(tx, "from");
  if (from.type == JSON_TYPE_STRING && from.len >= 5 && strncmp(from.start, "\"0x0\"", 5) != 0) {
    b = json_as_bytes(from, &value_buf);
    memcpy(ptr + 64 - b.len, b.data, b.len);
  }
  json_t to = json_get(tx, "to");
  if (to.type == JSON_TYPE_STRING && to.len >= 5 && strncmp(to.start, "\"0x0\"", 5) != 0) {
    b = json_as_bytes(to, &value_buf);
    memcpy(ptr + 96 - b.len, b.data, b.len);
  }
  const char* signature = "Transfer(address,address,uint256)";
  keccak(bytes((uint8_t*) signature, strlen(signature)), ptr);
}

#define G_TRANSACTION     21000
#define G_TXDATA_ZERO     4
#define G_TXDATA_NON_ZERO 16

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
  safe_free(evm->call_result.data);
  evm->call_result = NULL_BYTES;
  free_emitted_logs(evm->logs);
  evm->logs = NULL;
  call_account_free_list(evm->accounts);
  evm->accounts = NULL;
}

static void evm_call_ctx_free_ptr(void* ptr) {
  if (!ptr) return;
  evm_call_ctx_free((evm_call_ctx_t*) ptr);
  safe_free(ptr);
}

RETURNS_NONNULL static evm_call_ctx_t* call_get_evm_ctx(verify_ctx_t* ctx) {
  evm_call_ctx_t* evm = (evm_call_ctx_t*) ctx->user_data;
  if (!evm) {
    evm                 = safe_calloc(1, sizeof(evm_call_ctx_t));
    evm->accounts       = NULL;
    evm->evm_done       = false;
    evm->pap_mode       = ctx->flags & VERIFY_FLAG_PAP;
    ctx->user_data      = evm;
    ctx->user_data_free = evm_call_ctx_free_ptr;
  }
  return evm;
}

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
    evm->call_result = NULL_BYTES;
    return true;
  }
  return evm->call_result.data && bytes_eq(evm->call_result, ctx->data.bytes);
}

bool verify_evm_call(verify_ctx_t* ctx, evm_call_ctx_t* evm) {
  bool is_simulate = ctx->method && strcmp(ctx->method, "colibri_simulateTransaction") == 0;
  bool is_estimate = ctx->method && strcmp(ctx->method, "eth_estimateGas") == 0;

  CHECK_JSON_VERIFY(ctx->args, "[{to:address,data:bytes,gas?:hexuint,value?:hexuint,gasPrice?:hexuint,from?:address},block?,{*:{balance?:hexuint,code?:bytes,state?:{*:bytes32},stateDiff?:{*:bytes32}}}?]", "Invalid transaction");

  ssz_ob_t ssz_accounts = (ctx->proof.def && ctx->proof.def->type != SSZ_TYPE_NONE) ? ssz_get(&ctx->proof, "accounts") : (ssz_ob_t) {0};

  if (!evm->accounts && ssz_accounts.def)
    evm->accounts = call_accounts_from_ssz(ssz_accounts);

  if (eth_resolve_account_codes(ctx, evm->accounts) != C4_SUCCESS) return false;
  if (call_apply_state_overrides(ctx, &evm->accounts, json_at(ctx->args, 2)) != C4_SUCCESS) return false;

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
  if (!c4_eth_verify_accounts(ctx, ssz_accounts, evm->state_root)) RETURN_VERIFY_ERROR(ctx, "Failed to verify accounts");
  return true;
}

// :: PAP Phase C + D helpers

static uint32_t pap_build_proof_payload(call_account_t* ac, buffer_t* out_payload, bytes32_t out_req_id, json_t block_id) {
  buffer_t keys_buf  = {0};
  uint32_t key_count = 0;
  for (call_storage_t* s = ac->storage; s; s = s->next) {
    if (s->accessed && s->verified_at == 0) {
      if (key_count > 0) buffer_append(&keys_buf, bytes((uint8_t*) ",", 1));
      bprintf(&keys_buf, "\"0x%x\"", bytes((uint8_t*) s->key, 32));
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

static bool pap_verify_proof_response(verify_ctx_t* ctx, call_account_t* ac, bytes_t payload_data, bytes_t response, bool* values_changed) {
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

  c4_status_t hdr_status = c4_verify_header(ctx, ssz_get(&state_proof, "header"), state_proof);
  if (hdr_status == C4_ERROR && !ctx->state.error) c4_state_add_error(&ctx->state, "header verification failed");
  if (hdr_status != C4_SUCCESS) goto cleanup;

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
    call_storage_t* cs = call_storage_find(ac, proof_key);
    if (!cs || memcmp(cs->src_value, proof_val, 32) != 0)
      *values_changed = true;
    if (cs) {
      memcpy(cs->src_value, proof_val, 32);
      memcpy(cs->post_value, proof_val, 32);
      cs->verified_at = 1;
      cs->source      = STORAGE_SRC_PROOF;
      cs->modified    = false;
    }
    else
      eth_call_cache_set_storage(ac, proof_key, proof_val, STORAGE_SRC_PROOF, 1);
  }
  result = true;

cleanup:
  c4_verify_free_data(&proof_ctx);
  return result;
}

static bool verify_call_result_and_finish(verify_ctx_t* ctx, evm_call_ctx_t* evm, bool is_simulate, bool is_estimate) {
  bool   all_verified   = true;
  bool   values_changed = false;
  json_t block_id       = json_at(ctx->args, 1);
  if (block_id.type != JSON_TYPE_STRING) block_id = json_parse("\"latest\"");

  for (call_account_t* ac = evm->accounts; ac; ac = ac->next) {
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
    safe_free(evm->call_result.data);
    evm->call_result = NULL_BYTES;
    free_emitted_logs(evm->logs);
    evm->logs     = NULL;
    evm->gas_used = 0;
    evm->evm_done = false;
    return false;
  }
  if (!all_verified) return false;

  for (call_account_t* ac = evm->accounts; ac; ac = ac->next)
    eth_call_cache_save(ctx, ac->address, ac);

  bool match = is_simulate   ? match_simulate_result(ctx, evm)
               : is_estimate ? match_estimate_result(ctx, evm)
                             : match_call_result(ctx, evm);

  if (!match) RETURN_VERIFY_ERROR(ctx, is_simulate ? "Simulation result mismatch" : "Call result mismatch");
  ctx->success = true;
  return true;
}

bool verify_call_proof(verify_ctx_t* ctx) {
  bool            is_simulate = ctx->method && strcmp(ctx->method, "colibri_simulateTransaction") == 0;
  bool            is_estimate = ctx->method && strcmp(ctx->method, "eth_estimateGas") == 0;
  bool            has_proof   = ctx->proof.def && ctx->proof.def->type != SSZ_TYPE_NONE;
  evm_call_ctx_t* evm         = call_get_evm_ctx(ctx);

  if (evm->evm_done) return verify_call_result_and_finish(ctx, evm, is_simulate, is_estimate);

  CHECK_JSON_VERIFY(ctx->args, "[{to:address,data:bytes,gas?:hexuint,value?:hexuint,gasPrice?:hexuint,from?:address},block?,{*:{balance?:hexuint,code?:bytes,state?:{*:bytes32},stateDiff?:{*:bytes32}}}?]", "Invalid transaction");

  // prepare the accounts and storage
  if (!evm->accounts && has_proof) {
    ssz_ob_t accounts    = ssz_get(&ctx->proof, "accounts");
    ssz_ob_t state_proof = ssz_get(&ctx->proof, "state_proof");
    ssz_ob_t header      = ssz_get(&state_proof, "header");
    if (!c4_eth_verify_accounts(ctx, accounts, evm->state_root)) return false;
    if (!bytes_all_zero(bytes(evm->state_root, 32)) &&
        (!eth_verify_state_proof(ctx, state_proof, evm->state_root) || c4_verify_header(ctx, header, state_proof) != C4_SUCCESS))
      return false;
    evm->accounts = call_accounts_from_ssz(accounts);
  }
  if (eth_resolve_account_codes(ctx, evm->accounts) != C4_SUCCESS) return false;
  if (call_apply_state_overrides(ctx, &evm->accounts, json_at(ctx->args, 2)) != C4_SUCCESS) return false;

#ifdef EVMONE
  c4_status_t call_status = eth_run_call_evmone_with_events(ctx, evm, is_simulate);
#else
  c4_status_t call_status = c4_state_add_error(&ctx->state, "no EVM is enabled, build with -DEVMONE=1");
#endif
  evm->gas_used += eth_intrinsic_gas(json_at(ctx->args, 0));

  if (call_status != C4_SUCCESS || c4_state_get_pending_request(&ctx->state)) return false;

  evm->evm_done = true;
  return verify_call_result_and_finish(ctx, evm, is_simulate, is_estimate);
}
