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

#include "bytes.h"
#include "call_ctx.h"
#include "crypto.h"
#include "eth_verify.h"
#include "evmone_c_wrapper.h"
#include "json.h"
#include "patricia.h"
#include "precompiles.h"
#include "rlp.h"
#include "ssz.h"
#include "state_overrides.h"
#include "sync_committee.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EVM_DEBUG 0

#if EVM_DEBUG
#define EVM_LOG(format, ...) fbprintf(stderr, "[EVM] " format "\n", ##__VA_ARGS__)
static void debug_print_address(const char* prefix, const evmc_address* addr) {
  fbprintf(stderr, "[EVM] %s: 0x%x\n", prefix, bytes(addr->bytes, 20));
}
static void debug_print_bytes32(const char* prefix, const evmc_bytes32* data) {
  fbprintf(stderr, "[EVM] %s: 0x%x\n", prefix, bytes(data->bytes, 32));
}
#else
#define EVM_LOG(...)                      (void) 0
#define debug_print_address(prefix, addr) (void) 0
#define debug_print_bytes32(prefix, data) (void) 0
#endif

/* Execution uses EVMONE_REV_OSAKA from evmone_c_wrapper.h (mapped to EVMC_OSAKA). */

static const char* evmone_status_message(int code) {
  static const char* positive_msgs[] = {
      [0]  = "Success",
      [1]  = "Failure",
      [2]  = "Revert",
      [3]  = "Out of gas",
      [4]  = "Invalid instruction",
      [5]  = "Undefined instruction",
      [6]  = "Stack overflow",
      [7]  = "Stack underflow",
      [8]  = "Bad jump destination",
      [9]  = "Invalid memory access",
      [10] = "Call depth exceeded",
      [11] = "Static mode violation",
      [12] = "Precompile failure",
      [13] = "Contract validation failure",
      [14] = "Argument out of range",
      [15] = "WASM unreachable instruction",
      [16] = "WASM trap",
      [17] = "Insufficient balance",
  };
  if (code >= 0 && code <= 17) return positive_msgs[code];
  if (code == -1) return "Internal error";
  if (code == -2) return "Rejected";
  if (code == -3) return "Out of memory";
  return "Unknown error";
}

typedef enum {
  CALL_KIND_CALL         = 0,
  CALL_KIND_DELEGATECALL = 1,
  CALL_KIND_CALLCODE     = 2,
  CALL_KIND_CREATE       = 3,
  CALL_KIND_CREATE2      = 4
} evmone_call_kind;

typedef struct evm_res_ptr {
  struct evmone_result result;
  struct evm_res_ptr*  next;
} evm_res_ptr_t;

static const struct evmone_host_interface host_interface;

static void add_evm_result(evmone_context_t* ctx, struct evmone_result* result) {
  evm_res_ptr_t* new_result = safe_malloc(sizeof(evm_res_ptr_t));
  new_result->result        = *result;
  new_result->next          = ctx->results;
  ctx->results              = new_result;
}

static bool host_account_exists(void* context, const evmc_address* addr) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("account_exists for", addr);
  call_account_t* acc = call_account_find(ctx, addr->bytes);
  if (acc && !(acc->flags & ACCOUNT_DELETED)) {
    // EIP-161: an account exists iff nonce != 0 OR balance != 0 OR code != empty
    bool has_nonce   = (acc->flags & ACCOUNT_HAS_NONCE) && acc->nonce != 0;
    bool has_balance = (acc->flags & ACCOUNT_HAS_BALANCE) && !bytes_all_zero(bytes(acc->balance, 32));
    bool has_code    = (acc->flags & ACCOUNT_HAS_CODE) && acc->code.len > 0;
    bool exists      = has_nonce || has_balance || has_code;
    EVM_LOG("account_exists result: %s", exists ? "true" : "false");
    return exists;
  }
  EVM_LOG("account_exists result: false (not found)");
  return false;
}

static evmc_bytes32 host_get_storage(void* context, const evmc_address* addr, const evmc_bytes32* key) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("get_storage for account", addr);
  debug_print_bytes32("get_storage key", key);

  evmc_bytes32    result = {0};
  call_account_t* acc    = call_account_find(ctx, addr->bytes);
  if (acc) {
    call_storage_t* s = call_storage_find(acc, key->bytes);
    if (s) {
      memcpy(result.bytes, s->post_value, 32);
      s->accessed = true;
      debug_print_bytes32("get_storage result (found)", &result);
      return result;
    }
    if (acc->flags & ACCOUNT_FULL_STATE) {
      debug_print_bytes32("get_storage result (full_state override -> zero)", &result);
      return result;
    }
  }

  if (ctx->pap_mode) {
    call_account_lazy_fetch_storage(ctx, addr->bytes, key->bytes, result.bytes);
    debug_print_bytes32("get_storage result (pap lazy)", &result);
  }

  return result;
}

