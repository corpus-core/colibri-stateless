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

#ifndef ETH_CALL_ACCOUNT_H
#define ETH_CALL_ACCOUNT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "bytes.h"
#include "crypto.h"
#include "verify.h"
#include <stdbool.h>
#include <string.h>

// :: Storage value source

typedef enum {
  STORAGE_SRC_NONE     = 0,
  STORAGE_SRC_PROOF    = 1,
  STORAGE_SRC_CACHE    = 2,
  STORAGE_SRC_OVERRIDE = 3,
  STORAGE_SRC_RPC      = 4,
} storage_source_t;

// :: Unified storage slot

typedef struct call_storage {
  bytes32_t            key;
  bytes32_t            src_value;
  bytes32_t            post_value;
  storage_source_t     source;
  uint64_t             verified_at;
  bool                 accessed;
  bool                 modified;
  struct call_storage* next;
} call_storage_t;

// :: Account flags

typedef enum {
  ACCOUNT_HAS_NONCE        = 1 << 0,
  ACCOUNT_HAS_BALANCE      = 1 << 1,
  ACCOUNT_HAS_CODE_HASH    = 1 << 2,
  ACCOUNT_HAS_STORAGE_ROOT = 1 << 3,
  ACCOUNT_HAS_CODE         = 1 << 4,
  ACCOUNT_FREE_CODE        = 1 << 5,
  ACCOUNT_FULL_STATE       = 1 << 6,
  ACCOUNT_DELETED          = 1 << 7,
  ACCOUNT_ACCESSED         = 1 << 8, // EIP-2929: account was accessed in this transaction
} call_account_flags_t;

// :: Unified account

typedef struct call_account {
  address_t            address;
  uint64_t             nonce;
  bytes32_t            balance;
  bytes32_t            code_hash;
  bytes32_t            storage_root;
  bytes_t              code;
  uint32_t             flags;
  uint64_t             verified_at;
  call_storage_t*      storage;
  struct call_account* next;
} call_account_t;

// :: Storage slot lookup (inline for hot-path performance)

/**
 * Searches the storage linked list of `acc` for the slot matching `key`.
 *
 * @param acc account whose storage to search (may be `NULL`)
 * @param key 32-byte storage key
 * @return matching slot or `NULL`
 */
static inline call_storage_t* call_storage_find(call_account_t* acc, const bytes32_t key) {
  if (!acc) return NULL;
  for (call_storage_t* s = acc->storage; s; s = s->next)
    if (memcmp(s->key, key, 32) == 0) return s;
  return NULL;
}

// :: Linked-list helpers

/**
 * Searches `list` for an entry matching `addr`.
 *
 * @param list head of the linked list (may be `NULL`)
 * @param addr 20-byte address to look for
 * @return matching node or `NULL`
 */
call_account_t* call_account_list_find(call_account_t* list, const address_t addr);

/**
 * Returns the existing node for `addr` in `*list`, or allocates a new zeroed node,
 * prepends it, and returns it.
 *
 * @param list pointer to the head of the linked list
 * @param addr 20-byte address
 * @return always non-`NULL` (aborts on OOM via `safe_calloc`)
 */
RETURNS_NONNULL call_account_t* call_account_list_get_or_create(call_account_t** list, const address_t addr);

// :: Memory management

void call_storage_free_list(call_storage_t* s);
void call_account_free(call_account_t* acc);
void call_account_free_list(call_account_t* list);

// :: Storage slot helpers

/**
 * Sets or updates the storage slot `key` in `account`.
 *
 * If the slot already exists its `src_value`, `post_value` and `verified_at` are updated.
 * Otherwise a new entry is prepended.
 *
 * @param account     target account (must be non-`NULL`)
 * @param key         32-byte storage key
 * @param value       32-byte storage value (written to both `src_value` and `post_value`)
 * @param source      where the value originated from
 * @param verified_at block number when this value was verified (0 = unverified)
 */
void call_account_set_storage(call_account_t* account, const bytes32_t key, const bytes32_t value, storage_source_t source, uint64_t verified_at);

// :: Access tracking helpers (not persisted to disk)

/**
 * Resets the `accessed` and `modified` flags on every storage slot in `list`,
 * and copies `src_value` back to `post_value`.
 *
 * Call this before each EVM run so that only slots touched by the current
 * execution are marked.
 *
 * @param list head of the linked list (may be `NULL`)
 */
void call_account_reset_accessed(call_account_t* list);

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
void eth_call_account_serialize(buffer_t* out, const call_account_t* account);

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
bool eth_call_account_deserialize(bytes_t data, call_account_t* out);

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
call_account_t* eth_call_account_cache_load(verify_ctx_t* ctx, const address_t addr);

/**
 * Serializes `account` and writes it to the storage plugin under the key
 * `"call_<chain_id>_<hex-addr>"`.
 *
 * @param ctx     verification context (provides chain_id and storage plugin)
 * @param addr    20-byte contract address
 * @param account account to persist (only this node, `next` is ignored)
 */
void eth_call_account_cache_save(verify_ctx_t* ctx, const address_t addr, call_account_t* account);

#ifdef __cplusplus
}
#endif

#endif /* ETH_CALL_ACCOUNT_H */
