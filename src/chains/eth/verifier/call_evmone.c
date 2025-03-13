#include "bytes.h"
#include "call_ctx.h"
#include "crypto.h"
#include "eth_verify.h"
#include "evmone_c_wrapper.h"
#include "json.h"
#include "patricia.h"
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

/* Define the call kinds enum to match evmone_message's anonymous enum */
typedef enum {
  CALL_KIND_CALL         = 0,
  CALL_KIND_DELEGATECALL = 1,
  CALL_KIND_CALLCODE     = 2,
  CALL_KIND_CREATE       = 3,
  CALL_KIND_CREATE2      = 4
} evmone_call_kind;

/* EVM Host interface implementation */
static const struct evmone_host_interface host_interface;

// Debug function to print address as hex
static void debug_print_address(const char* prefix, const evmc_address* addr) {
  if (!EVM_DEBUG) return;
  fprintf(stderr, "[EVM] %s: 0x", prefix);
  for (int i = 0; i < 20; i++) {
    fprintf(stderr, "%02x", addr->bytes[i]);
  }
  fprintf(stderr, "\n");
}

// Debug function to print bytes32 as hex
static void debug_print_bytes32(const char* prefix, const evmc_bytes32* data) {
  if (!EVM_DEBUG) return;
  fprintf(stderr, "[EVM] %s: 0x", prefix);
  for (int i = 0; i < 32; i++) {
    fprintf(stderr, "%02x", data->bytes[i]);
  }
  fprintf(stderr, "\n");
}

// Check if an account exists
static bool host_account_exists(void* context, const evmc_address* addr) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("account_exists for", addr);
  changed_account_t* ac = get_changed_account(ctx, addr->bytes);
  if (ac) return ac->deleted;
  bool exists = get_src_account(ctx, addr->bytes).def != NULL;
  EVM_LOG("account_exists result: %s", exists ? "true" : "false");
  return exists;
}

// Get storage value for an account
static evmc_bytes32 host_get_storage(void* context, const evmc_address* addr, const evmc_bytes32* key) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("get_storage for account", addr);
  debug_print_bytes32("get_storage key", key);

  evmc_bytes32       result  = {0};
  changed_storage_t* storage = get_changed_storage(ctx, addr->bytes, key->bytes);
  if (storage)
    memcpy(result.bytes, storage->value, 32);
  else
    get_src_storage(ctx, addr->bytes, key->bytes, result.bytes);

  debug_print_bytes32("get_storage result", &result);
  return result;
}

// Set storage value for an account
static evmone_storage_status host_set_storage(void* context, const evmc_address* addr, const evmc_bytes32* key, const evmc_bytes32* value) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("set_storage for account", addr);
  debug_print_bytes32("set_storage key", key);
  debug_print_bytes32("set_storage value", value);

  evmc_bytes32 current_value   = host_get_storage(context, addr, key);
  bool         created_account = false;
  bool         created_storage = false;
  if (memcmp(current_value.bytes, value->bytes, 32) == 0) {
    EVM_LOG("set_storage: UNCHANGED");
    return EVMONE_STORAGE_UNCHANGED;
  }

  set_changed_storage(ctx, addr->bytes, key->bytes, value->bytes, &created_account, &created_storage);
  if (created_account) {
    EVM_LOG("set_storage: ADDED (created account)");
    return EVMONE_STORAGE_ADDED;
  }
  if (bytes_all_zero(bytes(value->bytes, 32))) {
    EVM_LOG("set_storage: DELETED");
    return EVMONE_STORAGE_DELETED;
  }
  if (!created_storage) {
    EVM_LOG("set_storage: MODIFIED_AGAIN");
    return EVMONE_STORAGE_MODIFIED_AGAIN;
  }
  if (created_storage && bytes_all_zero(bytes(current_value.bytes, 32))) {
    EVM_LOG("set_storage: ADDED (created storage)");
    return EVMONE_STORAGE_ADDED;
  }

  EVM_LOG("set_storage: MODIFIED");
  return EVMONE_STORAGE_MODIFIED;
}