static evmone_storage_status host_set_storage(void* context, const evmc_address* addr, const evmc_bytes32* key, const evmc_bytes32* value) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("set_storage for account", addr);
  debug_print_bytes32("set_storage key", key);
  debug_print_bytes32("set_storage value", value);

  // c = current value, v = new value
  evmc_bytes32 current = host_get_storage(context, addr, key);
  if (memcmp(current.bytes, value->bytes, 32) == 0) {
    EVM_LOG("set_storage: ASSIGNED (unchanged)");
    return EVMONE_STORAGE_ASSIGNED;
  }

  call_account_t* acc = call_account_get_or_create(ctx, addr->bytes);
  call_storage_t* s   = call_storage_find(acc, key->bytes);
  bytes32_t       original; // o = committed value at start of transaction
  bool            is_clean;

  if (s) {
    memcpy(original, s->src_value, 32);
    is_clean = !s->modified; // clean means o == c
    memcpy(s->post_value, value->bytes, 32);
    s->modified = memcmp(s->src_value, s->post_value, 32) != 0;
  }
  else {
    memcpy(original, current.bytes, 32); // first time seeing this slot: o == c
    is_clean = true;
    s        = safe_calloc(1, sizeof(call_storage_t));
    memcpy(s->key, key->bytes, 32);
    memcpy(s->src_value, current.bytes, 32);
    memcpy(s->post_value, value->bytes, 32);
    s->modified  = true;
    s->accessed  = true;
    s->next      = acc->storage;
    acc->storage = s;
  }

  bool v_zero = bytes_all_zero(bytes(value->bytes, 32));
  bool o_zero = bytes_all_zero(bytes(original, 32));
  bool c_zero = bytes_all_zero(bytes(current.bytes, 32));
  bool v_eq_o = memcmp(value->bytes, original, 32) == 0;

  evmone_storage_status status;
  if (is_clean) {
    // o == c (clean slot)
    if (c_zero)
      status = EVMONE_STORAGE_ADDED; // 0 -> 0 -> Z
    else if (v_zero)
      status = EVMONE_STORAGE_DELETED; // X -> X -> 0
    else
      status = EVMONE_STORAGE_MODIFIED; // X -> X -> Z
  }
  else {
    // o != c (dirty slot)
    if (v_zero && o_zero)
      status = EVMONE_STORAGE_ADDED_DELETED; // 0 -> Y -> 0
    else if (v_zero && !o_zero)
      status = EVMONE_STORAGE_MODIFIED_DELETED; // X -> Y -> 0
    else if (!v_zero && c_zero && v_eq_o)
      status = EVMONE_STORAGE_DELETED_RESTORED; // X -> 0 -> X
    else if (!v_zero && c_zero)
      status = EVMONE_STORAGE_DELETED_ADDED; // X -> 0 -> Z
    else if (!v_zero && !c_zero && v_eq_o)
      status = EVMONE_STORAGE_MODIFIED_RESTORED; // X -> Y -> X
    else
      status = EVMONE_STORAGE_ASSIGNED; // catch-all dirty re-write
  }
  EVM_LOG("set_storage: status=%d (clean=%d o_zero=%d c_zero=%d v_zero=%d v_eq_o=%d)", status, is_clean, o_zero, c_zero, v_zero, v_eq_o);
  return status;
}

static evmc_bytes32 host_get_balance(void* context, const evmc_address* addr) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("get_balance for", addr);

  evmc_bytes32    result = {0};
  call_account_t* acc    = call_account_find(ctx, addr->bytes);
  if (acc && (acc->flags & ACCOUNT_HAS_BALANCE))
    memcpy(result.bytes, acc->balance, 32);

  debug_print_bytes32("get_balance result", &result);
  return result;
}

static uint64_t host_get_nonce(void* context, const evmc_address* addr) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("get_nonce for", addr);

  call_account_t* acc = call_account_find(ctx, addr->bytes);
  if (acc && (acc->flags & ACCOUNT_HAS_NONCE)) {
    EVM_LOG("get_nonce result: %l", (size_t) acc->nonce);
    return acc->nonce;
  }
  EVM_LOG("get_nonce result: 0 (missing)");
  return 0;
}

static size_t host_get_code_size(void* context, const evmc_address* addr) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("get_code_size for", addr);
  size_t size = call_account_get_code(ctx, addr->bytes).len;
  EVM_LOG("get_code_size result: %l bytes", size);
  return size;
}

static evmc_bytes32 host_get_code_hash(void* context, const evmc_address* addr) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("get_code_hash for", addr);
  evmc_bytes32    result = {0};
  call_account_t* acc    = call_account_find(ctx, addr->bytes);

  // EIP-1052: EXTCODEHASH returns 0 for non-existent accounts
  if (!acc || (acc->flags & ACCOUNT_DELETED)) {
    debug_print_bytes32("get_code_hash result (non-existent)", &result);
    return result;
  }

  if (acc->flags & ACCOUNT_HAS_CODE_HASH) {
    memcpy(result.bytes, acc->code_hash, 32);
  }
  else {
    bytes_t code = call_account_get_code(ctx, addr->bytes);
    keccak(code, result.bytes);
  }
  debug_print_bytes32("get_code_hash result", &result);
  return result;
}

