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
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef ETH_CALL_CACHE_H
#define ETH_CALL_CACHE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "call_ctx.h"
#include "verify.h"

/**
 * PAP (Pragmatic Adaptive Privacy) state for a single `eth_call` / `eth_estimateGas`
 * / `colibri_simulateTransaction` verification cycle.
 *
 * Stored in `verify_ctx_t.user_data` so the cache list and the last successful EVM
 * result survive across multiple `C4_PENDING` rounds without re-reading from disk.
 */
typedef struct {
  cached_account_t* accounts;    /**< Linked list of cached accounts (owned). */
  bytes_t           call_result; /**< Return data from the last successful EVM run. */
  emitted_log_t*    logs;        /**< Emitted events from the last successful EVM run. */
  uint64_t          gas_used;    /**< Gas consumed by the last successful EVM run. */
  bool              evm_done;    /**< `true` once EVM has run with all storage present. */
} pap_call_state_t;

// :: Serialization helpers

/**
 * Serializes a single `cached_account_t` (without following `next`) into `out`.
 *
 * Binary layout:
 * - `[8 B LE]` verified_at
 * - `[32 B]` storage_root, `[32 B]` balance, `[32 B]` code_hash
 * - `[4 B LE]` num_storage
 * - Per storage entry: `[32 B]` key + `[32 B]` value + `[8 B LE]` verified_at
 *
 * @param out    destination buffer (appended to)
 * @param account source account to serialize
 */
void eth_call_cache_write(buffer_t* out, const cached_account_t* account);

/**
 * Deserializes one `cached_account_t` from `data` into `out`.
 *
 * Allocates `out->storage` on the heap; caller must call `eth_call_cache_free()`.
 * Does **not** touch `out->address` or `out->next`.
 *
 * @param data  source bytes
 * @param out   destination struct (must be zero-initialized by caller)
 * @return `true` on success, `false` if `data` is too short or malformed
 */
bool eth_call_cache_read(bytes_t data, cached_account_t* out);

// :: Cache load / save

/**
 * Loads the cached account entry for `addr` on `ctx->chain_id` from the storage
 * plugin.
 *
 * @param ctx  verification context (provides chain_id and storage plugin)
 * @param addr 20-byte contract address
 * @return heap-allocated `cached_account_t` with `address` and `next` set to zero,
 *         or `NULL` if no entry exists in the cache
 */
cached_account_t* eth_call_cache_load(verify_ctx_t* ctx, const address_t addr);

/**
 * Serializes `account` and writes it to the storage plugin under the key
 * `"call_<chain_id>_<hex-addr>"`.
 *
 * @param ctx     verification context (provides chain_id and storage plugin)
 * @param addr    20-byte contract address
 * @param account account to persist (only this node, `next` is ignored)
 */
void eth_call_cache_save(verify_ctx_t* ctx, const address_t addr, cached_account_t* account);

// :: Linked-list helpers

/**
 * Searches `list` for an entry matching `addr`.
 *
 * @param list head of the linked list (may be `NULL`)
 * @param addr 20-byte address to look for
 * @return matching node or `NULL`
 */
cached_account_t* eth_call_cache_find(cached_account_t* list, const address_t addr);

/**
 * Returns the existing node for `addr` in `*list`, or allocates a new zeroed node,
 * prepends it, and returns it.
 *
 * @param list pointer to the head of the linked list
 * @param addr 20-byte address
 * @return always non-`NULL` (aborts on OOM via `safe_calloc`)
 */
cached_account_t* eth_call_cache_get_or_create(cached_account_t** list, const address_t addr);

// :: Storage slot helpers

/**
 * Looks up storage slot `key` inside `account->storage`.
 *
 * @param account source account
 * @param key     32-byte storage key
 * @param value_out written with the 32-byte value on success
 * @return `true` if found, `false` otherwise
 */
bool eth_call_cache_get_storage(const cached_account_t* account, const bytes32_t key, bytes32_t value_out);

/**
 * Sets or updates the storage slot `key` in `account`.
 *
 * If the slot already exists its `value` and `verified_at` are updated in-place.
 * Otherwise a new entry is appended via `safe_realloc`.
 *
 * @param account     target account (must be non-`NULL`)
 * @param key         32-byte storage key
 * @param value       32-byte storage value
 * @param verified_at block number when this value was verified (0 = unverified)
 */
void eth_call_cache_set_storage(cached_account_t* account, const bytes32_t key, const bytes32_t value, uint64_t verified_at);

// :: Access tracking helpers (not persisted to disk)

/**
 * Resets the `accessed` flag on every storage slot in `list`.
 *
 * Call this before each EVM run so Phase C only proves slots touched by the
 * current execution.
 *
 * @param list head of the linked list (may be `NULL`)
 */
void eth_call_cache_reset_accessed(cached_account_t* list);

/**
 * Sets the `accessed` flag on the storage slot identified by `key` in `account`.
 *
 * No-op if the key is not found.
 *
 * @param account target account
 * @param key     32-byte storage key
 */
void eth_call_cache_mark_accessed(cached_account_t* account, const bytes32_t key);

// :: Memory management

/**
 * Frees `account->storage` and then `account` itself.
 *
 * Does **not** follow `next` – the caller is responsible for iterating the list.
 *
 * @param account node to free (may be `NULL`)
 */
void eth_call_cache_free(cached_account_t* account);

/**
 * Frees every node in the linked list starting at `list`.
 *
 * @param list head of the list (may be `NULL`)
 */
void eth_call_cache_free_list(cached_account_t* list);

/**
 * Frees all resources owned by `state` and then `state` itself.
 *
 * Suitable as `verify_ctx_t.user_data_free` (cast to `void (*)(void*)`).
 *
 * @param state pointer to `pap_call_state_t` cast to `void*`
 */
void pap_call_state_free(void* state);

#ifdef __cplusplus
}
#endif

#endif /* ETH_CALL_CACHE_H */
