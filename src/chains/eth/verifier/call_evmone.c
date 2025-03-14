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
#include "sync_committee.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Define debug macro for EVM execution
#define EVM_DEBUG 0 // Set to 0 to disable debugging
#define EVM_LOG(format, ...)                                             \
  do {                                                                   \
    if (EVM_DEBUG) fprintf(stderr, "[EVM] " format "\n", ##__VA_ARGS__); \
  } while (0)

// Storage status codes (from EVMC)
#define EVMC_STORAGE_UNCHANGED      0
#define EVMC_STORAGE_MODIFIED       1
#define EVMC_STORAGE_MODIFIED_AGAIN 2
#define EVMC_STORAGE_ADDED          3
#define EVMC_STORAGE_DELETED        4

// Debug utility functions
static void debug_print_address(const char* prefix, const uint8_t* addr) {
  if (!EVM_DEBUG) return;
  fprintf(stderr, "[EVM] %s: 0x", prefix);
  for (int i = 0; i < 20; i++) {
    fprintf(stderr, "%02x", addr[i]);
  }
  fprintf(stderr, "\n");
}

static void debug_print_bytes32(const char* prefix, const uint8_t* data) {
  if (!EVM_DEBUG) return;
  fprintf(stderr, "[EVM] %s: 0x", prefix);
  for (int i = 0; i < 32; i++) {
    fprintf(stderr, "%02x", data[i]);
  }
  fprintf(stderr, "\n");
}

// Check if all bytes are zero
static bool are_bytes_zero(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (data[i] != 0) return false;
  }
  return true;
}

/* EVM Host interface implementation */
static bool host_account_exists(void* context, const evmc_address* addr) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("account_exists for", addr->bytes);

  changed_account_t* ac = get_changed_account(ctx, addr->bytes);
  if (ac) return !ac->deleted;

  ssz_ob_t account = get_src_account(ctx, addr->bytes);
  bool     exists  = account.def != NULL;
  EVM_LOG("account_exists result: %s", exists ? "true" : "false");
  return exists;
}

static evmc_bytes32 host_get_storage(void* context, const evmc_address* addr, const evmc_bytes32* key) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("get_storage for account", addr->bytes);
  debug_print_bytes32("get_storage key", key->bytes);

  evmc_bytes32       result  = {0};
  changed_storage_t* storage = get_changed_storage(ctx, addr->bytes, key->bytes);
  if (storage)
    memcpy(result.bytes, storage->value, 32);
  else
    get_src_storage(ctx, addr->bytes, key->bytes, result.bytes);

  debug_print_bytes32("get_storage result", result.bytes);
  return result;
}

static int host_set_storage(void* context, const evmc_address* addr, const evmc_bytes32* key, const evmc_bytes32* value) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("set_storage for account", addr->bytes);
  debug_print_bytes32("set_storage key", key->bytes);
  debug_print_bytes32("set_storage value", value->bytes);

  evmc_bytes32 current_value   = host_get_storage(context, addr, key);
  bool         created_account = false;
  bool         created_storage = false;

  if (memcmp(current_value.bytes, value->bytes, 32) == 0) {
    EVM_LOG("set_storage: UNCHANGED");
    return EVMC_STORAGE_UNCHANGED;
  }

  set_changed_storage(ctx, addr->bytes, key->bytes, value->bytes, &created_account, &created_storage);

  if (created_account) {
    EVM_LOG("set_storage: ADDED (created account)");
    return EVMC_STORAGE_ADDED;
  }

  if (are_bytes_zero(value->bytes, 32)) {
    EVM_LOG("set_storage: DELETED");
    return EVMC_STORAGE_DELETED;
  }

  if (!created_storage) {
    EVM_LOG("set_storage: MODIFIED_AGAIN");
    return EVMC_STORAGE_MODIFIED_AGAIN;
  }

  if (created_storage && are_bytes_zero(current_value.bytes, 32)) {
    EVM_LOG("set_storage: ADDED (created storage)");
    return EVMC_STORAGE_ADDED;
  }

  EVM_LOG("set_storage: MODIFIED");
  return EVMC_STORAGE_MODIFIED;
}

static evmc_bytes32 host_get_balance(void* context, const evmc_address* addr) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("get_balance for", addr->bytes);

  evmc_bytes32       result = {0};
  changed_account_t* acc    = get_changed_account(ctx, addr->bytes);
  if (acc)
    memcpy(result.bytes, acc->balance, 32);
  else {
    ssz_ob_t account = get_src_account(ctx, addr->bytes);
    if (account.def) {
      ssz_ob_t balance = ssz_get(&account, "balance");
      if (balance.def && balance.bytes.len <= 32) {
        memcpy(result.bytes + 32 - balance.bytes.len, balance.bytes.data, balance.bytes.len);
      }
    }
  }

  debug_print_bytes32("get_balance result", result.bytes);
  return result;
}

static size_t host_get_code_size(void* context, const evmc_address* addr) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("get_code_size for", addr->bytes);

  size_t size = get_code(ctx, addr->bytes).len;
  EVM_LOG("get_code_size result: %zu bytes", size);
  return size;
}

