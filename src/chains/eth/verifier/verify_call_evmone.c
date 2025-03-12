#include "bytes.h"
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

// Context for EVM execution
typedef struct evmone_context {
  // State - Use a more generic type until we define patricia implementation details
  void* state; // Will hold state information
  // Current block info
  uint64_t  block_number;
  bytes32_t block_hash;
  uint64_t  timestamp;
  // Transaction info
  bytes32_t tx_origin;
  uint64_t  gas_price;
  // For storing results
  bool    success;
  bytes_t output;
} evmone_context_t;

// Check if an account exists
static bool host_account_exists(void* context, const evmc_address* addr) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  // TODO: Implement account existence check using patricia_state_t
  // For now, return true to indicate all accounts exist
  return true;
}

// Get storage value for an account
static evmc_bytes32 host_get_storage(void* context, const evmc_address* addr, const evmc_bytes32* key) {
  evmone_context_t* ctx    = (evmone_context_t*) context;
  evmc_bytes32      result = {0};
  // TODO: Implement storage access using patricia_state_t
  return result;
}

// Set storage value for an account
static evmone_storage_status host_set_storage(void* context, const evmc_address* addr, const evmc_bytes32* key, const evmc_bytes32* value) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  // TODO: Implement storage modification using patricia_state_t
  return EVMONE_STORAGE_UNCHANGED;
}

// Get account balance
static evmc_bytes32 host_get_balance(void* context, const evmc_address* addr) {
  evmone_context_t* ctx    = (evmone_context_t*) context;
  evmc_bytes32      result = {0};
  // TODO: Implement balance retrieval from patricia_state_t
  return result;
}

// Get code size for an account
static size_t host_get_code_size(void* context, const evmc_address* addr) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  // TODO: Implement code size retrieval
  return 0;
}

// Get code hash for an account
static evmc_bytes32 host_get_code_hash(void* context, const evmc_address* addr) {
  evmone_context_t* ctx    = (evmone_context_t*) context;
  evmc_bytes32      result = {0};
  // TODO: Implement code hash retrieval
  return result;
}

// Copy code from an account
static size_t host_copy_code(void* context, const evmc_address* addr, size_t code_offset, uint8_t* buffer_data, size_t buffer_size) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  // TODO: Implement code copying
  return 0;
}

// Handle selfdestruct operation
static void host_selfdestruct(void* context, const evmc_address* addr, const evmc_address* beneficiary) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  // TODO: Implement selfdestruct logic
}

// Handle call to another contract
static void host_call(void* context, const struct evmone_message* msg, const uint8_t* code, size_t code_size, struct evmone_result* result) {
  evmone_context_t* ctx = (evmone_context_t*) context;
  // TODO: Implement call logic

  // Set some defaults for the result
  result->status_code    = 0; // Success
  result->gas_left       = 0;
  result->gas_refund     = 0;
  result->output_data    = NULL;
  result->output_size    = 0;
  result->create_address = NULL;
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
static const evmone_host_interface host_interface = {
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

static bytes_t get_code(ssz_ob_t accounts, bytes_t address) {
  for (int i = 0; i < ssz_len(accounts); i++) {
    ssz_ob_t account = ssz_at(accounts, i);
    bytes_t  addr    = ssz_get(&account, "address").bytes;
    if (addr.len == address.len && memcmp(addr.data, address.data, address.len) == 0)
      return ssz_get(&account, "code").bytes;
  }
  return (bytes_t) {0};
}

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

  // TODO: Initialize the context with data from the state proof

  // Parse the transaction parameters from JSON
  json_t tx = json_at(ctx->args, 0);

  // Initialize our EVM context with state from the proof
  evmone_context_t context = {0};

  // Get the code to execute from the account state
  bytes_t code = get_code(accounts, json_get_bytes(tx, "to", &buffer));

  // Initialize the EVM message
  evmone_message message;
  set_message(&message, tx, &buffer);

  // Create the EVM executor
  void* executor = evmone_create_executor();

  // Execute the code
  evmone_result result = evmone_execute(
      executor,
      &host_interface,
      &context,
      0, // Revision (0 for latest)
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