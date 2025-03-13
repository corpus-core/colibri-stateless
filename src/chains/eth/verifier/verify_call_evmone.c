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

// Check if an account exists
static bool host_account_exists(void* context, const evmc_address* addr) {
  evmone_context_t*  ctx = (evmone_context_t*) context;
  changed_account_t* ac  = get_changed_account(ctx, addr->bytes);
  if (ac) return ac->deleted;
  return get_src_account(ctx, addr->bytes).def != NULL;
}

// Get storage value for an account
static evmc_bytes32 host_get_storage(void* context, const evmc_address* addr, const evmc_bytes32* key) {
  evmone_context_t*  ctx     = (evmone_context_t*) context;
  evmc_bytes32       result  = {0};
  changed_storage_t* storage = get_changed_storage(ctx, addr->bytes, key->bytes);
  if (storage)
    memcpy(result.bytes, storage->value, 32);
  else
    get_src_storage(ctx, addr->bytes, key->bytes, result.bytes);
  return result;
}

// Set storage value for an account
static evmone_storage_status host_set_storage(void* context, const evmc_address* addr, const evmc_bytes32* key, const evmc_bytes32* value) {
  evmone_context_t* ctx             = (evmone_context_t*) context;
  evmc_bytes32      current_value   = host_get_storage(context, addr, key);
  bool              created_account = false;
  bool              created_storage = false;
  if (memcmp(current_value.bytes, value->bytes, 32) == 0) return EVMONE_STORAGE_UNCHANGED;

  set_changed_storage(ctx, addr->bytes, key->bytes, value->bytes, &created_account, &created_storage);
  if (created_account) return EVMONE_STORAGE_ADDED;
  if (bytes_all_zero(bytes(value->bytes, 32))) return EVMONE_STORAGE_DELETED;
  if (!created_storage) return EVMONE_STORAGE_MODIFIED_AGAIN;
  if (created_storage && bytes_all_zero(bytes(current_value.bytes, 32))) return EVMONE_STORAGE_ADDED;

  return EVMONE_STORAGE_MODIFIED;
}

// Get account balance
static evmc_bytes32 host_get_balance(void* context, const evmc_address* addr) {
  evmone_context_t*  ctx    = (evmone_context_t*) context;
  evmc_bytes32       result = {0};
  changed_account_t* acc    = get_changed_account(ctx, addr->bytes);
  if (acc)
    memcpy(result.bytes, acc->balance, 32);
  else {
    ssz_ob_t account = get_src_account(ctx, addr->bytes);
    if (account.def) memcpy(result.bytes, ssz_get(&account, "balance").bytes.data, 32);
  }

  return result;
}

// Get code size for an account
static size_t host_get_code_size(void* context, const evmc_address* addr) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  return get_code(ctx, addr->bytes).len;
}

// Get code hash for an account
static evmc_bytes32 host_get_code_hash(void* context, const evmc_address* addr) {
  evmone_context_t* ctx    = (evmone_context_t*) context;
  evmc_bytes32      result = {0};
  keccak(get_code(ctx, addr->bytes), result.bytes);
  return result;
}

// Copy code from an account
static size_t host_copy_code(void* context, const evmc_address* addr, size_t code_offset, uint8_t* buffer_data, size_t buffer_size) {
  evmone_context_t* ctx       = (evmone_context_t*) context;
  bytes_t           code      = get_code(ctx, addr->bytes);
  size_t            copy_size = code.len - code_offset;
  if (buffer_size < copy_size) copy_size = buffer_size;
  memcpy(buffer_data, code.data + code_offset, copy_size);
  return copy_size;
}

// Handle selfdestruct operation
static void host_selfdestruct(void* context, const evmc_address* addr, const evmc_address* beneficiary) {
  evmone_context_t*  ctx = (evmone_context_t*) context;
  bool               created;
  changed_account_t* acc = create_changed_account(ctx, addr->bytes, &created);
  while (acc->storage) {
    changed_storage_t* storage = acc->storage;
    acc->storage               = storage->next;
    free(storage);
  }
  acc->deleted = true;
}

