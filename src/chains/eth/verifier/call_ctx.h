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

// Optional `colibri_simulateTransaction` config (4th params object). Both off unless set.
#define EVM_SIM_POSITIONS    (1u << 0) // record each executed JUMPDEST program counter once
#define EVM_SIM_STATE_VALUES (1u << 1) // include proven pre-state values on the access list

// JUMPDEST program counters are bounded by the largest code evmone will execute.
#define EVM_JUMPDEST_PC_LIMIT 65536u

/**
 * Unique JUMPDEST program counters executed by one code address.
 *
 * `bits` is a bitmap of `EVM_JUMPDEST_PC_LIMIT` bits (bit `pc` set once the
 * destination has run). `count` is the number of set bits.
 */
typedef struct jumpdest_set {
  address_t            address; // code address (delegatecall: the implementation)
  uint8_t*             bits;
  uint32_t             count;
  struct jumpdest_set* next;
} jumpdest_set_t;

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
 *
 * `sim_flags` is read from the optional 4th argument of `colibri_simulateTransaction`
 * (`positions`, `state_values`). `positions` holds the unique JUMPDEST program
 * counters captured while `EVM_SIM_POSITIONS` is set.
 */
typedef struct evm_call_ctx {
  call_account_t* accounts;
  bytes_t         call_result;
  emitted_log_t*  logs;
  keccak_entry_t* keccak_entries;
  trace_entry_t*  traces;
  jumpdest_set_t* positions;
  uint64_t        gas_used;
  bytes32_t       state_root;
  uint32_t        sim_flags;
  bool            pap_mode;
  bool            evm_done;
  bool            reverted;  // set to true when the EVM execution reverted; `call_result` then holds the revert data
  bytes_t         el_header; // header of the execution payload
  bytes32_t       el_block_hash;
  bytes_t         block_header;   // verified RLP header the EVM block context is taken from (borrowed, lives as long as the verify_ctx; empty = not yet known)
  bool            block_ctx_used; // the last EVM run read the tx/block context (set by host_get_tx_context)
} evm_call_ctx_t;

/**
 * JSON schema for `colibri_simulateTransaction` params.
 *
 * ```
 * [tx, block, overrides | null, { positions?: bool, state_values?: bool } | null]
 * ```
 *
 * The 3rd element is the state-override object (or `null`). The 4th element is
 * optional and selects extra simulation output.
 */
#define C4_SIMULATE_TX_PARAMS                                                             \
  "[{to:address,data:bytes,gas?:hexuint,value?:hexuint,gasPrice?:hexuint,from?:address}," \
  "block,"                                                                                \
  "null|{*:{balance?:hexuint,code?:bytes,state?:{*:bytes32},stateDiff?:{*:bytes32}}},"    \
  "null|{positions?:bool,state_values?:bool}]"

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
  bool                   has_block_context;      // block fields were populated from a block header
  bool                   block_header_requested; // PAP: the header request was already emitted in this run
  evm_call_ctx_t*        evm;                    // owning call context (root only)
} evmone_context_t;

/** Block context extracted from the verified RLP execution header of a call proof. */
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
 * Handles both a previously verified `el_header` on the call context and a
 * `elProof` field of `ETH_EL_PROOF_UNION` (verified via `c4_verify_block`).
 * Returns `false` when no block context is available (e.g. PAP-only proof),
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

/**
 * PAP mode: lazily fetches and verifies the block header used as EVM block context.
 *
 * Called when the EVM reads the tx/block context and no header is known yet.
 * Requests an `eth_getBlockHeader(<block tag of the call>)` proof from the
 * prover (the host may serve it as a CDN-cacheable GET) and verifies it via
 * `c4_verify_block`, including the `latest` freshness and the block number of a
 * concrete block tag. On success `evm->block_header` points to the verified
 * header (borrowed, valid for the lifetime of the verify_ctx) and the block
 * fields of `root` are populated. While the proof or its sync data is pending
 * `root->storage_miss` is set, so the current EVM run is aborted and repeated
 * once the response is available (same pattern as lazy storage fetching).
 *
 * @param root root EVM execution context (must have `evm` set)
 */
void call_lazy_fetch_block_header(evmone_context_t* root);

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

/**
 * Releases a list of JUMPDEST sets, including each bitmap.
 *
 * @param sets list head, or `NULL`
 */
void free_jumpdest_sets(jumpdest_set_t* sets);

