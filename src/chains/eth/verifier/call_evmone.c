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
#include "eth_call_cache.h"
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

#define EVM_DEBUG      0
#define EVMC_REV_OSAKA 14
#define EVM_LOG(format, ...)                                              \
  do {                                                                    \
    if (EVM_DEBUG) fbprintf(stderr, "[EVM] " format "\n", ##__VA_ARGS__); \
  } while (0)

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

static void debug_print_address(const char* prefix, const evmc_address* addr) {
  if (!EVM_DEBUG) return;
  fbprintf(stderr, "[EVM] %s: 0x%x\n", prefix, bytes(addr->bytes, 20));
}

static void debug_print_bytes32(const char* prefix, const evmc_bytes32* data) {
  if (!EVM_DEBUG) return;
  fbprintf(stderr, "[EVM] %s: 0x%x\n", prefix, bytes(data->bytes, 32));
}

static bool host_account_exists(void* context, const evmc_address* addr) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("account_exists for", addr);
  call_account_t* acc = call_account_find(ctx, addr->bytes);
  if (acc) {
    bool exists = !(acc->flags & ACCOUNT_DELETED);
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

  evmc_bytes32 current = host_get_storage(context, addr, key);
  if (memcmp(current.bytes, value->bytes, 32) == 0) {
    EVM_LOG("set_storage: UNCHANGED");
    return EVMONE_STORAGE_UNCHANGED;
  }

  call_account_t* acc         = call_account_get_or_create(ctx, addr->bytes);
  call_storage_t* s           = call_storage_find(acc, key->bytes);
  bool            first_write = true;
  if (s) {
    first_write = !s->modified;
    memcpy(s->post_value, value->bytes, 32);
    s->modified = memcmp(s->src_value, s->post_value, 32) != 0;
  }
  else {
    s = safe_calloc(1, sizeof(call_storage_t));
    memcpy(s->key, key->bytes, 32);
    memcpy(s->src_value, current.bytes, 32);
    memcpy(s->post_value, value->bytes, 32);
    s->modified  = true;
    s->accessed  = true;
    s->next      = acc->storage;
    acc->storage = s;
  }

  if (bytes_all_zero(bytes(value->bytes, 32))) {
    EVM_LOG("set_storage: DELETED");
    return EVMONE_STORAGE_DELETED;
  }
  if (first_write && bytes_all_zero(bytes(current.bytes, 32))) {
    EVM_LOG("set_storage: ADDED");
    return EVMONE_STORAGE_ADDED;
  }
  if (!first_write) {
    EVM_LOG("set_storage: MODIFIED_AGAIN");
    return EVMONE_STORAGE_MODIFIED_AGAIN;
  }
  EVM_LOG("set_storage: MODIFIED");
  return EVMONE_STORAGE_MODIFIED;
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

static size_t host_get_code_size(void* context, const evmc_address* addr) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("get_code_size for", addr);
  size_t size = call_account_get_code(ctx, addr->bytes).len;
  EVM_LOG("get_code_size result: %zu bytes", size);
  return size;
}

static evmc_bytes32 host_get_code_hash(void* context, const evmc_address* addr) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("get_code_hash for", addr);
  evmc_bytes32 result = {0};
  keccak(call_account_get_code(ctx, addr->bytes), result.bytes);
  debug_print_bytes32("get_code_hash result", &result);
  return result;
}

static size_t host_copy_code(void* context, const evmc_address* addr, size_t code_offset, uint8_t* buffer_data, size_t buffer_size) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("copy_code for", addr);
  EVM_LOG("copy_code offset: %zu, buffer size: %zu", code_offset, buffer_size);

  bytes_t code      = call_account_get_code(ctx, addr->bytes);
  size_t  copy_size = code.len - code_offset;
  if (buffer_size < copy_size) copy_size = buffer_size;
  if (code.data) memcpy(buffer_data, code.data + code_offset, copy_size);

  EVM_LOG("copy_code result: copied %zu bytes", copy_size);
  return copy_size;
}

static void host_selfdestruct(void* context, const evmc_address* addr, const evmc_address* beneficiary) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("selfdestruct account", addr);
  debug_print_address("selfdestruct beneficiary", beneficiary);

  call_account_t* acc = call_account_get_or_create(ctx, addr->bytes);
  call_storage_free_list(acc->storage);
  acc->storage = NULL;
  acc->flags |= ACCOUNT_DELETED;

  EVM_LOG("selfdestruct: account marked as deleted");
}