static size_t host_copy_code(void* context, const evmc_address* addr, size_t code_offset, uint8_t* buffer_data, size_t buffer_size) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("copy_code for", addr);
  EVM_LOG("copy_code offset: %l, buffer size: %l", code_offset, buffer_size);

  bytes_t code = call_account_get_code(ctx, addr->bytes);
  if (code_offset >= code.len) return 0;
  size_t copy_size = code.len - code_offset;
  if (buffer_size < copy_size) copy_size = buffer_size;
  if (code.data) memcpy(buffer_data, code.data + code_offset, copy_size);

  EVM_LOG("copy_code result: copied %l bytes", copy_size);
  return copy_size;
}

static void host_selfdestruct(void* context, const evmc_address* addr, const evmc_address* beneficiary) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("selfdestruct account", addr);
  debug_print_address("selfdestruct beneficiary", beneficiary);

  call_account_t* acc = call_account_get_or_create(ctx, addr->bytes);

  // EIP-6780: transfer remaining balance to beneficiary, zero own balance.
  // Storage is NOT cleared post-Cancun (unless created in same tx, which we don't track).
  if (!bytes_all_zero(bytes(acc->balance, 32)) && memcmp(addr->bytes, beneficiary->bytes, 20) != 0) {
    call_account_t* ben   = call_account_get_or_create(ctx, beneficiary->bytes);
    uint16_t        carry = 0;
    for (int i = 31; i >= 0; i--) {
      carry += (uint16_t) ben->balance[i] + (uint16_t) acc->balance[i];
      ben->balance[i] = (uint8_t) carry;
      carry >>= 8;
    }
    ben->flags |= ACCOUNT_HAS_BALANCE;
    memset(acc->balance, 0, 32);
  }
  else if (memcmp(addr->bytes, beneficiary->bytes, 20) == 0) {
    // EIP-6780: self-destruct to self -- balance stays zero
    memset(acc->balance, 0, 32);
  }

  EVM_LOG("selfdestruct: balance transferred, account flagged");
}

static evmone_context_t* context_root(evmone_context_t* ctx) {
  while (ctx->parent) ctx = ctx->parent;
  return ctx;
}

static trace_entry_t* create_trace_entry(evmone_context_t* ctx, const struct evmone_message* msg) {
  trace_entry_t* entry = safe_calloc(1, sizeof(trace_entry_t));
  entry->type          = (msg->kind == CALL_KIND_CALL && msg->is_static) ? TRACE_STATICCALL : (uint8_t) msg->kind;
  entry->gas           = (uint64_t) msg->gas;
  memcpy(entry->from, msg->sender.bytes, 20);
  memcpy(entry->to, msg->destination.bytes, 20);
  memcpy(entry->value, msg->value.bytes, 32);
  if (msg->input_data && msg->input_size)
    entry->input = bytes_dup(bytes(msg->input_data, msg->input_size));

  entry->trace_depth   = ctx->trace_depth + 1;
  entry->trace_address = safe_malloc(entry->trace_depth * sizeof(uint32_t));
  if (ctx->trace_depth && ctx->trace_address)
    memcpy(entry->trace_address, ctx->trace_address, ctx->trace_depth * sizeof(uint32_t));
  entry->trace_address[ctx->trace_depth] = ctx->subtrace_count;
  ctx->subtrace_count++;

  evmone_context_t* root = context_root(ctx);
  entry->next            = root->traces;
  root->traces           = entry;
  return entry;
}

/**
 * Bumps the CREATE/CREATE2 sender nonce on the parent context.
 *
 * EVMC ABI 18: the VM reads the pre-bump nonce via get_nonce() to compute the
 * create address, then the host must increment before the child state checkpoint.
 * The bump is not reverted when creation fails (Yellow Paper / evmone Host::call).
 */
static void bump_creator_nonce(evmone_context_t* ctx, const address_t sender) {
  call_account_t* acc = call_account_get_or_create(ctx, sender);
  acc->nonce++;
  acc->flags |= ACCOUNT_HAS_NONCE;
}