// Get account balance
static evmc_bytes32 host_get_balance(void* context, const evmc_address* addr) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("get_balance for", addr);

  evmc_bytes32       result = {0};
  changed_account_t* acc    = get_changed_account(ctx, addr->bytes);
  if (acc)
    memcpy(result.bytes, acc->balance, 32);
  else {
    ssz_ob_t account = get_src_account(ctx, addr->bytes);
    if (account.def) memcpy(result.bytes, ssz_get(&account, "balance").bytes.data, 32);
  }

  debug_print_bytes32("get_balance result", &result);
  return result;
}

// Get code size for an account
static size_t host_get_code_size(void* context, const evmc_address* addr) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("get_code_size for", addr);

  size_t size = get_code(ctx, addr->bytes).len;
  EVM_LOG("get_code_size result: %zu bytes", size);
  return size;
}

// Get code hash for an account
static evmc_bytes32 host_get_code_hash(void* context, const evmc_address* addr) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("get_code_hash for", addr);

  evmc_bytes32 result = {0};
  keccak(get_code(ctx, addr->bytes), result.bytes);

  debug_print_bytes32("get_code_hash result", &result);
  return result;
}

// Copy code from an account
static size_t host_copy_code(void* context, const evmc_address* addr, size_t code_offset, uint8_t* buffer_data, size_t buffer_size) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("copy_code for", addr);
  EVM_LOG("copy_code offset: %zu, buffer size: %zu", code_offset, buffer_size);

  bytes_t code      = get_code(ctx, addr->bytes);
  size_t  copy_size = code.len - code_offset;
  if (buffer_size < copy_size) copy_size = buffer_size;
  memcpy(buffer_data, code.data + code_offset, copy_size);

  EVM_LOG("copy_code result: copied %zu bytes", copy_size);
  return copy_size;
}

// Handle selfdestruct operation
static void host_selfdestruct(void* context, const evmc_address* addr, const evmc_address* beneficiary) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("selfdestruct account", addr);
  debug_print_address("selfdestruct beneficiary", beneficiary);

  bool               created;
  changed_account_t* acc = create_changed_account(ctx, addr->bytes, &created);
  while (acc->storage) {
    changed_storage_t* storage = acc->storage;
    acc->storage               = storage->next;
    free(storage);
  }
  acc->deleted = true;

  EVM_LOG("selfdestruct: account marked as deleted");
}

// Handle call to another contract
static void host_call(void* context, const struct evmone_message* msg, const uint8_t* code, size_t code_size, struct evmone_result* result) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  EVM_LOG("========Executing child call...");
  debug_print_address("call from", &msg->sender);
  debug_print_address("call to", &msg->destination);
  debug_print_address("code from", &msg->code_address);
  EVM_LOG("call gas: %lld, depth: %d, is_static: %s", msg->gas, msg->depth, msg->is_static ? "true" : "false");

  // If code isn't provided (which happens during DELEGATECALL and CALLCODE),
  // we need to fetch it from the account specified by code_address
  const uint8_t* execution_code      = code;
  size_t         execution_code_size = code_size;
  bytes_t        fetched_code        = {0};

  if ((execution_code == NULL || execution_code_size == 0) && msg->kind != CALL_KIND_CREATE && msg->kind != CALL_KIND_CREATE2) {
    EVM_LOG("Code not provided, fetching from code_address");
    fetched_code        = get_code(ctx, msg->code_address.bytes);
    execution_code      = fetched_code.data;
    execution_code_size = fetched_code.len;
    EVM_LOG("Fetched code size: %zu bytes", execution_code_size);
  }

  EVM_LOG("call code size: %zu bytes", execution_code_size);
  if (msg->input_data && msg->input_size > 0) {
    EVM_LOG("call input data (%zu bytes): 0x", msg->input_size);
    if (EVM_DEBUG) {
      for (size_t i = 0; i < (msg->input_size > 64 ? 64 : msg->input_size); i++) {
        fprintf(stderr, "%02x", msg->input_data[i]);
      }
      if (msg->input_size > 64) fprintf(stderr, "...");
      fprintf(stderr, "\n");
    }
  }

  evmone_context_t child = *ctx;
  child.parent           = ctx;
  child.changed_accounts = NULL;

  // Execute the code (now using fetched code if needed)
  evmone_result exec_result = evmone_execute(
      ctx->executor,
      &host_interface,
      &child,
      14, // Revision - using CANCUN
      msg,
      execution_code,
      execution_code_size);

  EVM_LOG("Child call complete. Status: %d, Gas left: %llu", exec_result.status_code, exec_result.gas_left);
  if (exec_result.output_data && exec_result.output_size > 0) {
    EVM_LOG("Child call output (%zu bytes): 0x", exec_result.output_size);
    if (EVM_DEBUG) {
      for (size_t i = 0; i < (exec_result.output_size > 64 ? 64 : exec_result.output_size); i++) {
        fprintf(stderr, "%02x", exec_result.output_data[i]);
      }
      if (exec_result.output_size > 64) fprintf(stderr, "...");
      fprintf(stderr, "\n");
    }
  }
  EVM_LOG("========/child call complete ====");

  context_free(&child);
  *result = exec_result;
}