static void host_call(void* context, const struct evmone_message* msg, const uint8_t* code, size_t code_size, struct evmone_result* result) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  EVM_LOG("========Executing child call...");
  debug_print_address("call from", &msg->sender);
  debug_print_address("call to", &msg->destination);
  debug_print_address("code from", &msg->code_address);
  EVM_LOG("call gas: %zu, depth: %d, is_static: %s", (size_t) msg->gas, msg->depth, msg->is_static ? "true" : "false");

  if (bytes_all_zero(bytes(msg->code_address.bytes, 19)) && msg->code_address.bytes[19]) {
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
    add_evm_result(ctx, result);
    return;
  }

  const uint8_t* execution_code      = code;
  size_t         execution_code_size = code_size;
  bytes_t        fetched_code        = {0};

  if ((execution_code == NULL || execution_code_size == 0) && msg->kind != CALL_KIND_CREATE && msg->kind != CALL_KIND_CREATE2) {
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

    EVM_LOG("Fetched code size: %zu bytes", execution_code_size);
  }

  EVM_LOG("call code size: %zu bytes", execution_code_size);
  if (msg->input_data && msg->input_size > 0) {
    if (EVM_DEBUG) {
      size_t display_size = msg->input_size > 64 ? 64 : msg->input_size;
      fbprintf(stderr, "[EVM] call input data (%l bytes): 0x%x%s\n",
               (uint64_t) msg->input_size,
               bytes(msg->input_data, display_size),
               msg->input_size > 64 ? "..." : "");
    }
  }

  evmone_context_t child = *ctx;
  child.parent           = ctx;
  child.accounts         = NULL;

  evmone_result exec_result = evmone_execute(
      ctx->executor,
      &host_interface,
      &child,
      EVMC_REV_OSAKA,
      msg,
      execution_code,
      execution_code_size);

  EVM_LOG("Child call complete. Status: %d, Gas left: %zu", exec_result.status_code, (size_t) exec_result.gas_left);
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

static evmc_bytes32 host_get_tx_context(void* context) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  EVM_LOG("get_tx_context called");
  evmc_bytes32 result = {0};
  debug_print_bytes32("get_tx_context result", &result);
  return result;
}

static evmc_bytes32 host_get_block_hash(void* context, int64_t number) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  EVM_LOG("get_block_hash for block number: %zu", (size_t) number);
  evmc_bytes32 result = {0};
  debug_print_bytes32("get_block_hash result", &result);
  return result;
}

static void host_emit_log(void* context, const evmc_address* addr, const uint8_t* data, size_t data_size, const evmc_bytes32 topics[], size_t topics_count) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("emit_log from", addr);
  EVM_LOG("emit_log: data size: %zu bytes, topics count: %zu", data_size, topics_count);

  if (ctx->capture_events)
    add_emitted_log(ctx, addr, data, data_size, topics, topics_count);

  if (data && data_size > 0 && EVM_DEBUG) {
    size_t display_size = data_size > 64 ? 64 : data_size;
    fbprintf(stderr, "[EVM] Log data (hex): 0x%x%s\n",
             bytes(data, display_size),
             data_size > 64 ? "..." : "");
  }

  for (size_t i = 0; i < topics_count && EVM_DEBUG; i++)
    debug_print_bytes32("Log topic", &topics[i]);
}

static void host_access_account(void* context, const evmc_address* addr) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("access_account", addr);
}

static void host_access_storage(void* context, const evmc_address* addr, const evmc_bytes32* key) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("access_storage account", addr);
  debug_print_bytes32("access_storage key", key);
}

static const struct evmone_host_interface host_interface = {
    .account_exists = host_account_exists,
    .get_storage    = host_get_storage,
    .set_storage    = host_set_storage,
    .get_balance    = host_get_balance,
    .get_code_size  = host_get_code_size,
    .get_code_hash  = host_get_code_hash,
    .copy_code      = host_copy_code,
    .selfdestruct   = host_selfdestruct,
    .call           = host_call,
    .get_tx_context = host_get_tx_context,
    .get_block_hash = host_get_block_hash,
    .emit_log       = host_emit_log,
    .access_account = host_access_account,
    .access_storage = host_access_storage,
};

/**
 * Initialize an evmone_message from JSON transaction data.
 *
 * @param message pointer to the message to initialize
 * @param tx      JSON transaction object
 * @param buffer  buffer to use for string operations
 */
