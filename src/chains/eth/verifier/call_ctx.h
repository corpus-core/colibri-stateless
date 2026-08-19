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

#ifndef CALL_CTX_H
#define CALL_CTX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "eth_call_account.h"
#include "json.h"
#include "ssz.h"
#include "state.h"
#include "verify.h"

#ifdef EVMONE
#include "evmone_c_wrapper.h"
#endif

// :: Keccak preimage (captured via hook during simulation)

typedef struct keccak_entry {
  bytes32_t            hash;
  bytes_t              input;
  struct keccak_entry* next;
} keccak_entry_t;

// :: Trace call kind (mirrors evmone call kinds + STATICCALL)

typedef enum {
  TRACE_CALL         = 0,
  TRACE_DELEGATECALL = 1,
  TRACE_CALLCODE     = 2,
  TRACE_CREATE       = 3,
  TRACE_CREATE2      = 4,
  TRACE_STATICCALL   = 5
} trace_call_kind_t;

// :: Execution trace entry (captured during simulation)

typedef struct trace_entry {
  uint8_t             type; // EVMONE_CALL, EVMONE_DELEGATECALL, etc.
  address_t           from;
  address_t           to;
  uint64_t            gas;
  uint64_t            gas_used;
  bytes_t             input;
  bytes_t             output;
  bytes32_t           value;
  uint32_t            subtraces;
  uint32_t*           trace_address;
  uint32_t            trace_depth;
  struct trace_entry* next;
} trace_entry_t;

// :: Emitted log (captured during EVM execution for simulation)

typedef struct emitted_log {
  address_t           address;
  bytes_t             data;
  bytes32_t*          topics;
  size_t              topics_count;
  struct emitted_log* next;
} emitted_log_t;

/**
 * Shared context for EVM call verification (`eth_call`, `eth_estimateGas`, `colibri_simulateTransaction`).
 *
 * Holds all inputs, intermediate state, and outputs for the EVM execution.
 * In PAP mode this struct is heap-allocated and attached to `verify_ctx_t.user_data`
 * so it survives across multiple `C4_PENDING` rounds. For non-PAP paths (e.g. OP-Stack)
 * it may be stack-allocated with a single-pass lifetime.
 */
typedef struct evm_call_ctx {
  call_account_t* accounts;
  bytes_t         call_result;
  emitted_log_t*  logs;
  keccak_entry_t* keccak_entries;
  trace_entry_t*  traces;
  uint64_t        gas_used;
  bytes32_t       state_root;
  bool            pap_mode;
  bool            evm_done;
  bool            reverted; // set to true when the EVM execution reverted; `call_result` then holds the revert data
} evm_call_ctx_t;

/**
 * EVM execution context passed as host context to evmone (or a future light-EVM).
 *
 * For child calls a shallow copy is made with `parent` pointing to the caller's
 * context. `context_apply()` merges a successful child's state back into the parent.
 */
/**
 * Transient storage slot (EIP-1153). Per-transaction, cleared after tx ends.
 * Linked list keyed by (address, key).
 */
typedef struct transient_slot {
  address_t              address;
  bytes32_t              key;
  bytes32_t              value;
  struct transient_slot* next;
} transient_slot_t;

typedef struct evmone_context {
  void*                  executor;
  verify_ctx_t*          ctx;
  call_account_t*        accounts;
  uint64_t               block_number;
  bytes32_t              block_hash;
  uint64_t               timestamp;
  address_t              tx_origin;
  address_t              block_coinbase;
  bytes32_t              block_prev_randao;
  bytes32_t              block_base_fee;
  bytes32_t              blob_base_fee;
  uint64_t               gas_price;
  uint64_t               chain_id;
  uint64_t               block_gas_limit;
  struct evmone_context* parent;
  void*                  results;
  emitted_log_t*         logs;
  trace_entry_t*         traces;
  transient_slot_t*      transient_storage; // EIP-1153: only at root context
  uint32_t               subtrace_count;
  uint32_t               trace_depth;
  uint32_t*              trace_address;
  bool                   capture_events;
  bool                   pap_mode;
  bool                   storage_miss;
} evmone_context_t;

/** Block context extracted from call proof when state_proof.block is the blockContext union variant (selector 3). */
typedef struct eth_call_block_context {
  uint64_t  block_number;
  uint64_t  timestamp;
  address_t coinbase;
  bytes32_t prev_randao;
  bytes32_t base_fee_per_gas;
  bytes32_t block_hash;
  uint64_t  gas_limit;
  uint64_t  excess_blob_gas;
} eth_call_block_context_t;

