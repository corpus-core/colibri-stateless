/**
 * C wrapper for evmone library
 */
#ifndef EVMONE_C_WRAPPER_H
#define EVMONE_C_WRAPPER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Basic types needed for the interface
typedef struct {
  uint8_t bytes[20];
} evmc_address;
typedef struct {
  uint8_t bytes[32];
} evmc_bytes32;
typedef int64_t        evmc_int64;
typedef uint64_t       evmc_uint64;
typedef uint8_t        evmc_bytes;
typedef int            evmc_status_code;
typedef struct evmc_vm evmc_vm;

// Status codes
enum {
  EVMC_SUCCESS                      = 0,
  EVMC_FAILURE                      = 1,
  EVMC_REVERT                       = 2,
  EVMC_OUT_OF_GAS                   = 3,
  EVMC_INVALID_INSTRUCTION          = 4,
  EVMC_UNDEFINED_INSTRUCTION        = 5,
  EVMC_STACK_OVERFLOW               = 6,
  EVMC_STACK_UNDERFLOW              = 7,
  EVMC_BAD_JUMP_DESTINATION         = 8,
  EVMC_INVALID_MEMORY_ACCESS        = 9,
  EVMC_CALL_DEPTH_EXCEEDED          = 10,
  EVMC_STATIC_MODE_VIOLATION        = 11,
  EVMC_PRECOMPILE_FAILURE           = 12,
  EVMC_CONTRACT_VALIDATION_FAILURE  = 13,
  EVMC_ARGUMENT_OUT_OF_RANGE        = 14,
  EVMC_WASM_UNREACHABLE_INSTRUCTION = 15,
  EVMC_WASM_TRAP                    = 16,
  EVMC_INSUFFICIENT_BALANCE         = 17,
  EVMC_INTERNAL_ERROR               = -1,
  EVMC_REJECTED                     = -2,
  EVMC_OUT_OF_MEMORY                = -3
};

// Storage status codes for the host
enum {
  EVMC_STORAGE_UNCHANGED      = 0,
  EVMC_STORAGE_MODIFIED       = 1,
  EVMC_STORAGE_MODIFIED_AGAIN = 2,
  EVMC_STORAGE_ADDED          = 3,
  EVMC_STORAGE_DELETED        = 4
};

// Simplified result structure
struct evmc_result {
  evmc_status_code status_code;
  int64_t          gas_left;
  int64_t          gas_refund; // Gas refund from the execution
  uint8_t*         output_data;
  size_t           output_size;
  void*            release;
  void*            context;
};

// Simplified message structure
struct evmc_message {
  int            kind;
  int            flags;
  int            depth;
  int64_t        gas;
  evmc_address   destination;
  evmc_address   sender;
  const uint8_t* input_data;
  size_t         input_size;
  evmc_bytes32   value;
  evmc_bytes32   create2_salt;
  evmc_address   code_address;
};

// Host interface structure
struct evmc_host_interface {
  // Account existence check
  bool (*account_exists)(void* context, const evmc_address* addr);

  // Get storage value
  evmc_bytes32 (*get_storage)(void* context, const evmc_address* addr, const evmc_bytes32* key);

  // Set storage value
  int (*set_storage)(void* context, const evmc_address* addr, const evmc_bytes32* key, const evmc_bytes32* value);

  // Get account balance
  evmc_bytes32 (*get_balance)(void* context, const evmc_address* addr);

  // Get code size
  size_t (*get_code_size)(void* context, const evmc_address* addr);

  // Get code hash
  evmc_bytes32 (*get_code_hash)(void* context, const evmc_address* addr);

  // Copy code
  size_t (*copy_code)(void* context, const evmc_address* addr, size_t code_offset, uint8_t* buffer_data, size_t buffer_size);

  // Selfdestruct
  void (*selfdestruct)(void* context, const evmc_address* addr, const evmc_address* beneficiary);

  // Call
  void (*call)(void* context, const struct evmc_message* msg, const uint8_t* code, size_t code_size, struct evmc_result* result);

  // Get tx context
  evmc_bytes32 (*get_tx_context)(void* context);

  // Get block hash
  evmc_bytes32 (*get_block_hash)(void* context, int64_t number);

  // Emit log
  void (*emit_log)(void* context, const evmc_address* addr, const uint8_t* data, size_t data_size, const evmc_bytes32 topics[], size_t topics_count);

  // Access account
  void (*access_account)(void* context, const evmc_address* addr);

  // Access storage
  void (*access_storage)(void* context, const evmc_address* addr, const evmc_bytes32* key);
};

// Function declarations
evmc_vm* evmc_create_evmone();

struct evmc_result evmone_execute(struct evmc_vm*                   vm,
                                  const struct evmc_host_interface* host,
                                  void*                             context,
                                  int                               revision,
                                  const struct evmc_message*        msg,
                                  const uint8_t*                    code,
                                  size_t                            code_size);

#ifdef __cplusplus
}
#endif

#endif /* EVMONE_C_WRAPPER_H */