static void set_message(evmone_message* message, json_t tx, buffer_t* buffer) {
  struct compatible_msg {
    int            kind;
    bool           is_static;
    int32_t        depth;
    int64_t        gas;
    evmc_address   destination;
    evmc_address   sender;
    const uint8_t* input_data;
    size_t         input_size;
    evmc_bytes32   value;
    evmc_bytes32   create_salt;
    evmc_address   code_address;
  } compat_msg = {0};

  bytes_t to = json_get_bytes(tx, "to", buffer);
  if (to.len == 20) {
    memcpy(compat_msg.destination.bytes, to.data, 20);
    memcpy(compat_msg.code_address.bytes, to.data, 20);
  }

  bytes_t from = json_get_bytes(tx, "from", buffer);
  if (from.len == 20) memcpy(compat_msg.sender.bytes, from.data, 20);

  compat_msg.gas = json_get_uint64(tx, "gas");
  if (compat_msg.gas == 0) compat_msg.gas = 10000000;

  bytes_t value = json_get_bytes(tx, "value", buffer);
  if (value.len && value.len <= 32) memcpy(compat_msg.value.bytes + 32 - value.len, value.data, value.len);

  bytes_t input = json_get_bytes(tx, "data", buffer);
  if (!input.len) input = json_get_bytes(tx, "input", buffer);
  compat_msg.input_data = input.data;
  compat_msg.input_size = input.len;

  memcpy(compat_msg.code_address.bytes, compat_msg.destination.bytes, 20);
  memcpy(message, &compat_msg, sizeof(*message));

  EVM_LOG("Message initialized:");
  EVM_LOG("  kind: %d", message->kind);
  EVM_LOG("  is_static: %s", message->is_static ? "true" : "false");
  EVM_LOG("  gas: %" PRId64, message->gas);
  debug_print_address("  destination", &message->destination);
  debug_print_address("  sender", &message->sender);
  debug_print_address("  code_address", &message->code_address);
  EVM_LOG("  input_size: %zu bytes", message->input_size);
  if (message->input_data && message->input_size > 0 && EVM_DEBUG) {
    size_t display_size = message->input_size > 64 ? 64 : message->input_size;
    fbprintf(stderr, "[EVM] input data: 0x%x%s\n",
             bytes(message->input_data, display_size),
             message->input_size > 64 ? "..." : "");
  }
  debug_print_bytes32("  value", &message->value);
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

  // reset the call result and logs
  if (evm->call_result.data) safe_free(evm->call_result.data);
  eth_call_cache_reset_accessed(evm->accounts);
  evm->call_result = NULL_BYTES;
  free_emitted_logs(evm->logs);
  evm->logs     = NULL;
  evm->gas_used = 0;
  evm->evm_done = false;

  set_message(&message, tx, &buffer);

  // special handling for precompiles
  if (bytes_all_zero(bytes(to, 19)) && to[19]) {
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

  evmone_context_t context = {
      .executor       = executor,
      .ctx            = ctx,
      .accounts       = evm->accounts,
      .block_number   = 0,
      .block_hash     = {0},
      .timestamp      = 0,
      .tx_origin      = {0},
      .gas_price      = 0,
      .parent         = NULL,
      .results        = NULL,
      .logs           = NULL,
      .capture_events = capture_events,
      .pap_mode       = evm->pap_mode,
      .storage_miss   = false,
  };

  bytes_t code = call_account_get_code(&context, to);

  // EIP-7702: resolve delegation indicator for top-level call
  if (code.len == 23 && code.data[0] == 0xef && code.data[1] == 0x01 && code.data[2] == 0x00) {
    memcpy(message.code_address.bytes, code.data + 3, 20);
    code = call_account_get_code(&context, message.code_address.bytes);
    EVM_LOG("EIP-7702: resolved delegation to code_address");
    debug_print_address("  delegated code_address", &message.code_address);
  }

  EVM_LOG("Contract code size: %u bytes", (uint32_t) code.len);

  evmone_result result = evmone_execute(
      executor,
      &host_interface,
      &context,
      EVMC_REV_OSAKA,
      &message,
      code.data,
      code.len);

  EVM_LOG("Result status code: %d", result.status_code);
  EVM_LOG("Gas left: %zu", (size_t) result.gas_left);
  EVM_LOG("Gas refund: %zu", (size_t) result.gas_refund);

  evm->gas_used = (uint64_t) (message.gas - result.gas_left);

  if (EVM_DEBUG && result.output_data && result.output_size > 0)
    print_hex(stderr, bytes(result.output_data, result.output_size), "[EVM] Output data: 0x", "\n");

  if (!ctx->state.error)
    evm->call_result = result.output_size ? bytes_dup(bytes(result.output_data, result.output_size)) : NULL_BYTES;
  else
    evm->call_result = NULL_BYTES;

  if (capture_events) {
    evm->logs    = context.logs;
    context.logs = NULL;
  }

  // The EVM may have created/modified accounts in context.accounts that
  // were originally pointing into evm->accounts. Propagate them back.
  evm->accounts    = context.accounts;
  context.accounts = NULL;

  if (result.status_code == 0) {
    EVM_LOG("Call verification successful");
  }
  else {
    EVM_LOG("Call verification failed with status code: %d", result.status_code);
    const char* error_msg = "Unknown error";
    switch (result.status_code) {
      case 1: error_msg = "Failure"; break;
      case 2: error_msg = "Revert"; break;
      case 3: error_msg = "Out of gas"; break;
      case 4: error_msg = "Invalid instruction"; break;
      case 5: error_msg = "Undefined instruction"; break;
      case 6: error_msg = "Stack overflow"; break;
      case 7: error_msg = "Stack underflow"; break;
      case 8: error_msg = "Bad jump destination"; break;
      case 9: error_msg = "Invalid memory access"; break;
      case 10: error_msg = "Call depth exceeded"; break;
      case 11: error_msg = "Static mode violation"; break;
      case 12: error_msg = "Precompile failure"; break;
      case 13: error_msg = "Contract validation failure"; break;
      case 14: error_msg = "Argument out of range"; break;
      case 15: error_msg = "WASM unreachable instruction"; break;
      case 16: error_msg = "WASM trap"; break;
      case 17: error_msg = "Insufficient balance"; break;
      case -1: error_msg = "Internal error"; break;
      case -2: error_msg = "Rejected"; break;
      case -3: error_msg = "Out of memory"; break;
    }
    EVM_LOG("Error details: %s", error_msg);
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