// Get transaction context
static evmc_bytes32 host_get_tx_context(void* context) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  EVM_LOG("get_tx_context called");

  evmc_bytes32 result = {0};
  // TODO: Return serialized transaction context

  debug_print_bytes32("get_tx_context result", &result);
  return result;
}

// Get block hash for a specific block number
static evmc_bytes32 host_get_block_hash(void* context, int64_t number) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  EVM_LOG("get_block_hash for block number: %lld", number);

  evmc_bytes32 result = {0};
  // TODO: Implement block hash retrieval logic

  debug_print_bytes32("get_block_hash result", &result);
  return result;
}

// Handle emitting logs
static void host_emit_log(void* context, const evmc_address* addr, const uint8_t* data, size_t data_size, const evmc_bytes32 topics[], size_t topics_count) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("emit_log from", addr);
  EVM_LOG("emit_log: data size: %zu bytes, topics count: %zu", data_size, topics_count);

  if (data && data_size > 0 && EVM_DEBUG) {
    fprintf(stderr, "[EVM] Log data (hex): 0x");
    for (size_t i = 0; i < (data_size > 64 ? 64 : data_size); i++) {
      fprintf(stderr, "%02x", data[i]);
    }
    if (data_size > 64) fprintf(stderr, "...");
    fprintf(stderr, "\n");
  }

  for (size_t i = 0; i < topics_count && EVM_DEBUG; i++) {
    debug_print_bytes32("Log topic", &topics[i]);
  }
}

// Track account access for gas metering
static void host_access_account(void* context, const evmc_address* addr) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("access_account", addr);
}

// Track storage access for gas metering
static void host_access_storage(void* context, const evmc_address* addr, const evmc_bytes32* key) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  debug_print_address("access_storage account", addr);
  debug_print_bytes32("access_storage key", key);
}

// Set up the host interface with all our callback functions
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
 * Initialize an evmone_message from JSON transaction data
 *
 * @param message Pointer to the message to initialize
 * @param tx JSON transaction object
 * @param buffer Buffer to use for string operations
 */