static evmc_bytes32 host_get_code_hash(void* context, const evmc_address* addr) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("get_code_hash for", addr->bytes);

  evmc_bytes32 result = {0};
  bytes_t      code   = get_code(ctx, addr->bytes);
  if (code.len > 0) {
    keccak(code, result.bytes);
  }

  debug_print_bytes32("get_code_hash result", result.bytes);
  return result;
}

static size_t host_copy_code(void* context, const evmc_address* addr, size_t code_offset, uint8_t* buffer_data, size_t buffer_size) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("copy_code for", addr->bytes);
  EVM_LOG("copy_code offset: %zu, buffer size: %zu", code_offset, buffer_size);

  bytes_t code = get_code(ctx, addr->bytes);
  if (code_offset >= code.len) return 0;

  size_t copy_size = code.len - code_offset;
  if (buffer_size < copy_size) copy_size = buffer_size;

  memcpy(buffer_data, code.data + code_offset, copy_size);

  EVM_LOG("copy_code result: copied %zu bytes", copy_size);
  return copy_size;
}

static void host_selfdestruct(void* context, const evmc_address* addr, const evmc_address* beneficiary) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("selfdestruct account", addr->bytes);
  debug_print_address("selfdestruct beneficiary", beneficiary->bytes);

  bool               created;
  changed_account_t* acc = create_changed_account(ctx, addr->bytes, &created);

  // Free all storage
  while (acc->storage) {
    changed_storage_t* storage = acc->storage;
    acc->storage               = storage->next;
    free(storage);
  }

  // Mark as deleted
  acc->deleted = true;

  EVM_LOG("selfdestruct: account marked as deleted");
}

static void host_call(void* context, const struct evmc_message* msg, const uint8_t* code, size_t code_size, struct evmc_result* result) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  EVM_LOG("========Executing child call...");
  debug_print_address("call from", msg->sender.bytes);
  debug_print_address("call to", msg->destination.bytes);
  debug_print_address("code from", msg->code_address.bytes);

  // Check for precompiled contracts (address < 0x20)
  uint8_t zero_bytes[19] = {0};
  if (memcmp(msg->code_address.bytes, zero_bytes, 19) == 0 && msg->code_address.bytes[19] < 0x20) {
    buffer_t     output     = {0};
    uint64_t     gas_used   = 0;
    bytes_t      input_data = {(uint8_t*) msg->input_data, msg->input_size};
    pre_result_t pre_result = eth_execute_precompile(msg->code_address.bytes, input_data, &output, &gas_used);

    result->output_data = output.data.data;
    result->output_size = output.data.len;
    result->gas_left    = msg->gas - gas_used;
    result->gas_refund  = 0;
    result->status_code = pre_result == PRE_SUCCESS ? EVMC_SUCCESS : EVMC_FAILURE;

    if (pre_result != PRE_SUCCESS) {
      EVM_LOG("Precompile failed with status code: %d", pre_result);
      result->gas_left = 0;
    }
    return;
  }

  // If code isn't provided, fetch it from the account
  const uint8_t* execution_code      = code;
  size_t         execution_code_size = code_size;
  bytes_t        fetched_code        = {0};

  if ((execution_code == NULL || execution_code_size == 0) &&
      msg->kind != 3 && msg->kind != 4) { // CALL_KIND_CREATE and CALL_KIND_CREATE2
    EVM_LOG("Code not provided, fetching from code_address");
    fetched_code        = get_code(ctx, msg->code_address.bytes);
    execution_code      = fetched_code.data;
    execution_code_size = fetched_code.len;
    EVM_LOG("Fetched code size: %zu bytes", execution_code_size);
  }

  // Create a child context
  evmone_context_t* child_ctx = calloc(1, sizeof(evmone_context_t));
  if (!child_ctx) {
    result->status_code = EVMC_OUT_OF_MEMORY;
    return;
  }

  child_ctx->executor     = ctx->executor;
  child_ctx->ctx          = ctx->ctx;
  child_ctx->src_accounts = ctx->src_accounts;
  child_ctx->parent       = ctx;

  // Set up the host interface
  static const struct evmc_host_interface host_interface = {
      .account_exists = host_account_exists,
      .get_storage    = host_get_storage,
      .set_storage    = host_set_storage,
      .get_balance    = host_get_balance,
      .get_code_size  = host_get_code_size,
      .get_code_hash  = host_get_code_hash,
      .copy_code      = host_copy_code,
      .selfdestruct   = host_selfdestruct,
      .call           = host_call,
      .get_tx_context = NULL, // Not implemented for simplicity
      .get_block_hash = NULL, // Not implemented for simplicity
      .emit_log       = NULL, // Not implemented for simplicity
      .access_account = NULL, // Not implemented for simplicity
      .access_storage = NULL  // Not implemented for simplicity
  };

  // Execute the code
  *result = evmone_execute(
      ctx->executor,
      &host_interface,
      child_ctx,
      0, // Revision - simplified for now
      msg,
      execution_code,
      execution_code_size);

  // Free the child context
  context_free(child_ctx);
  free(child_ctx);
}

