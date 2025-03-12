/**
 * C wrapper for evmone library
 */
#ifndef EVMONE_C_WRAPPER_H
#define EVMONE_C_WRAPPER_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* For WASM builds, define the necessary types inline to avoid dependency on evmc/evmc.h */
#ifdef EVMONE_WASM_BUILD

/* Basic EVMC types for WASM build */
typedef struct {
  uint8_t bytes[20];
} evmc_address;
typedef struct {
  uint8_t bytes[32];
} evmc_bytes32;

#else
/* Include the actual EVMC types for non-WASM builds */
#include <evmc/evmc.h>
#endif

/* Result structure */
typedef struct evmone_result {
  int                 status_code;
  uint64_t            gas_left;
  uint64_t            gas_refund;
  const uint8_t*      output_data;
  size_t              output_size;
  void*               release_callback; /* Function pointer to release resources */
  void*               release_context;  /* Context for the release callback */
  const evmc_address* create_address;
} evmone_result;

/* Message structure */
typedef struct evmone_message {
  enum { EVMONE_CALL,
         EVMONE_DELEGATECALL,
         EVMONE_CALLCODE,
         EVMONE_CREATE,
         EVMONE_CREATE2 } kind;
  bool           is_static;
  int32_t        depth;
  int64_t        gas;
  evmc_address   destination;
  evmc_address   sender;
  const uint8_t* input_data;
  size_t         input_size;
  evmc_bytes32   value;
  evmc_bytes32   create_salt;
  uint64_t       code_address; /* Used for code identification */
} evmone_message;

/* Storage status enum - maps to EVMC's storage statuses */
typedef enum {
  EVMONE_STORAGE_UNCHANGED,
  EVMONE_STORAGE_MODIFIED,
  EVMONE_STORAGE_MODIFIED_AGAIN,
  EVMONE_STORAGE_ADDED,
  EVMONE_STORAGE_DELETED
} evmone_storage_status;

/* Host context callbacks */
typedef bool (*evmone_account_exists_fn)(void* context, const evmc_address* addr);
typedef evmc_bytes32 (*evmone_get_storage_fn)(void* context, const evmc_address* addr, const evmc_bytes32* key);
typedef evmone_storage_status (*evmone_set_storage_fn)(void* context, const evmc_address* addr, const evmc_bytes32* key, const evmc_bytes32* value);
typedef evmc_bytes32 (*evmone_get_balance_fn)(void* context, const evmc_address* addr);
typedef size_t (*evmone_get_code_size_fn)(void* context, const evmc_address* addr);
typedef evmc_bytes32 (*evmone_get_code_hash_fn)(void* context, const evmc_address* addr);
typedef size_t (*evmone_copy_code_fn)(void* context, const evmc_address* addr, size_t code_offset, uint8_t* buffer_data, size_t buffer_size);
typedef void (*evmone_selfdestruct_fn)(void* context, const evmc_address* addr, const evmc_address* beneficiary);
typedef void (*evmone_call_fn)(void* context, const struct evmone_message* msg, const uint8_t* code, size_t code_size, struct evmone_result* result);
typedef evmc_bytes32 (*evmone_get_tx_context_fn)(void* context);
typedef evmc_bytes32 (*evmone_get_block_hash_fn)(void* context, int64_t number);
typedef void (*evmone_emit_log_fn)(void* context, const evmc_address* addr, const uint8_t* data, size_t data_size, const evmc_bytes32 topics[], size_t topic_count);
typedef void (*evmone_access_account_fn)(void* context, const evmc_address* addr);
typedef void (*evmone_access_storage_fn)(void* context, const evmc_address* addr, const evmc_bytes32* key);

/* Host interface */
typedef struct evmone_host_interface {
  evmone_account_exists_fn account_exists;
  evmone_get_storage_fn    get_storage;
  evmone_set_storage_fn    set_storage;
  evmone_get_balance_fn    get_balance;
  evmone_get_code_size_fn  get_code_size;
  evmone_get_code_hash_fn  get_code_hash;
  evmone_copy_code_fn      copy_code;
  evmone_selfdestruct_fn   selfdestruct;
  evmone_call_fn           call;
  evmone_get_tx_context_fn get_tx_context;
  evmone_get_block_hash_fn get_block_hash;
  evmone_emit_log_fn       emit_log;
  evmone_access_account_fn access_account;
  evmone_access_storage_fn access_storage;
  // Note: get_transient_storage and set_transient_storage are not included in this interface
  // but are supported in the C++ adapter
} evmone_host_interface;

/* Create EVM executor instance */
void* evmone_create_executor();

/* Destroy EVM executor instance */
void evmone_destroy_executor(void* executor);

/* Execute EVM code */
evmone_result evmone_execute(
    void*                        executor,
    const evmone_host_interface* host_interface,
    void*                        host_context,
    int                          revision,
    const evmone_message*        msg,
    const uint8_t*               code,
    size_t                       code_size);

/* Release result resources */
void evmone_release_result(evmone_result* result);

#ifdef __cplusplus
}
#endif

#endif /* EVMONE_C_WRAPPER_H */