// Handle call to another contract
static void host_call(void* context, const struct evmone_message* msg, const uint8_t* code, size_t code_size, struct evmone_result* result) {
  evmone_context_t* ctx   = (evmone_context_t*) context;
  evmone_context_t  child = *ctx;
  child.parent            = ctx;
  child.changed_accounts  = NULL;
  // Execute the code
  evmone_result exec_result = evmone_execute(
      ctx->executor,
      &host_interface,
      &child,
      14, // Revision (0 for latest)
      msg,
      code,
      code_size);

  context_free(&child);
  *result = exec_result;
}

// Get transaction context
static evmc_bytes32 host_get_tx_context(void* context) {
  evmone_context_t* ctx    = (evmone_context_t*) context;
  evmc_bytes32      result = {0};
  // TODO: Return serialized transaction context
  return result;
}

// Get block hash for a specific block number
static evmc_bytes32 host_get_block_hash(void* context, int64_t number) {
  evmone_context_t* ctx    = (evmone_context_t*) context;
  evmc_bytes32      result = {0};

  // TODO: Implement block hash retrieval logic
  return result;
}

// Handle emitting logs
static void host_emit_log(void* context, const evmc_address* addr, const uint8_t* data, size_t data_size, const evmc_bytes32 topics[], size_t topics_count) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  // TODO: Implement log emission logic
}

// Track account access for gas metering
static void host_access_account(void* context, const evmc_address* addr) {
  evmone_context_t* ctx = (evmone_context_t*) context;

  // TODO: Implement account access tracking
}

// Track storage access for gas metering
static void host_access_storage(void* context, const evmc_address* addr, const evmc_bytes32* key) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  // TODO: Implement storage access tracking
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
    uint64_t       code_address;
  } compat_msg = {0};

  // Set destination (to) address
  bytes_t to = json_get_bytes(tx, "to", buffer);
  if (to.len == 20) memcpy(compat_msg.destination.bytes, to.data, 20);

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

  // Copy the initialized struct to the actual message
  memcpy(message, &compat_msg, sizeof(*message));
}

// Function to verify call proof
bool verify_call_proof(verify_ctx_t* ctx) {
  bytes32_t body_root                = {0};
  bytes32_t state_root               = {0};
  ssz_ob_t  state_proof              = ssz_get(&ctx->proof, "state_proof");
  ssz_ob_t  accounts                 = ssz_get(&ctx->proof, "accounts");
  ssz_ob_t  state_merkle_proof       = ssz_get(&state_proof, "state_proof");
  ssz_ob_t  header                   = ssz_get(&state_proof, "header");
  ssz_ob_t  sync_committee_bits      = ssz_get(&state_proof, "sync_committee_bits");
  ssz_ob_t  sync_committee_signature = ssz_get(&state_proof, "sync_committee_signature");
  buffer_t  buffer                   = {0};
  address_t to                       = {0};
  buffer_t  to_buf                   = stack_buffer(to);
  // TODO: Initialize the context with data from the state proof

  // Parse the transaction parameters from JSON
  json_t tx = json_at(ctx->args, 0);
  if (json_get_bytes(tx, "to", &to_buf).len != 20) RETURN_VERIFY_ERROR(ctx, "Invalid transaction: to address is not 20 bytes");

  void* executor = evmone_create_executor();

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
      .output           = NULL_BYTES,
      .gas_price        = 0,
      .success          = false,
      .parent           = NULL,
  };

  // Get the code to execute from the account state
  bytes_t code = get_code(&context, to);

  // Initialize the EVM message
  evmone_message message;
  set_message(&message, tx, &buffer);

  // Execute the code
  evmone_result result = evmone_execute(
      executor,
      &host_interface,
      &context,
      14, // OSAKA-Version
      &message,
      code.data,
      code.len);

  // Process the execution result
  if (result.status_code == 0) { // Success
    ctx->success = true;

    // TODO: Save the execution output if needed
    // if (result.output_data && result.output_size > 0) {
    //     // Save output data
    // }
  }
  else {
    ctx->success = false;
    // TODO: Handle specific error cases
  }

  // Clean up resources
  evmone_release_result(&result);
  evmone_destroy_executor(executor);

  return ctx->success;
}