// Set up a message from JSON transaction data
static void set_message(struct evmc_message* message, json_t tx, buffer_t* buffer) {
  memset(message, 0, sizeof(*message));

  // Set destination (to) address
  bytes_t to = json_get_bytes(tx, "to", buffer);
  if (to.len == 20) {
    memcpy(message->destination.bytes, to.data, 20);
    memcpy(message->code_address.bytes, to.data, 20);
  }

  // Set sender (from) address
  bytes_t from = json_get_bytes(tx, "from", buffer);
  if (from.len == 20) {
    memcpy(message->sender.bytes, from.data, 20);
  }

  // Set gas limit
  message->gas = json_get_uint64(tx, "gas");
  if (message->gas == 0) message->gas = 10000000; // Default gas limit if not specified

  // Set value
  bytes_t value = json_get_bytes(tx, "value", buffer);
  if (value.len && value.len <= 32) {
    memcpy(message->value.bytes + 32 - value.len, value.data, value.len);
  }

  // Set input data (check both "data" and "input" fields)
  bytes_t input = json_get_bytes(tx, "data", buffer);
  if (!input.len) input = json_get_bytes(tx, "input", buffer);
  message->input_data = input.data;
  message->input_size = input.len;

  // Set call kind (always CALL for transactions)
  message->kind = 0; // CALL_KIND_CALL = 0

  EVM_LOG("Message initialized:");
  EVM_LOG("  kind: %d", message->kind);
  EVM_LOG("  gas: %lld", message->gas);
  debug_print_address("  destination", message->destination.bytes);
  debug_print_address("  sender", message->sender.bytes);
  debug_print_address("  code_address", message->code_address.bytes);
  EVM_LOG("  input_size: %zu bytes", message->input_size);
}

// Main function to execute a transaction in the EVM
bool eth_run_call_evmone(verify_ctx_t* ctx, ssz_ob_t accounts, json_t tx, bytes_t* call_result) {
  // Create the evmone context
  evmone_context_t* evmone_ctx = calloc(1, sizeof(evmone_context_t));
  if (!evmone_ctx) {
    EVM_LOG("Failed to create evmone context");
    return false;
  }

  evmone_ctx->executor     = evmc_create_evmone();
  evmone_ctx->ctx          = ctx;
  evmone_ctx->src_accounts = accounts;

  if (!evmone_ctx->executor) {
    EVM_LOG("Failed to create evmone executor");
    free(evmone_ctx);
    return false;
  }

  // Set up the host interface
  static const struct evmc_host_interface host_interface = {
      .account_exists = host_account_exists,
      .get_storage    = host_get_storage,
      .set_storage    = host_set_storage,
      .get_balance    = host_get_balance,
      .get_code_size  = host_get_code_size,
      .get_code_hash  = host_get_code_hash,
      .copy_code      = host_copy_code,
      .selfdestruct   = host_selfdestruct,
      .call           = host_call,
      .get_tx_context = NULL, // Not implemented for simplicity
      .get_block_hash = NULL, // Not implemented for simplicity
      .emit_log       = NULL, // Not implemented for simplicity
      .access_account = NULL, // Not implemented for simplicity
      .access_storage = NULL  // Not implemented for simplicity
  };

  // Set up the message from the transaction
  struct evmc_message message;
  buffer_t            buffer = {0};
  set_message(&message, tx, &buffer);

  // Get the contract code
  bytes_t code = get_code(evmone_ctx, message.destination.bytes);

  if (code.len == 0) {
    EVM_LOG("No code found at the destination address");
    if (call_result) {
      call_result->data = NULL;
      call_result->len  = 0;
    }
    context_free(evmone_ctx);
    free(evmone_ctx);
    return true;
  }

  // Execute the code
  struct evmc_result result = evmone_execute(
      evmone_ctx->executor,
      &host_interface,
      evmone_ctx,
      0, // Revision - simplified for now
      &message,
      code.data,
      code.len);

  EVM_LOG("Execution result: status code = %d, gas left = %lld", result.status_code, result.gas_left);

  // Check the execution result
  bool success = result.status_code == EVMC_SUCCESS;

  // Set the call result
  if (call_result) {
    if (result.output_data && result.output_size > 0) {
      call_result->data = malloc(result.output_size);
      if (call_result->data) {
        memcpy(call_result->data, result.output_data, result.output_size);
        call_result->len = result.output_size;
      }
      else {
        call_result->len = 0;
      }
    }
    else {
      call_result->data = NULL;
      call_result->len  = 0;
    }
  }

  // Free resources
  context_free(evmone_ctx);
  free(evmone_ctx);

  // Release the result if needed
  if (result.release) {
    void (*release_fn)(struct evmc_result*) = (void (*)(struct evmc_result*)) result.release;
    release_fn(&result);
  }

  return success;
}