/**
 * Reads the optional 4th `colibri_simulateTransaction` argument.
 *
 * A missing argument, `null`, or a boolean `false` leaves the corresponding
 * flag clear. Only JSON booleans `true` set `EVM_SIM_POSITIONS` or
 * `EVM_SIM_STATE_VALUES`.
 *
 * @param args RPC params array
 * @return combination of `EVM_SIM_POSITIONS` and `EVM_SIM_STATE_VALUES`
 */
uint32_t c4_eth_sim_flags_from_args(json_t args);
void     free_emitted_logs(emitted_log_t* logs);

// :: Trace helpers

void           free_trace_entries(trace_entry_t* entries);
emitted_log_t* add_emitted_log(emitted_log_t** logs, const address_t addr, const uint8_t* data, size_t data_size, const bytes32_t* topics, size_t topics_count);

/**
 * EIP-7708: emits a `Transfer(address,address,uint256)` protocol log from
 * `SYSTEM_ADDRESS` (`0xfffffffffffffffffffffffffffffffffffffffe`).
 *
 * Skipped when the amount is zero or when `from == to` (per EIP-7708).
 * Otherwise the log is prepended to `*logs` in the same LIFO shape used by
 * `add_emitted_log`, so the caller can reverse the list once at the end to
 * obtain chronological output.
 *
 * @param logs  target list head (may be `NULL`); the log is prepended on emit
 * @param from  20-byte sender address (transaction origin, CALL sender,
 *              CREATE creator, or SELFDESTRUCT contract)
 * @param to    20-byte recipient address
 * @param value 32-byte big-endian uint256 amount in Wei
 */
void emit_eth_transfer_log(emitted_log_t** logs, const address_t from, const address_t to, const uint8_t value[32]);

// :: Child-context management

void context_free(evmone_context_t* ctx);
void context_apply(evmone_context_t* ctx);

/**
 * Keeps the storage reads of a reverted or failed child frame.
 *
 * The child's writes and its EIP-2929 warm set are discarded, but every slot the
 * child read still influenced the execution. Each such slot is recorded with its
 * pre-state value as `accessed` (not `warm`) on the nearest ancestor holding the
 * account, so it appears in the simulation access list and is verified by
 * `colibri_proofCall` in PAP mode. If no ancestor holds the account, the slot is
 * recorded on a new account entry of the root context, so the read is never dropped.
 *
 * @param ctx child context whose frame did not succeed; `ctx->parent` must be set
 */
void context_keep_reads(evmone_context_t* ctx);

// :: Context initialization

/**
 * Initializes an `evmone_context_t` with default values and block context
 * extracted from the verification context.
 *
 * Populates `block_number`, `timestamp`, `block_coinbase`, `block_prev_randao`,
 * `block_base_fee`, `blob_base_fee`, and `block_gas_limit` from the verified
 * RLP execution header, or leaves them at zero/defaults for PAP mode.
 * The verified header is referenced by `evm->block_header`; if it is already
 * set (e.g. fetched lazily in PAP mode) it takes precedence. In PAP mode a
 * missing header is fetched on first use via `call_lazy_fetch_block_header`.
 *
 * @param out      context to initialize (zeroed by caller)
 * @param ctx      verification context
 * @param evm      call context with accounts and mode
 * @param executor evmone executor instance
 * @param capture_events whether to capture emitted logs
 */
void init_evmone_context(evmone_context_t* out, verify_ctx_t* ctx, evm_call_ctx_t* evm, void* executor, bool capture_events);

// :: Shared builders

/**
 * Builds the `colibri_simulateTransaction` result.
 *
 * `storage` on each access-list entry is included only when `sim_flags`
 * contains `EVM_SIM_STATE_VALUES`. `positions` is included only when
 * `sim_flags` contains `EVM_SIM_POSITIONS`.
 *
 * @param call_result return data of the call
 * @param logs emitted logs in chronological order
 * @param success `true` when the call did not revert
 * @param gas_used gas consumed by the call
 * @param execution_payload verified execution payload, or `NULL`
 * @param accounts accounts touched by the call
 * @param keccak_entries keccak preimages captured during the call
 * @param traces call trace, or `NULL`
 * @param sim_flags `EVM_SIM_POSITIONS` and `EVM_SIM_STATE_VALUES`
 * @param positions unique JUMPDEST program counters, or `NULL`
 * @return SSZ simulation result (caller frees `bytes.data`)
 */
ssz_ob_t eth_build_simulation_result_ssz(bytes_t call_result, emitted_log_t* logs, bool success, uint64_t gas_used, ssz_ob_t* execution_payload, call_account_t* accounts, keccak_entry_t* keccak_entries, trace_entry_t* traces, uint32_t sim_flags, jumpdest_set_t* positions);

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