/**
 * Extracts the block context from a call/estimate/simulate proof.
 *
 * Handles both hybrid call proofs (`header_data`) and standard proofs
 * (`state_proof.block` blockContext union variant). Returns `false` when
 * no block context is available (e.g. PAP-only proof or legacy format),
 * in which case `out` is left untouched.
 *
 * @param ctx verification context (must have `proof` set)
 * @param out destination struct (zeroed before this call by the caller)
 * @return `true` if `out` was populated, `false` otherwise
 */
bool eth_get_call_block_context_from_proof(verify_ctx_t* ctx, eth_call_block_context_t* out);

// :: EVM call context lifecycle

void evm_call_ctx_free(evm_call_ctx_t* evm);

// :: Account lookup helpers (traverse parent chain)

call_account_t* call_account_find(evmone_context_t* ctx, const address_t address);
call_account_t* call_account_get_or_create(evmone_context_t* ctx, const address_t address);

// :: PAP-mode lazy fetchers

void    call_account_lazy_fetch_storage(evmone_context_t* ctx, const address_t address, const bytes32_t key, bytes32_t result);
bytes_t call_account_get_code(evmone_context_t* ctx, const address_t address);

// :: Block hash lookup

/**
 * Fetches the block hash for a given block number.
 *
 * Returns `C4_SUCCESS` if the hash was found and copied into `result`.
 * Returns `C4_PENDING` if a request was sent but not yet answered --
 * the caller should set `ctx->storage_miss = true` and treat the current
 * EVM run as aborted (same pattern as lazy storage fetching).
 * Returns `C4_ERROR` on failure (error stored in `ctx->ctx->state`).
 *
 * @param ctx    EVM execution context
 * @param number block number to look up
 * @param result 32-byte buffer for the block hash
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
c4_status_t call_fetch_block_hash(evmone_context_t* ctx, int64_t number, bytes32_t result);

// :: State overrides

c4_status_t call_apply_state_overrides(verify_ctx_t* ctx, call_account_t** accounts, json_t overrides_json);

// :: Emitted log helpers

void free_keccak_entries(keccak_entry_t* entries);
void free_emitted_logs(emitted_log_t* logs);

// :: Trace helpers

void           free_trace_entries(trace_entry_t* entries);
emitted_log_t* add_emitted_log(emitted_log_t** logs, const address_t addr, const uint8_t* data, size_t data_size, const bytes32_t* topics, size_t topics_count);

// :: Child-context management

void context_free(evmone_context_t* ctx);
void context_apply(evmone_context_t* ctx);

// :: Context initialization

/**
 * Initializes an `evmone_context_t` with default values and block context
 * extracted from the verification context.
 *
 * Populates `block_number`, `timestamp`, `block_coinbase`, `block_prev_randao`,
 * `block_base_fee`, `blob_base_fee`, and `block_gas_limit` from the call proof's
 * state_proof (when using `ETH_CALL_STATE_PROOF`) or leaves them at zero/defaults
 * for PAP mode.
 *
 * @param out      context to initialize (zeroed by caller)
 * @param ctx      verification context
 * @param evm      call context with accounts and mode
 * @param executor evmone executor instance
 * @param capture_events whether to capture emitted logs
 */
void init_evmone_context(evmone_context_t* out, verify_ctx_t* ctx, evm_call_ctx_t* evm, void* executor, bool capture_events);

// :: Shared builders

ssz_ob_t eth_build_simulation_result_ssz(bytes_t call_result, emitted_log_t* logs, bool success, uint64_t gas_used, ssz_ob_t* execution_payload, call_account_t* accounts, keccak_entry_t* keccak_entries, trace_entry_t* traces);

/**
 * Runs an EVM call with optional event capture and gas metering.
 *
 * Reads `evm->accounts` as input (overrides must already be applied).
 * Writes results to `evm->call_result`, `evm->logs` (when `capture_events`),
 * and `evm->gas_used`. The transaction is read from `ctx->args[0]`.
 *
 * @param ctx the verification context
 * @param evm call context with inputs populated, outputs written on return
 * @param capture_events whether to capture emitted events
 * @return C4_SUCCESS, C4_ERROR, or C4_PENDING
 */
c4_status_t eth_run_call_evmone_with_events(verify_ctx_t* ctx, evm_call_ctx_t* evm, bool capture_events);

#ifdef __cplusplus
}
#endif

#endif /* CALL_CTX_H */