static void host_call(void* context, const struct evmone_message* msg, const uint8_t* code, size_t code_size, struct evmone_result* result) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  EVM_LOG("========Executing child call...");
  debug_print_address("call from", &msg->sender);
  debug_print_address("call to", &msg->destination);
  debug_print_address("code from", &msg->code_address);
  EVM_LOG("call gas: %l, depth: %d, is_static: %s", (size_t) msg->gas, msg->depth, msg->is_static ? "true" : "false");

  if (eth_is_precompile_address(msg->code_address.bytes)) {
    buffer_t     output     = {0};
    uint64_t     gas_used   = 0;
    pre_result_t pre_result = eth_execute_precompile(msg->code_address.bytes, bytes(msg->input_data, msg->input_size), &output, &gas_used);
    result->output_data     = output.data.data;
    result->output_size     = output.data.len;
    result->gas_left        = msg->gas - gas_used;
    result->gas_refund      = 0;
    result->status_code     = pre_result;
    if (pre_result != PRE_SUCCESS) {
      EVM_LOG("Precompile failed with status code: %d", pre_result);
      result->gas_left = 0;
    }
    if (ctx->capture_events) {
      trace_entry_t* entry = create_trace_entry(ctx, msg);
      entry->gas_used      = (pre_result == PRE_SUCCESS) ? gas_used : (uint64_t) msg->gas;
      if (result->output_data && result->output_size)
        entry->output = bytes_dup(bytes(result->output_data, result->output_size));
    }
    add_evm_result(ctx, result);
    return;
  }

  const bool is_create = (msg->kind == CALL_KIND_CREATE || msg->kind == CALL_KIND_CREATE2);

  // Must run before the child context so a failed CREATE cannot roll the bump back.
  if (is_create) {
    bump_creator_nonce(ctx, msg->sender.bytes);
    EVM_LOG("CREATE/CREATE2: bumped creator nonce");
  }

  const uint8_t* execution_code      = code;
  size_t         execution_code_size = code_size;
  bytes_t        fetched_code        = {0};

  if (is_create) {
    // Initcode is passed as call input; the VM already put the create address in destination.
    execution_code      = msg->input_data;
    execution_code_size = msg->input_size;
  }
  else if (execution_code == NULL || execution_code_size == 0) {
    EVM_LOG("Code not provided, fetching from code_address");
    fetched_code        = call_account_get_code(ctx, msg->code_address.bytes);
    execution_code      = fetched_code.data;
    execution_code_size = fetched_code.len;

    // EIP-7702: resolve delegation indicator for nested calls
    if (execution_code_size == 23 && execution_code[0] == 0xef && execution_code[1] == 0x01 && execution_code[2] == 0x00) {
      fetched_code        = call_account_get_code(ctx, execution_code + 3);
      execution_code      = fetched_code.data;
      execution_code_size = fetched_code.len;
      EVM_LOG("EIP-7702: resolved delegation for nested call");
    }

    EVM_LOG("Fetched code size: %l bytes", execution_code_size);
  }

  EVM_LOG("call code size: %l bytes", execution_code_size);
  if (msg->input_data && msg->input_size > 0) {
    if (EVM_DEBUG) {
      size_t display_size = msg->input_size > 64 ? 64 : msg->input_size;
      fbprintf(stderr, "[EVM] call input data (%l bytes): 0x%x%s\n",
               (uint64_t) msg->input_size,
               bytes(msg->input_data, display_size),
               msg->input_size > 64 ? "..." : "");
    }
  }

  trace_entry_t* trace = ctx->capture_events ? create_trace_entry(ctx, msg) : NULL;

  evmone_context_t child  = *ctx;
  child.parent            = ctx;
  child.accounts          = NULL;
  child.logs              = NULL;
  child.traces            = NULL;
  child.transient_storage = NULL; // shared via root, not copied
  child.subtrace_count    = 0;
  if (trace) {
    child.trace_depth   = trace->trace_depth;
    child.trace_address = trace->trace_address; // borrowed, owned by root trace list
  }

  // Spurious Dragon+: newly created accounts start at nonce 1 (revertible with child).
  if (is_create) {
    call_account_t* created = call_account_get_or_create(&child, msg->destination.bytes);
    created->nonce          = 1;
    created->flags |= ACCOUNT_HAS_NONCE;
  }

  // During initcode execution CALLDATA must be empty (initcode is the code, not input).
  evmone_message exec_msg = *msg;
  if (is_create) {
    exec_msg.input_data = NULL;
    exec_msg.input_size = 0;
  }

  evmone_result exec_result = evmone_execute(
      ctx->executor,
      &host_interface,
      &child,
      EVMONE_REV_OSAKA,
      &exec_msg,
      execution_code,
      execution_code_size);

  EVM_LOG("Child call complete. Status: %d, Gas left: %l", exec_result.status_code, (size_t) exec_result.gas_left);

  if (trace) {
    trace->gas_used  = (uint64_t) (msg->gas - exec_result.gas_left);
    trace->subtraces = child.subtrace_count;
    if (exec_result.output_data && exec_result.output_size)
      trace->output = bytes_dup(bytes(exec_result.output_data, exec_result.output_size));
  }

  if (exec_result.output_data && exec_result.output_size > 0) {
    if (EVM_DEBUG) {
      size_t display_size = exec_result.output_size > 64 ? 64 : exec_result.output_size;
      fbprintf(stderr, "[EVM] Child call output (%l bytes): 0x%x%s\n",
               (uint64_t) exec_result.output_size,
               bytes(exec_result.output_data, display_size),
               exec_result.output_size > 64 ? "..." : "");
    }
  }
  add_evm_result(ctx, &exec_result);

  if (exec_result.status_code == 0)
    context_apply(&child);
  EVM_LOG("========/child call complete ====");

  context_free(&child);

  *result = exec_result;
}

