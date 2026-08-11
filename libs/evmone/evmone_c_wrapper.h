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

/**
 * Stable Colibri revision IDs mapped to EVMC revisions inside the C++ wrapper.
 *
 * Do not pass raw `evmc_revision` numeric values from C: EVMC ABI 18 removed
 * explicit enumerator values, so Osaka is no longer safely hard-coded as 14.
 */
#define EVMONE_REV_OSAKA 0

/* Result structure (CREATE address is computed by the VM since EVMC ABI 18). */
typedef struct evmone_result {
  int            status_code;
  uint64_t       gas_left;
  uint64_t       gas_refund;
  const uint8_t* output_data;
  size_t         output_size;
  void*          release_callback; /* Function pointer to release resources */
  void*          release_context;  /* Context for the release callback */
} evmone_result;

/* Message structure */
typedef struct evmone_message {
  enum { EVMONE_CALL,
         EVMONE_DELEGATECALL,
         EVMONE_CALLCODE,
         EVMONE_CREATE,
         EVMONE_CREATE2 } kind;
  bool           is_static;
  bool           is_delegated; /* EIP-7702 delegated call flag (EVMC_DELEGATED) */
  int32_t        depth;
  int64_t        gas;
  evmc_address   destination;
  evmc_address   sender;
  const uint8_t* input_data;
  size_t         input_size;
  evmc_bytes32   value;
  evmc_address   code_address; /* Address of the code to execute (for DELEGATECALL) */
} evmone_message;

/* Storage status enum - must match evmc_storage_status values exactly */
typedef enum {
  EVMONE_STORAGE_ASSIGNED          = 0, // no-op or dirty re-write (catch-all)
  EVMONE_STORAGE_ADDED             = 1, // 0 -> 0 -> Z (clean zero to nonzero)
  EVMONE_STORAGE_DELETED           = 2, // X -> X -> 0 (clean nonzero to zero)
  EVMONE_STORAGE_MODIFIED          = 3, // X -> X -> Z (clean nonzero to other nonzero)
  EVMONE_STORAGE_DELETED_ADDED     = 4, // X -> 0 -> Z (dirty zero, original != 0)
  EVMONE_STORAGE_MODIFIED_DELETED  = 5, // X -> Y -> 0 (dirty nonzero to zero, original != 0)
  EVMONE_STORAGE_DELETED_RESTORED  = 6, // X -> 0 -> X (dirty zero to original)
  EVMONE_STORAGE_ADDED_DELETED     = 7, // 0 -> Y -> 0 (dirty nonzero to zero, original == 0)
  EVMONE_STORAGE_MODIFIED_RESTORED = 8  // X -> Y -> X (dirty nonzero to original)
} evmone_storage_status;

/* Transaction context passed from host to EVM for opcodes like ORIGIN, NUMBER, TIMESTAMP, etc. */
typedef struct evmone_tx_context {
  evmc_bytes32        tx_gas_price;
  evmc_address        tx_origin;
  evmc_address        block_coinbase;
  int64_t             block_number;
  int64_t             block_timestamp;
  int64_t             block_gas_limit;
  evmc_bytes32        block_prev_randao;
  evmc_bytes32        chain_id;
  evmc_bytes32        block_base_fee;
  evmc_bytes32        blob_base_fee;
  const evmc_bytes32* blob_hashes;       /* EIP-4844; pointers must outlive execute() */
  size_t              blob_hashes_count; /* EIP-4844 */
  uint64_t            block_slot_number; /* EIP-7843; unused until Amsterdam activation */
} evmone_tx_context;

/* Access status returned by access_account / access_storage (EIP-2929) */
#define EVMONE_ACCESS_COLD 0
#define EVMONE_ACCESS_WARM 1

