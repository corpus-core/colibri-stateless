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
#include "el_header.h"
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

// :: Build phase: materialize call_account_t list from SSZ proof accounts

static call_account_t* call_accounts_from_ssz(ssz_ob_t ssz_accounts) {
  call_account_t* list = NULL;
  uint32_t        len  = ssz_len(ssz_accounts);
  for (uint32_t i = 0; i < len; i++) {
    ssz_ob_t        acc  = ssz_at(ssz_accounts, i);
    call_account_t* ca   = safe_calloc(1, sizeof(call_account_t));
    bytes_t         addr = ssz_get(&acc, "address").bytes;
    ca->storage          = NULL; // redundant (calloc zeroes), but silences static analyzer

    if (addr.data && addr.len >= 20) memcpy(ca->address, addr.data, 20);
    ca->flags = ACCOUNT_HAS_BALANCE | ACCOUNT_HAS_CODE_HASH | ACCOUNT_HAS_STORAGE_ROOT | ACCOUNT_HAS_NONCE;

    // walk the MPT proof to distinguish existing from non-existing accounts
    bytes32_t addr_hash   = {0};
    bytes32_t dummy_root  = {0};
    bytes_t   rlp_account = {0};
    keccak(addr, addr_hash);
    patricia_result_t mpt_result = patricia_verify(dummy_root, bytes(addr_hash, 32), ssz_get(&acc, "accountProof"), &rlp_account);

    if (mpt_result == PATRICIA_FOUND && rlp_account.data) {
      bytes_t field_value = {0};
      bytes_t rlp_list    = rlp_account;
      if (rlp_decode(&rlp_list, 0, &rlp_list) == RLP_LIST) {
        if (rlp_decode(&rlp_list, ETH_ACCOUNT_NONCE - 1, &field_value) == RLP_ITEM && field_value.len <= 32) {
          bytes32_t nonce_be = {0};
          memcpy(nonce_be + 32 - field_value.len, field_value.data, field_value.len);
          ca->nonce = uint64_from_be(nonce_be + 24);
        }
        if (rlp_decode(&rlp_list, ETH_ACCOUNT_BALANCE - 1, &field_value) == RLP_ITEM && field_value.len <= 32)
          memcpy(ca->balance + 32 - field_value.len, field_value.data, field_value.len);
        if (rlp_decode(&rlp_list, ETH_ACCOUNT_CODE_HASH - 1, &field_value) == RLP_ITEM && field_value.len <= 32)
          memcpy(ca->code_hash + 32 - field_value.len, field_value.data, field_value.len);
        else
          memcpy(ca->code_hash, EMPTY_HASH, 32);
        if (rlp_decode(&rlp_list, ETH_ACCOUNT_STORAGE_HASH - 1, &field_value) == RLP_ITEM && field_value.len <= 32)
          memcpy(ca->storage_root + 32 - field_value.len, field_value.data, field_value.len);
        else
          memcpy(ca->storage_root, EMPTY_ROOT_HASH, 32);
      }
    }
    else {
      memcpy(ca->code_hash, EMPTY_HASH, 32);
      memcpy(ca->storage_root, EMPTY_ROOT_HASH, 32);
    }

    // The SSZ "code" field is a union: either a byte list (full contract
    // code) or a boolean "code_used" flag (false = no code, true = code
    // exists but was not included in the proof).
    ssz_ob_t code = ssz_get(&acc, "code");
    if (code.def && ssz_is_list_type(code.def) && code.bytes.len > 0) {
      ca->code = code.bytes;
      ca->flags |= ACCOUNT_HAS_CODE;
    }
    else if (code.def && code.def->type == SSZ_TYPE_BOOLEAN && !code.bytes.data[0]) {
      // code_used == false: account has no code (empty code hash).
      ca->code = NULL_BYTES;
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
    if (ssz_is_list_type(code.def)) {
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
  // reverse trace list to chronological order (entries were prepended during execution)
  {
    trace_entry_t* prev = NULL;
    trace_entry_t* cur  = evm->traces;
    while (cur) {
      trace_entry_t* next = cur->next;
      cur->next           = prev;
      prev                = cur;
      cur                 = next;
    }
    evm->traces = prev;
  }

  // Reverse the log list to chronological order. Logs are prepended during
  // execution (LIFO): host_emit_log for VM logs, emit_eth_transfer_log for the
  // EIP-7708 protocol-generated logs (top-level Tx, nested CALL/CREATE with
  // value, SELFDESTRUCT). After the reversal the top-level transfer log is
  // first, matching EIP-7708's "before any other logs created by EVM
  // execution" ordering.
  {
    emitted_log_t* prev = NULL;
    emitted_log_t* cur  = evm->logs;
    while (cur) {
      emitted_log_t* next = cur->next;
      cur->next           = prev;
      prev                = cur;
      cur                 = next;
    }
    evm->logs = prev;
  }
  // A revert is reflected in the simulation result via `success = false`.
  // The revert bytes are already in `evm->call_result` and are carried as
  // the call output for callers that want to decode them.
  bool     evm_success       = ctx->state.error == NULL && !evm->reverted;
  ssz_ob_t simulation_result = eth_build_simulation_result_ssz(evm->call_result, evm->logs, evm_success, evm->gas_used, NULL, evm->accounts, evm->keccak_entries, evm->traces);

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
  // Like eth_call: a reverting estimateGas is a valid outcome -- expose the
  // revert data through the standard channel so hosts can throw a structured
  // JSON-RPC error (code 3, data = revert bytes). The REVERTED flag is set
  // only on success branches and the def is pinned to bytes so callers get
  // a hex string instead of a uint256 hex.
  if (evm->reverted) {
    if (ctx->data.def == NULL || ctx->data.def->type == SSZ_TYPE_NONE) {
      ctx->data = (ssz_ob_t) {.bytes = evm->call_result, .def = eth_ssz_verification_type(ETH_SSZ_DATA_BYTES)};
      if (evm->call_result.data) ctx->flags |= VERIFY_FLAG_FREE_DATA;
      ctx->flags |= VERIFY_FLAG_REVERTED;
      evm->call_result = NULL_BYTES;
      return true;
    }
    if (!bytes_eq(evm->call_result, ctx->data.bytes)) return false;
    ctx->data.def = eth_ssz_verification_type(ETH_SSZ_DATA_BYTES);
    ctx->flags |= VERIFY_FLAG_REVERTED;
    return true;
  }
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
  // Propagate revert state. The `call_result` for a revert may be empty
  // (raw `revert();` without data); we still hand over an empty bytes
  // object so the binding can surface the revert. The REVERTED flag is set
  // only on the success branches -- a byte-equality mismatch must still
  // raise the regular "Call result mismatch" error without lingering flags.
  if (evm->reverted) {
    if (ctx->data.def == NULL || ctx->data.def->type == SSZ_TYPE_NONE) {
      ctx->data = (ssz_ob_t) {.bytes = evm->call_result, .def = eth_ssz_verification_type(ETH_SSZ_DATA_BYTES)};
      if (evm->call_result.data) ctx->flags |= VERIFY_FLAG_FREE_DATA;
      ctx->flags |= VERIFY_FLAG_REVERTED;
      evm->call_result = NULL_BYTES;
      return true;
    }
    // remote prover provided expected revert bytes -- enforce byte-equality
    if (!bytes_eq(evm->call_result, ctx->data.bytes)) return false;
    // force-pin the def to bytes: even if the caller pre-set a different SSZ
    // type, the JSON serializer must dump the revert bytes as a hex string.
    ctx->data.def = eth_ssz_verification_type(ETH_SSZ_DATA_BYTES);
    ctx->flags |= VERIFY_FLAG_REVERTED;
    return true;
  }
  if (ctx->data.def == NULL || ctx->data.def->type == SSZ_TYPE_NONE) {
    if (evm->call_result.data) {
      ctx->data = (ssz_ob_t) {.bytes = evm->call_result, .def = eth_ssz_verification_type(ETH_SSZ_DATA_BYTES)};
      ctx->flags |= VERIFY_FLAG_FREE_DATA;
      evm->call_result = NULL_BYTES;
    }
    else
      ctx->data = (ssz_ob_t) {.bytes = bytes(ctx, 0), .def = eth_ssz_verification_type(ETH_SSZ_DATA_BYTES)};
    return true;
  }
  return evm->call_result.data && bytes_eq(evm->call_result, ctx->data.bytes);
}

// :: EIP-7702 authorization list

#define EIP7702_MAGIC      0x05
#define EIP7702_MARKER_LEN 23

/**
 * Apply EIP-7702 authorization list before EVM execution.
 *
 * For each tuple `{chainId, address, nonce, yParity, r, s}` in the list:
 * - recovers the authority via `ecrecover(keccak(0x05 || rlp([chainId, address, nonce])), yParity, r, s)`
 * - verifies chain_id (must be 0 or match `ctx->chain_id`)
 * - verifies the authority's code is empty or already a delegation indicator
 * - verifies the authority's nonce matches
 * - writes `0xef0100 || address` as the authority's code
 * - increments the authority's nonce
 *
 * If any check fails for a tuple it is silently skipped (per EIP-7702 spec).
 *
 * @param ctx the verification context (used for chain_id)
 * @param accounts pointer to the account list (may be extended)
 * @param tx the transaction JSON object that may contain `authorizationList`
 * @return `C4_SUCCESS` always (invalid tuples are skipped, not fatal)
 */
static c4_status_t call_apply_authorization_list(verify_ctx_t* ctx, call_account_t** accounts, json_t tx) {
  json_t auth_list = json_get(tx, "authorizationList");
  if (auth_list.type != JSON_TYPE_ARRAY) return C4_SUCCESS;

  size_t   len = json_len(auth_list);
  buffer_t buf = {0};

  for (size_t i = 0; i < len; i++) {
    json_t entry = json_at(auth_list, i);

    // parse fields
    uint64_t  chain_id = json_get_uint64(entry, "chainId");
    address_t target   = {0};
    buffer_t  addr_buf = stack_buffer(target);
    if (json_get_bytes(entry, "address", &addr_buf).len != 20) continue;

    uint64_t nonce = json_get_uint64(entry, "nonce");
    if (nonce >= UINT64_MAX) continue;

    uint8_t  sig[65] = {0};
    bytes_t  r_bytes = {0};
    bytes_t  s_bytes = {0};
    buffer_t r_buf   = stack_buffer(sig);
    buffer_t s_buf   = {.data = bytes(sig + 32, 32), .allocated = -32};
    r_bytes          = json_get_bytes(entry, "r", &r_buf);
    s_bytes          = json_get_bytes(entry, "s", &s_buf);
    if (r_bytes.len == 0 || r_bytes.len > 32) continue;
    if (s_bytes.len == 0 || s_bytes.len > 32) continue;
    // right-align r and s in their 32-byte slots
    if (r_bytes.len < 32) {
      memmove(sig + 32 - r_bytes.len, sig, r_bytes.len);
      memset(sig, 0, 32 - r_bytes.len);
    }
    if (s_bytes.len < 32) {
      memmove(sig + 64 - s_bytes.len, sig + 32, s_bytes.len);
      memset(sig + 32, 0, 32 - s_bytes.len);
    }
    sig[64] = (uint8_t) json_get_uint64(entry, "yParity");

    // verify chain_id
    if (chain_id != 0 && chain_id != ctx->chain_id) continue;

    // build signing digest: keccak(MAGIC || rlp([chain_id, address, nonce]))
    buffer_reset(&buf);
    rlp_add_uint64(&buf, chain_id);
    rlp_add_item(&buf, bytes(target, 20));
    rlp_add_uint64(&buf, nonce);
    rlp_to_list(&buf);
    buffer_splice(&buf, 0, 0, bytes(NULL, 1));
    buf.data.data[0] = EIP7702_MAGIC;

    bytes32_t digest = {0};
    keccak(buf.data, digest);

    // recover authority address
    uint8_t pubkey[64] = {0};
    if (!secp256k1_recover(digest, bytes(sig, 65), pubkey)) continue;

    address_t authority = {0};
    bytes32_t pub_hash  = {0};
    keccak(bytes(pubkey, 64), pub_hash);
    memcpy(authority, pub_hash + 12, 20);

    // find or create the authority account
    call_account_t* acc = call_account_list_get_or_create(accounts, authority);

    // verify code is empty or already a delegation indicator
    if (acc->flags & ACCOUNT_HAS_CODE) {
      if (acc->code.len != 0 && !(acc->code.len == EIP7702_MARKER_LEN && acc->code.data[0] == 0xef && acc->code.data[1] == 0x01 && acc->code.data[2] == 0x00))
        continue;
    }

    // verify nonce
    if ((acc->flags & ACCOUNT_HAS_NONCE) && acc->nonce != nonce) continue;

    // apply delegation: set code to 0xef0100 || target_address
    uint8_t* delegation_code = safe_malloc(EIP7702_MARKER_LEN);
    delegation_code[0]       = 0xef;
    delegation_code[1]       = 0x01;
    delegation_code[2]       = 0x00;
    memcpy(delegation_code + 3, target, 20);

    if (acc->flags & ACCOUNT_FREE_CODE) safe_free(acc->code.data);
    acc->code = bytes(delegation_code, EIP7702_MARKER_LEN);
    acc->flags |= ACCOUNT_HAS_CODE | ACCOUNT_FREE_CODE;

    // special case: zero address clears delegation
    if (bytes_all_zero(bytes(target, 20))) {
      safe_free(delegation_code);
      acc->code = NULL_BYTES;
      acc->flags |= ACCOUNT_HAS_CODE;
      acc->flags &= ~ACCOUNT_FREE_CODE;
    }

    // increment nonce
    acc->nonce++;
    acc->flags |= ACCOUNT_HAS_NONCE;
  }

  buffer_free(&buf);
  return C4_SUCCESS;
}

// Freshness check for eth_call/eth_estimateGas/colibri_simulateTransaction.
//
// `ctx` supplies the request args, the host-supplied lower bound and the
// error sink. `proof_ctx` is the context whose `user_data` (`evm_call_ctx_t`)
// holds the verified EL header: both the direct path and PAP
// (`pap_verify_proof_response`) pass the outer ctx, because that is where
// `call_get_evm_ctx` stores `el_header`. Passing the inner PAP `proof_ctx`
// would find no block context. The actual freshness logic lives in
// `eth_check_latest_freshness` so all block-tag methods share one error path.
static bool verify_call_freshness(verify_ctx_t* ctx, verify_ctx_t* proof_ctx) {
  bool                     is_latest = eth_json_is_latest(json_at(ctx->args, 1));
  eth_call_block_context_t bctx      = {0};
  bool                     has_ts    = eth_get_call_block_context_from_proof(proof_ctx, &bctx);
  return eth_check_latest_freshness(ctx, is_latest, has_ts, bctx.timestamp);
}

// shared helper: resolve codes, apply state overrides and EIP-7702 authorization list
static bool prepare_evm_call(verify_ctx_t* ctx, evm_call_ctx_t* evm, bool apply_overrides) {
  if (eth_resolve_account_codes(ctx, evm->accounts) != C4_SUCCESS) return false;
  if (apply_overrides && call_apply_state_overrides(ctx, &evm->accounts, json_at(ctx->args, 2)) != C4_SUCCESS) return false;
  if (call_apply_authorization_list(ctx, &evm->accounts, json_at(ctx->args, 0)) != C4_SUCCESS) return false;
  return true;
}

// shared helper: run the EVM and add intrinsic gas
static c4_status_t run_evm_call(verify_ctx_t* ctx, evm_call_ctx_t* evm, bool capture_events) {
#ifdef EVMONE
  c4_status_t status = eth_run_call_evmone_with_events(ctx, evm, capture_events);
#else
  c4_status_t status = c4_state_add_error(&ctx->state, "no EVM is enabled, build with -DEVMONE=1");
#endif
  evm->gas_used += eth_intrinsic_gas(json_at(ctx->args, 0));
  return status;
}

// shared helper: match the EVM result against the expected value
static bool match_evm_result(verify_ctx_t* ctx, evm_call_ctx_t* evm, bool is_simulate, bool is_estimate) {
  bool match = is_simulate   ? match_simulate_result(ctx, evm)
               : is_estimate ? match_estimate_result(ctx, evm)
                             : match_call_result(ctx, evm);
  if (!match) RETURN_VERIFY_ERROR(ctx, is_simulate ? "Simulation result mismatch" : "Call result mismatch");
  return true;
}

bool verify_evm_call(verify_ctx_t* ctx, evm_call_ctx_t* evm) {
  bool is_simulate = ctx->method && strcmp(ctx->method, "colibri_simulateTransaction") == 0;
  bool is_estimate = ctx->method && strcmp(ctx->method, "eth_estimateGas") == 0;

  CHECK_JSON_VERIFY(ctx->args, "[{to:address,data:bytes,gas?:hexuint,value?:hexuint,gasPrice?:hexuint,from?:address},block?,{*:{balance?:hexuint,code?:bytes,state?:{*:bytes32},stateDiff?:{*:bytes32}}}?]", "Invalid transaction");

  ssz_ob_t ssz_accounts = (ctx->proof.def && ctx->proof.def->type != SSZ_TYPE_NONE) ? ssz_get(&ctx->proof, "accounts") : (ssz_ob_t) {0};

  if (!evm->accounts && ssz_accounts.def)
    evm->accounts = call_accounts_from_ssz(ssz_accounts);

  if (!prepare_evm_call(ctx, evm, true)) return false;
  if (run_evm_call(ctx, evm, is_simulate) != C4_SUCCESS) return false;
  if (!match_evm_result(ctx, evm, is_simulate, is_estimate)) return false;
  if (!c4_eth_verify_accounts(ctx, ssz_accounts, evm->state_root)) RETURN_VERIFY_ERROR(ctx, "Failed to verify accounts");
  return true;
}

// :: PAP Phase C + D helpers

static bool pap_verify_proof_response(verify_ctx_t* ctx, call_account_t* call_accounts, bytes_t response, bool* values_changed) {
  verify_ctx_t    proof_ctx  = {0};
  bytes32_t       state_root = {0};
  evm_call_ctx_t* evm        = call_get_evm_ctx(ctx);
  bool            result     = false;

  // Inherit the outer verification flags into the inner sub-proof context so user-facing
  // policy flags (VERIFY_FLAG_SKIP_WSP_CHECK, VERIFY_FLAG_HYBRID, VERIFY_FLAG_OBLIVIOUS, …)
  // behave consistently across both contexts. VERIFY_FLAG_FREE_DATA must be masked out:
  // proof_ctx.data points into the prover response (owned by the data_request_t), so
  // c4_verify_free_data must not free it. VERIFY_FLAG_SYNC_REINIT_TRIED is a transient
  // internal guard scoped to a single verification context; the inner sub-proof must be
  // allowed its own one-time sync-state recovery, so it is masked out as well.
  verify_flags_t inner_flags = ctx->flags & ~(VERIFY_FLAG_FREE_DATA | VERIFY_FLAG_SYNC_REINIT_TRIED);
  if (c4_verify_init(&proof_ctx, response, "eth_call", ctx->args, ctx->chain_id, inner_flags) != C4_SUCCESS) {
    if (proof_ctx.state.error) c4_state_add_error(&ctx->state, proof_ctx.state.error);
    goto cleanup;
  }

  if (!ssz_is_type(&proof_ctx.proof, eth_ssz_verification_type(ETH_SSZ_VERIFY_CALL_PROOF))) {
    c4_state_add_error(&ctx->state, "proofCall response has unexpected proof type");
    goto cleanup;
  }

  if (!c4_eth_verify_accounts(ctx, ssz_get(&proof_ctx.proof, "accounts"), state_root)) {
    if (proof_ctx.state.error)
      c4_state_add_error(&ctx->state, proof_ctx.state.error);
    else
      c4_state_add_error(&ctx->state, "invalid account proof");
    goto cleanup;
  }

  // Apply sync_data on the outer ctx before verifying the block so bootstrap /
  // light-client validators are in storage when c4_verify_header runs. Pending
  // WSP and validator requests must live on `ctx` because the host fulfils
  // against that list (same reason pap_tx applies sync_data before c4_verify_block).
  ctx->sync_data          = proof_ctx.sync_data;
  c4_status_t sd_status   = c4_update_from_sync_data(ctx);
  if (sd_status == C4_PENDING) goto cleanup;
  if (sd_status != C4_SUCCESS) goto cleanup;

  c4_status_t block_status = c4_verify_block(ctx, ssz_get(&proof_ctx.proof, "block"), &evm->el_header, evm->el_block_hash);
  if (block_status == C4_PENDING) goto cleanup;
  if (block_status != C4_SUCCESS) {
    if (!ctx->state.error)
      c4_state_add_error(&ctx->state, "proofCall state proof verification failed");
    goto cleanup;
  }

  bytes_t header_state_root = eth_el_header_get(evm->el_header, EL_STATE_ROOT);
  if (!header_state_root.data || header_state_root.len != 32 || memcmp(header_state_root.data, state_root, 32) != 0) {
    c4_state_add_error(&ctx->state, "proofCall state proof verification failed");
    goto cleanup;
  }

  // Freshness gate for PAP: in PAP mode there is no usable proof when
  // verify_call_proof first runs, so the direct-path gate skips it. The call
  // proof arrives here via colibri_proofCall and has just been Merkle-verified
  // above (`evm->el_header` is populated). Timestamp is read from that header
  // via the outer ctx's user_data; args / min_ts / errors also live there.
  if (!verify_call_freshness(ctx, ctx)) goto cleanup;

  // Proof is valid, so we check the values for changes
  ssz_ob_t accounts     = ssz_get(&proof_ctx.proof, "accounts");
  uint32_t num_accounts = ssz_len(accounts);
  for (uint32_t i = 0; i < num_accounts; i++) {
    ssz_ob_t        ac          = ssz_at(accounts, i);
    uint8_t*        addr        = ssz_get(&ac, "address").bytes.data;
    call_account_t* acc         = NULL;
    ssz_ob_t        sp          = ssz_get(&ac, "storageProof");
    uint32_t        num_storage = ssz_len(sp);
    for (call_account_t* a = call_accounts; a; a = a->next) {
      if (memcmp(a->address, addr, 20) == 0) {
        acc = a;
        break;
      }
    }
    if (!acc) continue;

    for (uint32_t j = 0; j < num_storage; j++) {
      ssz_ob_t  entry     = ssz_at(sp, j);
      bytes32_t proof_key = {0};
      bytes32_t proof_val = {0};
      bytes_t   pk        = ssz_get(&entry, "key").bytes;
      if (pk.data && pk.len == 32) memcpy(proof_key, pk.data, 32);
      if (!eth_get_storage_value(entry, proof_key, proof_val)) {
        c4_state_add_error(&ctx->state, "failed to extract storage value from proof");
        goto cleanup;
      }
      call_storage_t* cs = call_storage_find(acc, proof_key);
      if (!cs || memcmp(cs->src_value, proof_val, 32) != 0)
        *values_changed = true;
      if (cs) {
        memcpy(cs->src_value, proof_val, 32);
        cs->verified_at = 1;
        cs->source      = STORAGE_SRC_PROOF;
      }
      else
        call_account_set_storage(acc, proof_key, proof_val, STORAGE_SRC_PROOF, 1);
    }
  }

  result = true;

cleanup:
  // Safety net: if any unexpected requests still sit on the inner sub-context
  // (e.g. a future helper that emits requests through proof_ctx between the
  // lend/return block and cleanup), forward them to the outer ctx instead of
  // leaking them via c4_state_free below.
  c4_state_take_requests(&ctx->state, &proof_ctx.state);
  c4_verify_free_data(&proof_ctx);
  return result;
}

static bool has_unverified_storage(call_account_t* ac) {
  for (call_storage_t* s = ac->storage; s; s = s->next) {
    if (s->accessed && s->verified_at == 0) return true;
  }
  return false;
}

static bool proof_call(verify_ctx_t* ctx, evm_call_ctx_t* evm) {
  buffer_t  payload        = {0};
  bytes32_t req_id         = {0};
  bool      firstAccount   = true;
  bool      firstStorage   = true;
  bool      values_changed = false;
  json_t    block_id       = json_at(ctx->args, 1);

  if (block_id.type != JSON_TYPE_STRING) block_id = json_parse("\"latest\"");
  buffer_add_chars(&payload, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"colibri_proofCall\",\"params\":[{\"accessList\":[");
  for (call_account_t* ac = evm->accounts; ac; ac = ac->next) {
    if (!has_unverified_storage(ac)) continue;
    if (firstAccount)
      firstAccount = false;
    else
      buffer_add_chars(&payload, ",");
    firstStorage = true;
    bprintf(&payload, "{\"address\":\"0x%x\",\"storageKeys\":[", bytes(ac->address, 20));
    for (call_storage_t* s = ac->storage; s; s = s->next) {
      if (s->accessed && s->verified_at == 0) {
        if (firstStorage)
          firstStorage = false;
        else
          buffer_add_chars(&payload, ",");
        bprintf(&payload, "\"0x%x\"", bytes(s->key, 32));
      }
    }
    buffer_add_chars(&payload, "]}");
  }
  bprintf(&payload, "]},%J]}", block_id);
  keccak(payload.data, req_id);
  data_request_t* req = c4_state_get_data_request_by_id(&ctx->state, req_id);
  if (req && req->response.data) {
    buffer_free(&payload);
    bool result = pap_verify_proof_response(ctx, evm->accounts, req->response, &values_changed);
    if (!result) return false;
    req->validated = true;
    if (values_changed) {
      evm->evm_done = false; // we need to repeat with the updates values
      return true;
    }
    return result;
  }
  else if (!req) {
    data_request_t* new_req = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
    new_req->chain_id       = ctx->chain_id;
    new_req->encoding       = C4_DATA_ENCODING_SSZ;
    new_req->type           = C4_DATA_TYPE_PROVER;
    new_req->method         = C4_DATA_METHOD_POST;
    new_req->payload        = payload.data;
    memcpy(new_req->id, req_id, 32);
    c4_state_add_request(&ctx->state, new_req);
    return false;
  }
  else {
    if (req->error) c4_state_add_error(&ctx->state, req->error);
    buffer_free(&payload);
    return false;
  }
}

static bool verify_call_result_and_finish(verify_ctx_t* ctx, evm_call_ctx_t* evm, bool is_simulate, bool is_estimate) {
  bool all_verified = true;

  // check if we need to use proofCall
  for (call_account_t* ac = evm->accounts; ac; ac = ac->next) {
    if (has_unverified_storage(ac)) {
      all_verified = false;
      break;
    }
  }

  // verify the values
  if (!all_verified && !proof_call(ctx, evm)) return false;
  if (!evm->evm_done) return true;

  ctx->success = is_simulate   ? match_simulate_result(ctx, evm)
                 : is_estimate ? match_estimate_result(ctx, evm)
                               : match_call_result(ctx, evm);

  if (evm->pap_mode && json_len(ctx->args) < 3) { // only save the cache if we are not using state overrides

    for (call_account_t* ac = evm->accounts; ac; ac = ac->next) {
      ac->verified_at = 0;
      if (ac->flags & ACCOUNT_FREE_CODE) {
        safe_free(ac->code.data);
        ac->code = NULL_BYTES;
      }
      ac->flags &= ~ACCOUNT_HAS_CODE;
      ac->flags &= ~ACCOUNT_FREE_CODE;
      ac->flags &= ~ACCOUNT_DELETED;
      ac->flags &= ~ACCOUNT_FULL_STATE;
      for (call_storage_t* s = ac->storage; s; s = s->next) {
        s->verified_at = 0;
        s->source      = STORAGE_SRC_NONE;
        s->modified    = false;
        s->accessed    = false;
      }
      eth_call_account_cache_save(ctx, ac->address, ac);
    }
  }

  if (!ctx->success) RETURN_VERIFY_ERROR(ctx, is_simulate ? "Simulation result mismatch" : "Call result mismatch");
  return true;
}

bool verify_call_proof(verify_ctx_t* ctx) {
  bool            is_simulate   = ctx->method && strcmp(ctx->method, "colibri_simulateTransaction") == 0;
  bool            is_estimate   = ctx->method && strcmp(ctx->method, "eth_estimateGas") == 0;
  bool            has_overrides = json_len(ctx->args) > 2 && json_at(ctx->args, 2).type == JSON_TYPE_OBJECT;
  bool            has_proof     = ctx->proof.def && ctx->proof.def->type != SSZ_TYPE_NONE;
  bool            is_pap        = ctx->flags & VERIFY_FLAG_PAP;
  evm_call_ctx_t* evm           = call_get_evm_ctx(ctx);

  if (evm->evm_done) {
    bool success = verify_call_result_and_finish(ctx, evm, is_simulate, is_estimate);
    if (!(success && !evm->evm_done)) return success;
  }

  CHECK_JSON_VERIFY(ctx->args, "[{to:address,data:bytes,gas?:hexuint,value?:hexuint,gasPrice?:hexuint,from?:address},block,{*:{balance?:hexuint,code?:bytes,state?:{*:bytes32},stateDiff?:{*:bytes32}}}]", "Invalid transaction");

  if (has_proof && !verify_call_freshness(ctx, ctx)) return false;

  if (!evm->accounts && has_proof) {
    ssz_ob_t accounts = ssz_get(&ctx->proof, "accounts");
    if (!c4_eth_verify_accounts(ctx, accounts, evm->state_root)) return false;
    if (c4_verify_block(ctx, ssz_get(&ctx->proof, "block"), &evm->el_header, evm->el_block_hash) != C4_SUCCESS) return false;
    evm->accounts = call_accounts_from_ssz(accounts);
  }
  if (!prepare_evm_call(ctx, evm, has_overrides)) return false;

  c4_status_t call_status = run_evm_call(ctx, evm, is_simulate);
  if (call_status != C4_SUCCESS || c4_state_get_pending_request(&ctx->state)) return false;

  evm->evm_done = true;
  return verify_call_result_and_finish(ctx, evm, is_simulate, is_estimate);
}