static void host_get_tx_context(void* context, evmone_tx_context* result) {
  evmone_context_t* ctx  = (evmone_context_t*) context;
  evmone_context_t* root = context_root(ctx);
  //  evm_call_ctx_t*   call_ctx = (evm_call_ctx_t*) ctx->ctx->user_data;
  //  if (!call_ctx) {
  //    EVM_LOG("get_tx_context called but no call context found");
  //    return;
  //  }
  EVM_LOG("get_tx_context called");
  memset(result, 0, sizeof(evmone_tx_context));
  memcpy(result->tx_origin.bytes, root->tx_origin, 20);
  memcpy(result->block_coinbase.bytes, root->block_coinbase, 20);
  memcpy(result->block_prev_randao.bytes, root->block_prev_randao, 32);
  memcpy(result->block_base_fee.bytes, root->block_base_fee, 32);
  memcpy(result->blob_base_fee.bytes, root->blob_base_fee, 32);
  result->block_number      = (int64_t) root->block_number;
  result->block_timestamp   = (int64_t) root->timestamp;
  result->block_gas_limit   = (int64_t) root->block_gas_limit;
  result->blob_hashes       = NULL;
  result->blob_hashes_count = 0;
  // SLOTNUM (EIP-7843) stays zero until Amsterdam is activated explicitly.
  result->block_slot_number = 0;

  // gas_price as big-endian uint256
  uint64_t gp = root->gas_price;
  for (int i = 31; i >= 0 && gp; i--) {
    result->tx_gas_price.bytes[i] = (uint8_t) (gp & 0xFF);
    gp >>= 8;
  }
  // chain_id as big-endian uint256
  uint64_t cid = root->chain_id;
  for (int i = 31; i >= 0 && cid; i--) {
    result->chain_id.bytes[i] = (uint8_t) (cid & 0xFF);
    cid >>= 8;
  }
  EVM_LOG("tx_context: origin=0x%x, block_number=%l, timestamp=%l, chain_id=%l",
          bytes(result->tx_origin.bytes, 20), (uint64_t) result->block_number,
          (uint64_t) result->block_timestamp, root->chain_id);
}

static evmc_bytes32 host_get_block_hash(void* context, int64_t number) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  EVM_LOG("get_block_hash for block number: %l", (size_t) number);
  evmc_bytes32 result = {0};
  c4_status_t  status = call_fetch_block_hash(ctx, number, result.bytes);
  if (status == C4_PENDING) ctx->storage_miss = true;
  debug_print_bytes32("get_block_hash result", &result);
  return result;
}

static void host_emit_log(void* context, const evmc_address* addr, const uint8_t* data, size_t data_size, const evmc_bytes32 topics[], size_t topics_count) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("emit_log from", addr);
  EVM_LOG("emit_log: data size: %l bytes, topics count: %l", data_size, topics_count);

  if (ctx->capture_events)
    add_emitted_log(&ctx->logs, addr->bytes, data, data_size, (const bytes32_t*) topics, topics_count);

  if (data && data_size > 0 && EVM_DEBUG) {
    size_t display_size = data_size > 64 ? 64 : data_size;
    fbprintf(stderr, "[EVM] Log data (hex): 0x%x%s\n",
             bytes(data, display_size),
             data_size > 64 ? "..." : "");
  }

  for (size_t i = 0; i < topics_count && EVM_DEBUG; i++)
    debug_print_bytes32("Log topic", &topics[i]);
}

static int host_access_account(void* context, const evmc_address* addr) {
  evmone_context_t* ctx  = (evmone_context_t*) context;
  evmone_context_t* root = context_root(ctx);
  debug_print_address("access_account", addr);

  // precompiles 1-9 are always warm (EIP-2929); EIP-7951 adds P256VERIFY at 0x0000…0100
  if (bytes_all_zero(bytes(addr->bytes, 19)) && addr->bytes[19] >= 1 && addr->bytes[19] <= 9) {
    EVM_LOG("access_account: WARM (precompile)");
    return EVMONE_ACCESS_WARM;
  }
  if (bytes_all_zero(bytes(addr->bytes, 18)) && addr->bytes[18] == 0x01 && addr->bytes[19] == 0x00) {
    EVM_LOG("access_account: WARM (P256VERIFY)");
    return EVMONE_ACCESS_WARM;
  }

  call_account_t* acc = call_account_find(root, addr->bytes);
  if (acc && (acc->flags & ACCOUNT_ACCESSED)) {
    EVM_LOG("access_account: WARM");
    return EVMONE_ACCESS_WARM;
  }
  if (!acc && ctx->pap_mode) {
    acc = eth_call_account_cache_load(ctx->ctx, addr->bytes);
    if (acc) {
      acc->next      = root->accounts;
      root->accounts = acc;
    }
  }

  if (!acc) acc = call_account_list_get_or_create(&root->accounts, addr->bytes);
  acc->flags |= ACCOUNT_ACCESSED;
  EVM_LOG("access_account: COLD");
  return EVMONE_ACCESS_COLD;
}

static int host_access_storage(void* context, const evmc_address* addr, const evmc_bytes32* key) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("access_storage account", addr);
  debug_print_bytes32("access_storage key", key);

  call_account_t* acc = call_account_find(ctx, addr->bytes);
  if (acc) {
    call_storage_t* s = call_storage_find(acc, key->bytes);
    if (s) {
      if (s->accessed) {
        EVM_LOG("access_storage: WARM");
        return EVMONE_ACCESS_WARM;
      }
      s->accessed = true;
      EVM_LOG("access_storage: COLD (marked)");
      return EVMONE_ACCESS_COLD;
    }
  }

  // slot not found yet; subsequent get_storage/set_storage will create it with accessed=true
  EVM_LOG("access_storage: COLD");
  return EVMONE_ACCESS_COLD;
}