/* Host context callbacks */
typedef bool (*evmone_account_exists_fn)(void* context, const evmc_address* addr);
typedef evmc_bytes32 (*evmone_get_storage_fn)(void* context, const evmc_address* addr, const evmc_bytes32* key);
typedef evmone_storage_status (*evmone_set_storage_fn)(void* context, const evmc_address* addr, const evmc_bytes32* key, const evmc_bytes32* value);
typedef evmc_bytes32 (*evmone_get_balance_fn)(void* context, const evmc_address* addr);
typedef uint64_t (*evmone_get_nonce_fn)(void* context, const evmc_address* addr);
typedef size_t (*evmone_get_code_size_fn)(void* context, const evmc_address* addr);
typedef evmc_bytes32 (*evmone_get_code_hash_fn)(void* context, const evmc_address* addr);
typedef size_t (*evmone_copy_code_fn)(void* context, const evmc_address* addr, size_t code_offset, uint8_t* buffer_data, size_t buffer_size);
typedef void (*evmone_selfdestruct_fn)(void* context, const evmc_address* addr, const evmc_address* beneficiary);
typedef void (*evmone_call_fn)(void* context, const struct evmone_message* msg, const uint8_t* code, size_t code_size, struct evmone_result* result);
typedef void (*evmone_get_tx_context_fn)(void* context, struct evmone_tx_context* result);
typedef evmc_bytes32 (*evmone_get_block_hash_fn)(void* context, int64_t number);
typedef void (*evmone_emit_log_fn)(void* context, const evmc_address* addr, const uint8_t* data, size_t data_size, const evmc_bytes32 topics[], size_t topic_count);
typedef int (*evmone_access_account_fn)(void* context, const evmc_address* addr);
typedef int (*evmone_access_storage_fn)(void* context, const evmc_address* addr, const evmc_bytes32* key);
typedef evmc_bytes32 (*evmone_get_transient_storage_fn)(void* context, const evmc_address* addr, const evmc_bytes32* key);
typedef void (*evmone_set_transient_storage_fn)(void* context, const evmc_address* addr, const evmc_bytes32* key, const evmc_bytes32* value);

/* Host interface */
typedef struct evmone_host_interface {
  evmone_account_exists_fn        account_exists;
  evmone_get_storage_fn           get_storage;
  evmone_set_storage_fn           set_storage;
  evmone_get_balance_fn           get_balance;
  evmone_get_nonce_fn             get_nonce;
  evmone_get_code_size_fn         get_code_size;
  evmone_get_code_hash_fn         get_code_hash;
  evmone_copy_code_fn             copy_code;
  evmone_selfdestruct_fn          selfdestruct;
  evmone_call_fn                  call;
  evmone_get_tx_context_fn        get_tx_context;
  evmone_get_block_hash_fn        get_block_hash;
  evmone_emit_log_fn              emit_log;
  evmone_access_account_fn        access_account;
  evmone_access_storage_fn        access_storage;
  evmone_get_transient_storage_fn get_transient_storage;
  evmone_set_transient_storage_fn set_transient_storage;
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

/**
 * Callback invoked for every KECCAK256 opcode during EVM execution.
 *
 * All pointers are only valid for the duration of the callback.
 *
 * @param context  Opaque pointer set via `evmone_set_keccak_hook`.
 * @param data     Pointer to the raw input bytes hashed by the opcode.
 * @param size     Length of `data` in bytes.
 * @param hash     Pointer to the 32-byte Keccak-256 result.
 */
typedef void (*evmone_keccak_fn)(void* context, const uint8_t* data,
                                 size_t size, const uint8_t* hash);

/**
 * Install a thread-local hook that fires on every `ethash_keccak256` call
 * (i.e. the EVM KECCAK256 opcode). Pass `NULL` to clear the hook.
 *
 * The hook is stored in thread-local storage, so each worker thread can
 * have its own independent callback.
 *
 * @param fn   Callback function, or NULL to clear.
 * @param ctx  Opaque context forwarded to `fn`.
 */
void evmone_set_keccak_hook(evmone_keccak_fn fn, void* ctx);

#ifdef __cplusplus
}
#endif

#endif /* EVMONE_C_WRAPPER_H */