static void set_message(evmone_message* message, json_t tx, buffer_t* buffer) {
  // Use a binary-compatible struct to avoid enum issues
  struct compatible_msg {
    int            kind; // 0 = CALL
    bool           is_static;
    int32_t        depth;
    int64_t        gas;
    evmc_address   destination;
    evmc_address   sender;
    const uint8_t* input_data;
    size_t         input_size;
    evmc_bytes32   value;
    evmc_bytes32   create_salt;
    evmc_address   code_address; /* Address of the code to execute */
  } compat_msg = {0};

  // Set destination (to) address
  bytes_t to = json_get_bytes(tx, "to", buffer);
  if (to.len == 20) {
    memcpy(compat_msg.destination.bytes, to.data, 20);
    memcpy(compat_msg.code_address.bytes, to.data, 20);
  }

  // Set sender (from) address
  bytes_t from = json_get_bytes(tx, "from", buffer);
  if (from.len == 20) memcpy(compat_msg.sender.bytes, from.data, 20);

  // Set gas limit
  compat_msg.gas = json_get_uint64(tx, "gas");
  if (compat_msg.gas == 0) compat_msg.gas = 10000000; // Default gas limit if not specified

  // Set value
  bytes_t value = json_get_bytes(tx, "value", buffer);
  if (value.len && value.len <= 32) memcpy(compat_msg.value.bytes + 32 - value.len, value.data, value.len);

  // Set input data (check both "data" and "input" fields)
  bytes_t input = json_get_bytes(tx, "data", buffer);
  if (!input.len) input = json_get_bytes(tx, "input", buffer);
  compat_msg.input_data = input.data;
  compat_msg.input_size = input.len;

  // Set code_address to match destination by default
  // This is what happens in normal CALL operations
  memcpy(compat_msg.code_address.bytes, compat_msg.destination.bytes, 20);

  // Copy the initialized struct to the actual message
  memcpy(message, &compat_msg, sizeof(*message));

  // Debug print message details
  EVM_LOG("Message initialized:");
  EVM_LOG("  kind: %d", message->kind);
  EVM_LOG("  is_static: %s", message->is_static ? "true" : "false");
  EVM_LOG("  gas: %lld", message->gas);
  debug_print_address("  destination", &message->destination);
  debug_print_address("  sender", &message->sender);
  debug_print_address("  code_address", &message->code_address);
  EVM_LOG("  input_size: %zu bytes", message->input_size);
  if (message->input_data && message->input_size > 0 && EVM_DEBUG) {
    fprintf(stderr, "[EVM] input data: 0x");
    for (size_t i = 0; i < (message->input_size > 64 ? 64 : message->input_size); i++) {
      fprintf(stderr, "%02x", message->input_data[i]);
    }
    if (message->input_size > 64) fprintf(stderr, "...");
    fprintf(stderr, "\n");
  }
  debug_print_bytes32("  value", &message->value);
}

// Function to verify call proof
bool eth_run_call_evmone(verify_ctx_t* ctx, ssz_ob_t accounts, json_t tx, bytes_t* call_result) {
  buffer_t  buffer = {0};
  address_t to     = {0};
  buffer_t  to_buf = stack_buffer(to);

  // Check if the transaction has a "to" address
  if (json_get_bytes(tx, "to", &to_buf).len != 20) RETURN_VERIFY_ERROR(ctx, "Invalid transaction: to address is not 20 bytes");

  EVM_LOG("Creating EVM executor...");
  void* executor = evmone_create_executor();
  if (!executor) RETURN_VERIFY_ERROR(ctx, "Error: Failed to create executor");

  // Initialize our EVM context with state from the proof
  evmone_context_t context = {
      .executor         = executor,
      .ctx              = ctx,
      .src_accounts     = accounts,
      .changed_accounts = NULL,
      .block_number     = 0,
      .block_hash       = {0},
      .timestamp        = 0,
      .tx_origin        = {0},
      .gas_price        = 0,
      .parent           = NULL,
  };

  bytes_t code = get_code(&context, to);
  EVM_LOG("Contract code size: %u bytes", (uint32_t) code.len);

  // Initialize the EVM message
  evmone_message message;
  set_message(&message, tx, &buffer);

  // Execute the code
  evmone_result result = evmone_execute(
      executor,
      &host_interface,
      &context,
      14, // Using CANCUN revision
      &message,
      code.data,
      code.len);

  EVM_LOG("Result status code: %d", result.status_code);
  EVM_LOG("Gas left: %llu", result.gas_left);
  EVM_LOG("Gas refund: %llu", result.gas_refund);

  if (EVM_DEBUG && result.output_data && result.output_size > 0)
    print_hex(stderr, bytes(result.output_data, result.output_size), "[EVM] Output data: 0x", "\n");

  // copy result
  *call_result = result.output_size ? bytes_dup(bytes(result.output_data, result.output_size)) : NULL_BYTES;

  // Process the execution result
  if (result.status_code == 0) { // Success
    EVM_LOG("Call verification successful");
  }
  else {
    EVM_LOG("Call verification failed with status code: %d", result.status_code);
    // Map status codes to error messages
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
  }

  // Clean up resources
  evmone_release_result(&result);
  evmone_destroy_executor(executor);
  buffer_free(&buffer);
  EVM_LOG("=== EVM call verification complete ===");

  return true;
}