// EIP-1153: transient storage lives on the root context (per-transaction scope)
static evmc_bytes32 host_get_transient_storage(void* context, const evmc_address* addr, const evmc_bytes32* key) {
  evmone_context_t* root = context_root((evmone_context_t*) context);
  debug_print_address("get_transient_storage for", addr);
  debug_print_bytes32("get_transient_storage key", key);
  evmc_bytes32 result = {0};
  for (transient_slot_t* s = root->transient_storage; s; s = s->next) {
    if (memcmp(s->address, addr->bytes, 20) == 0 && memcmp(s->key, key->bytes, 32) == 0) {
      memcpy(result.bytes, s->value, 32);
      break;
    }
  }
  debug_print_bytes32("get_transient_storage result", &result);
  return result;
}

static void host_set_transient_storage(void* context, const evmc_address* addr, const evmc_bytes32* key, const evmc_bytes32* value) {
  evmone_context_t* root = context_root((evmone_context_t*) context);
  debug_print_address("set_transient_storage for", addr);
  debug_print_bytes32("set_transient_storage key", key);
  debug_print_bytes32("set_transient_storage value", value);
  for (transient_slot_t* s = root->transient_storage; s; s = s->next) {
    if (memcmp(s->address, addr->bytes, 20) == 0 && memcmp(s->key, key->bytes, 32) == 0) {
      memcpy(s->value, value->bytes, 32);
      return;
    }
  }
  transient_slot_t* slot = safe_calloc(1, sizeof(transient_slot_t));
  memcpy(slot->address, addr->bytes, 20);
  memcpy(slot->key, key->bytes, 32);
  memcpy(slot->value, value->bytes, 32);
  slot->next              = root->transient_storage;
  root->transient_storage = slot;
}

static const struct evmone_host_interface host_interface = {
    .account_exists        = host_account_exists,
    .get_storage           = host_get_storage,
    .set_storage           = host_set_storage,
    .get_balance           = host_get_balance,
    .get_nonce             = host_get_nonce,
    .get_code_size         = host_get_code_size,
    .get_code_hash         = host_get_code_hash,
    .copy_code             = host_copy_code,
    .selfdestruct          = host_selfdestruct,
    .call                  = host_call,
    .get_tx_context        = host_get_tx_context,
    .get_block_hash        = host_get_block_hash,
    .emit_log              = host_emit_log,
    .access_account        = host_access_account,
    .access_storage        = host_access_storage,
    .get_transient_storage = host_get_transient_storage,
    .set_transient_storage = host_set_transient_storage,
};

/**
 * Initialize an evmone_message from JSON transaction data.
 *
 * @param message pointer to the message to initialize
 * @param tx      JSON transaction object
 * @param buffer  buffer to use for string operations
 */
static void set_message(evmone_message* message, json_t tx, buffer_t* buffer) {
  memset(message, 0, sizeof(*message));

  bytes_t to = json_get_bytes(tx, "to", buffer);
  if (to.len == 20) {
    memcpy(message->destination.bytes, to.data, 20);
    memcpy(message->code_address.bytes, to.data, 20);
  }

  bytes_t from = json_get_bytes(tx, "from", buffer);
  if (from.len == 20) memcpy(message->sender.bytes, from.data, 20);

  message->gas = json_get_uint64(tx, "gas");
  if (message->gas == 0) message->gas = 10000000;

  bytes_t value = json_get_bytes(tx, "value", buffer);
  if (value.len && value.len <= 32) memcpy(message->value.bytes + 32 - value.len, value.data, value.len);

  bytes_t input = json_get_bytes(tx, "data", buffer);
  if (!input.len) input = json_get_bytes(tx, "input", buffer);
  message->input_data = input.data;
  message->input_size = input.len;

  EVM_LOG("Message initialized:");
  EVM_LOG("  kind: %d", message->kind);
  EVM_LOG("  is_static: %s", message->is_static ? "true" : "false");
  EVM_LOG("  gas: %l", message->gas);
  debug_print_address("  destination", &message->destination);
  debug_print_address("  sender", &message->sender);
  debug_print_address("  code_address", &message->code_address);
  EVM_LOG("  input_size: %l bytes", (uint64_t) message->input_size);
  if (message->input_data && message->input_size > 0 && EVM_DEBUG) {
    size_t display_size = message->input_size > 64 ? 64 : message->input_size;
    fbprintf(stderr, "[EVM] input data: 0x%x%s\n",
             bytes(message->input_data, display_size),
             message->input_size > 64 ? "..." : "");
  }
  debug_print_bytes32("  value", &message->value);
}

static void keccak_hook_cb(void* context, const uint8_t* data, size_t size, const uint8_t* hash) {
  keccak_entry_t** head  = (keccak_entry_t**) context;
  keccak_entry_t*  entry = safe_calloc(1, sizeof(keccak_entry_t));
  memcpy(entry->hash, hash, 32);
  entry->input = bytes_dup(bytes(data, size));
  entry->next  = *head;
  *head        = entry;
}

