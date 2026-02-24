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

// :: Serialization helpers

/**
 * Serializes a single `call_account_t` (without following `next`) into `out`.
 *
 * Binary layout (version 1):
 * - `[1 B]` version (0x01)
 * - `[4 B LE]` flags
 * - `[8 B LE]` nonce
 * - `[8 B LE]` verified_at
 * - `[32 B]` storage_root, `[32 B]` balance, `[32 B]` code_hash
 * - `[4 B LE]` num_storage
 * - Per storage entry: `[32 B]` key + `[32 B]` value + `[1 B]` source + `[8 B LE]` verified_at
 *
 * @param out     destination buffer (appended to)
 * @param account source account to serialize
 */
void eth_call_cache_write(buffer_t* out, const call_account_t* account);

/**
 * Deserializes one `call_account_t` from `data` into `out`.
 *
 * Allocates `out->storage` on the heap; caller must call `call_account_free()`.
 * Does **not** touch `out->address` or `out->next`.
 *
 * @param data source bytes
 * @param out  destination struct (must be zero-initialized by caller)
 * @return `true` on success, `false` if `data` is too short or malformed
 */
bool eth_call_cache_read(bytes_t data, call_account_t* out);

// :: Cache load / save

/**
 * Loads the cached account entry for `addr` on `ctx->chain_id` from the storage
 * plugin.
 *
 * @param ctx  verification context (provides chain_id and storage plugin)
 * @param addr 20-byte contract address
 * @return heap-allocated `call_account_t` with `address` set,
 *         or `NULL` if no entry exists in the cache
 */
call_account_t* eth_call_cache_load(verify_ctx_t* ctx, const address_t addr);

/**
 * Serializes `account` and writes it to the storage plugin under the key
 * `"call_<chain_id>_<hex-addr>"`.
 *
 * @param ctx     verification context (provides chain_id and storage plugin)
 * @param addr    20-byte contract address
 * @param account account to persist (only this node, `next` is ignored)
 */
void eth_call_cache_save(verify_ctx_t* ctx, const address_t addr, call_account_t* account);

// :: Linked-list helpers

/**
 * Searches `list` for an entry matching `addr`.
 *
 * @param list head of the linked list (may be `NULL`)
 * @param addr 20-byte address to look for
 * @return matching node or `NULL`
 */
call_account_t* eth_call_cache_find(call_account_t* list, const address_t addr);

/**
 * Returns the existing node for `addr` in `*list`, or allocates a new zeroed node,
 * prepends it, and returns it.
 *
 * @param list pointer to the head of the linked list
 * @param addr 20-byte address
 * @return always non-`NULL` (aborts on OOM via `safe_calloc`)
 */
call_account_t* eth_call_cache_get_or_create(call_account_t** list, const address_t addr);

// :: Storage slot helpers

/**
 * Looks up storage slot `key` inside `account->storage`.
 *
 * @param account source account
 * @param key     32-byte storage key
 * @param value_out written with the 32-byte `post_value` on success
 * @return `true` if found, `false` otherwise
 */
bool eth_call_cache_get_storage(const call_account_t* account, const bytes32_t key, bytes32_t value_out);

/**
 * Sets or updates the storage slot `key` in `account`.
 *
 * If the slot already exists its `src_value`, `post_value` and `verified_at` are updated.
 * Otherwise a new entry is appended.
 *
 * @param account     target account (must be non-`NULL`)
 * @param key         32-byte storage key
 * @param value       32-byte storage value (written to both `src_value` and `post_value`)
 * @param source      where the value originated from
 * @param verified_at block number when this value was verified (0 = unverified)
 */
void eth_call_cache_set_storage(call_account_t* account, const bytes32_t key, const bytes32_t value, storage_source_t source, uint64_t verified_at);

// :: Access tracking helpers (not persisted to disk)

/**
 * Resets the `accessed` flag on every storage slot in `list`.
 *
 * Call this before each EVM run so Phase C only proves slots touched by the
 * current execution.
 *
 * @param list head of the linked list (may be `NULL`)
 */
void eth_call_cache_reset_accessed(call_account_t* list);

#ifdef __cplusplus
}
#endif

#endif /* ETH_CALL_CACHE_H */