INTERNAL c4_status_t eth_run_call_evmone_with_events(verify_ctx_t* ctx, evm_call_ctx_t* evm, bool capture_events) {
  buffer_t       buffer  = {0};
  address_t      to      = {0};
  buffer_t       to_buf  = stack_buffer(to);
  evmone_message message = {0};
  json_t         tx      = json_at(ctx->args, 0);

  json_t tx_input = json_get(tx, "data");
  if (tx_input.type == JSON_TYPE_NOT_FOUND) tx_input = json_get(tx, "input");
  if ((tx_input.type != JSON_TYPE_STRING || tx_input.len < 5) && !evm->accounts && !evm->pap_mode) return C4_SUCCESS;
  if (json_get_bytes(tx, "to", &to_buf).len != 20) THROW_ERROR("Invalid transaction: to address is not 20 bytes");

  // reset the call result, logs, and traces
  if (evm->call_result.data) safe_free(evm->call_result.data);
  call_account_reset_accessed(evm->accounts);
  evm->call_result = NULL_BYTES;
  free_emitted_logs(evm->logs);
  evm->logs = NULL;
  free_trace_entries(evm->traces);
  evm->traces   = NULL;
  evm->gas_used = 0;
  evm->evm_done = false;
  evm->reverted = false; // defense-in-depth: prevent stale revert flag from leaking into early-return paths

  set_message(&message, tx, &buffer);

  // special handling for precompiles
  if (eth_is_precompile_address(to)) {
    buffer_t     output         = {0};
    uint64_t     precompile_gas = 0;
    pre_result_t pre_result     = eth_execute_precompile(to, bytes(message.input_data, message.input_size), &output, &precompile_gas);
    buffer_free(&buffer);
    evm->call_result = output.data;
    switch (pre_result) {
      case PRE_SUCCESS:
        return C4_SUCCESS;
      case PRE_ERROR:
        c4_state_add_error(&ctx->state, "Precompile error");
        return C4_ERROR;
      case PRE_OUT_OF_BOUNDS:
        c4_state_add_error(&ctx->state, "Precompile out of bounds");
        return C4_ERROR;
      case PRE_INVALID_INPUT:
        c4_state_add_error(&ctx->state, "Precompile Invalid Input");
        return C4_ERROR;
      default:
        c4_state_add_error(&ctx->state, "Precompile unknown error");
        return C4_ERROR;
    }
  }

  EVM_LOG("Creating EVM executor...");
  void* executor = evmone_create_executor();
  if (!executor) THROW_ERROR("Error: Failed to create executor");

  evmone_context_t context;
  init_evmone_context(&context, ctx, evm, executor, capture_events);

  // populate tx_origin from the transaction's "from" field
  memcpy(context.tx_origin, message.sender.bytes, 20);

  // read gas_price from the transaction if provided
  context.gas_price = json_get_uint64(json_at(ctx->args, 0), "gasPrice");
  bytes_t code      = call_account_get_code(&context, to);

  // EIP-2929: pre-warm sender and destination
  {
    call_account_t* sender_acc = call_account_list_get_or_create(&context.accounts, message.sender.bytes);
    sender_acc->flags |= ACCOUNT_ACCESSED;
    call_account_t* dest_acc = call_account_list_get_or_create(&context.accounts, to);
    dest_acc->flags |= ACCOUNT_ACCESSED;
  }

  // EIP-7702: resolve delegation indicator for top-level call
  if (code.len == 23 && code.data[0] == 0xef && code.data[1] == 0x01 && code.data[2] == 0x00) {
    memcpy(message.code_address.bytes, code.data + 3, 20);
    code = call_account_get_code(&context, message.code_address.bytes);
    EVM_LOG("EIP-7702: resolved delegation to code_address");
    debug_print_address("  delegated code_address", &message.code_address);
  }

  if (code.len < 500)
    EVM_LOG("Contract code : 0x%x", code);
  else
    EVM_LOG("Contract code : %d bytes", (uint32_t) code.len);

  // Apply top-level value transfer: credit msg.value to destination account
  if (!bytes_all_zero(bytes(message.value.bytes, 32))) {
    call_account_t* dest_acc = call_account_find(&context, to);
    if (dest_acc) {
      uint16_t carry = 0;
      for (int i = 31; i >= 0; i--) {
        carry += (uint16_t) dest_acc->balance[i] + (uint16_t) message.value.bytes[i];
        dest_acc->balance[i] = (uint8_t) carry;
        carry >>= 8;
      }
      dest_acc->flags |= ACCOUNT_HAS_BALANCE;
    }
  }

  if (capture_events) {
    free_keccak_entries(evm->keccak_entries);
    evm->keccak_entries = NULL;
    evmone_set_keccak_hook(keccak_hook_cb, &evm->keccak_entries);

    // top-level trace entry with traceAddress = []
    trace_entry_t* root_trace = safe_calloc(1, sizeof(trace_entry_t));
    root_trace->type          = (uint8_t) message.kind;
    root_trace->gas           = (uint64_t) message.gas;
    memcpy(root_trace->from, message.sender.bytes, 20);
    memcpy(root_trace->to, message.destination.bytes, 20);
    memcpy(root_trace->value, message.value.bytes, 32);
    if (message.input_data && message.input_size)
      root_trace->input = bytes_dup(bytes(message.input_data, message.input_size));
    context.traces = root_trace;
  }

  evmone_result result = evmone_execute(
      executor,
      &host_interface,
      &context,
      EVMONE_REV_OSAKA,
      &message,
      code.data,
      code.len);

  if (capture_events)
    evmone_set_keccak_hook(NULL, NULL);

  EVM_LOG("Result status code: %d", result.status_code);
  EVM_LOG("Gas left: %l", (size_t) result.gas_left);
  EVM_LOG("Gas refund: %l", (size_t) result.gas_refund);

  {
    uint64_t raw_gas_used  = (uint64_t) (message.gas - result.gas_left);
    uint64_t refund_cap    = raw_gas_used / 5; // EIP-3529: refund capped at 1/5 of gas used
    uint64_t actual_refund = (uint64_t) result.gas_refund;
    if (actual_refund > refund_cap) actual_refund = refund_cap;
    evm->gas_used = raw_gas_used - actual_refund;
    EVM_LOG("Gas accounting: raw=%l, refund=%l, capped=%l, final=%l",
            (size_t) raw_gas_used, (size_t) result.gas_refund, (size_t) actual_refund, (size_t) evm->gas_used);
  }

  if (EVM_DEBUG && result.output_data && result.output_size > 0)
    print_hex(stderr, bytes(result.output_data, result.output_size), "[EVM] Output data: 0x", "\n");

  // On a deterministic EVM revert (status_code == 2) the output_data holds the
  // revert payload (e.g. ABI-encoded `Error(string)`, custom errors, or
  // `OffchainLookup(...)` for EIP-3668 / CCIP-Read). We preserve it on the
  // call context and signal the revert via `evm->reverted`, so verify_call.c
  // can surface it to the caller without treating the EVM run as a failure.
  evm->reverted = (result.status_code == 2);
  if (!ctx->state.error)
    evm->call_result = result.output_size ? bytes_dup(bytes(result.output_data, result.output_size)) : NULL_BYTES;
  else
    evm->call_result = NULL_BYTES;

  if (capture_events) {
    evm->logs    = context.logs;
    context.logs = NULL;

    // update the root trace entry with gas_used, output, subtraces
    if (context.traces) {
      context.traces->gas_used  = evm->gas_used;
      context.traces->subtraces = context.subtrace_count;
      if (result.output_data && result.output_size)
        context.traces->output = bytes_dup(bytes(result.output_data, result.output_size));
    }
    evm->traces    = context.traces;
    context.traces = NULL;
  }

  // The EVM may have created/modified accounts in context.accounts that
  // were originally pointing into evm->accounts. Propagate them back.
  evm->accounts    = context.accounts;
  context.accounts = NULL;

  // detect balance/nonce modifications for simulation stateChanges
  for (call_account_t* acc = evm->accounts; acc; acc = acc->next) {
    if (memcmp(acc->balance, acc->src_balance, 32) != 0)
      acc->flags |= ACCOUNT_BALANCE_MODIFIED;
    if (acc->nonce != acc->src_nonce)
      acc->flags |= ACCOUNT_NONCE_MODIFIED;
  }

  if (result.status_code == 0) {
    EVM_LOG("Call verification successful");
  }
  else if (result.status_code == 2) {
    // Reverts are an expected, valid execution outcome. They are NOT a
    // verification error: the EVM ran the bytecode to completion and chose
    // to revert. The caller decides how to surface this (see VERIFY_FLAG_REVERTED).
    EVM_LOG("Call reverted (revert data size=%l)", (size_t) (result.output_data ? result.output_size : 0));
  }
  else {
    const char* error_msg = evmone_status_message(result.status_code);
    EVM_LOG("Call verification failed: %s (code %d)", error_msg, result.status_code);
    if (!context.storage_miss) c4_state_add_error(&ctx->state, error_msg);
  }

  evmone_release_result(&result);
  evmone_destroy_executor(executor);
  buffer_free(&buffer);
  while (context.results) {
    evm_res_ptr_t* res  = (evm_res_ptr_t*) context.results;
    evm_res_ptr_t* next = res->next;
    evmone_release_result(&res->result);
    safe_free(res);
    context.results = next;
  }
  // context.accounts already transferred to evm->accounts above
  free_emitted_logs(context.logs);
  context.logs = NULL;
  context_free(&context);
  EVM_LOG("=== EVM call verification complete ===");

  if (context.storage_miss) {
    // if we have a pending request, the error is discarded since we could not stop the evm
    if (c4_state_get_pending_request(&ctx->state)) {
      if (ctx->state.error) {
        safe_free(ctx->state.error);
        ctx->state.error = NULL;
      }
      return C4_PENDING;
    }
    if (!ctx->state.error) c4_state_add_error(&ctx->state, "missing storage value but no pending request");
    return C4_ERROR;
  }

  return ctx->state.error == NULL ? C4_SUCCESS : C4_ERROR;
}
