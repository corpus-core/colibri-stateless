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

#ifndef C4_PROVER_H
#define C4_PROVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../util/chains.h"
#include "../util/state.h"

// : APIs

// :: Internal APIs

// ::: prover.h
// The prover API is used to create proofs for a given method and parameters.
//
// Example:
//
// ```c
// prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"latest\", false]", chain_id, C4_PROVER_FLAG_INCLUDE_CODE);
//
// // Execute prover in a loop:
// data_request_t* data_request = NULL;
// bytes_t proof = {0};
// while (true) {
//   switch (c4_prover_execute(ctx)) {
//     case C4_SUCCESS:
//       proof = bytes_dup(ctx->proof);
//       break;
//     case C4_PENDING:
//       while ((data_request = c4_state_get_pending_request(&ctx->state)))
//          fetch_data(data_request);
//       break;
//     case C4_ERROR:
//       printf("Error: %s\n", ctx->state.error);
//       break;
//   }
// }
// c4_prover_free(ctx);
// ```

/**
 * a bitmask holding flags used during the prover context.
 */
typedef enum {
  C4_PROVER_FLAG_INCLUDE_CODE       = 1 << 0, // includes the code of the contracts when creating the proof for eth_call, otherwise the verifier will need to fetch and cache the code as needed
  C4_PROVER_FLAG_UV_SERVER_CTX      = 1 << 1, // the proofser is running in a UV-server and if the we expect cpu-intensice operations, we should return pending after setting the C4_PROVER_FLAG_UV_WORKER_REQUIRED flag.
  C4_PROVER_FLAG_UV_WORKER_REQUIRED = 1 << 2, // requests the proof execution to run in a worker thread instead of the main eventloop.
  C4_PROVER_FLAG_CHAIN_STORE        = 1 << 3, // allows the prover to use internal request with data from the chain stroe
  C4_PROVER_FLAG_UNSTABLE_LATEST    = 1 << 4, // usually we use latest-1, but if this is set we return the real "latest"
  C4_PROVER_FLAG_INCLUDE_SYNC       = 1 << 5, // if true, the sync data will be included in the proof (requires the client_state to be set)
  C4_PROVER_FLAG_USE_ACCESSLIST     = 1 << 6, // if true, eth_call will use eth_createAccessList instead of eth_debug_traceCall

} prover_flag_types_t;

/**
 * a bitmask holding flags used during the prover context.
 */
typedef uint32_t prover_flags_t;

#ifdef PROVER_CACHE

// Warning: Cache implementation assumes single-threaded access via libuv event loop.
// Multi-threaded usage requires external synchronization.
#if defined(PROVER_CACHE) && !defined(HTTP_SERVER)
#warning "PROVER_CACHE without HTTP_SERVER may have thread-safety issues. Consider using with libuv-based HTTP_SERVER."
#endif

typedef void (*cache_free_cb)(void*);
typedef struct cache_entry {
  bytes32_t           key;       // cache key
  void*               value;     // cache value
  uint32_t            size;      // cache value size in order to
  uint64_t            timestamp; // cache timestamp to be removed after ttl. if this timestamp is 0. the entry will live only in the prover_ctx, otherwise it will be stored in the global cache when prover_free is called.
  cache_free_cb       free;      // the function to free the value.
  uint32_t            use_counter;
  struct cache_entry* next;              // next cache entry
  bool                from_global_cache; // previous cache entry
} cache_entry_t;
#endif

/**
 * a struct holding the prover context.
 */
#ifdef PROVER_TRACE
// Forward declaration for pointer fields
typedef struct prover_trace_span prover_trace_span_t;
#endif
typedef struct {
  char*          method;       // rpc-method
  json_t         params;       // rpc- params
  bytes_t        proof;        // result or proof as bytes
  chain_id_t     chain_id;     // target chain
  c4_state_t     state;        // prover ctx state, holding errors and requests.
  prover_flags_t flags;        // prover flags
  bytes_t        client_state; // optional client_state representing the synced periods and trusted blockhashes
  bytes_t        witness_key;  // witness key for the prover
#ifdef PROVER_CACHE
  cache_entry_t* cache; // cache for the prover (only active in the server context)
#endif
#ifdef HTTP_SERVER
  uint32_t client_type; // client type for the prover (for beacon API only)
#endif
#ifdef PROVER_TRACE
  // Collected finished spans (consumed by server); and currently open span
  prover_trace_span_t* trace_spans;
  prover_trace_span_t* trace_open;
#endif
} prover_ctx_t;

/**
 * create a new prover context
 * @param method the rpc-method to proof (required, cannot be NULL)
 * @param params the rpc-params to proof (optional, defaults to "[]" if NULL)
 * @param chain_id the target chain
 * @param flags the prover flags
 * @return the prover context, which needs to get freed with c4_prover_free.
 *         Always returns a valid context - check ctx->state.error for validation errors.
 */
prover_ctx_t* c4_prover_create(char* method, char* params, chain_id_t chain_id, prover_flags_t flags);

/**
 * cleanup for the ctx
 * @param ctx the prover context
 */
void c4_prover_free(prover_ctx_t* ctx);

/**
 * tries to create the proof, but if there are pending requests, they need to fetched before calling it again.
 * This function should be called until it returns C4_SUCCESS or C4_ERROR.
 * @param ctx the prover context
 * @return the status of the prover
 */
c4_status_t c4_prover_execute(prover_ctx_t* ctx);

/**
 * returns the status of the prover
 * @param ctx the prover context
 * @return the status of the prover
 */
c4_status_t c4_prover_status(prover_ctx_t* ctx);

#ifdef PROVER_CACHE
/**
 * Retrieve a cached value by key. First checks local cache, then global cache.
 * If found in global cache, copies entry to local cache for thread-safety.
 * @param ctx the prover context
 * @param key 32-byte cache key
 * @return read-only pointer to cached value, or NULL if not found.
 *         Caller MUST NOT modify the returned data.
 *         Pointer is valid until cache cleanup or context destruction.
 */
const void* c4_prover_cache_get(prover_ctx_t* ctx, bytes32_t key);

/**
 * Store a value in the local cache. Will be moved to global cache on context destruction
 * if duration_ms > 0.
 * @param ctx the prover context
 * @param key 32-byte cache key
 * @param value pointer to the value to cache (ownership transferred)
 * @param size size of the cached value in bytes
 * @param duration_ms cache TTL in milliseconds (0 = local-only, never moved to global)
 * @param free function to free the value when cache entry is removed
 */
void c4_prover_cache_set(prover_ctx_t* ctx, bytes32_t key, void* value, uint32_t size, uint64_t duration_ms, cache_free_cb free);

/**
 * Clean up expired entries from global cache and enforce size limits.
 * Removes entries that are expired OR would exceed size limit (unless in use).
 * @param now current timestamp in milliseconds
 * @param extra_size additional size to reserve (for new entries)
 */
void c4_prover_cache_cleanup(uint64_t now, uint64_t extra_size);

/**
 * Invalidate a cache entry by key (marks as expired).
 * @param key 32-byte cache key to invalidate
 */
void c4_prover_cache_invalidate(bytes32_t key);

/**
 * Get statistics about the global cache.
 * @param entries number of entries in global cache
 * @param size current total size of cached data in bytes
 * @param max_size maximum allowed cache size in bytes
 * @param capacity current allocated capacity of the cache array
 */
void c4_prover_cache_stats(uint64_t* entries, uint64_t* size, uint64_t* max_size, uint64_t* capacity);
#endif

// Time helpers made available to headers that need them
uint64_t current_ms();
uint64_t current_unix_ms();

#ifdef PROVER_TRACE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct prover_trace_kv {
  char*                   key;
  char*                   value; // stringified
  struct prover_trace_kv* next;
} prover_trace_kv_t;

struct prover_trace_span {
  char*                     name;
  uint64_t                  start_ms;
  uint64_t                  duration_ms;
  prover_trace_kv_t*        tags;
  struct prover_trace_span* next;
};

static inline void prover_trace_start(prover_ctx_t* ctx, const char* name) {
  if (!ctx || !name) return;
  uint64_t start_ms = current_unix_ms();
  if (ctx->trace_open) {
    ctx->trace_open->duration_ms = start_ms - ctx->trace_open->start_ms;
    ctx->trace_open->next        = ctx->trace_spans;
    ctx->trace_spans             = ctx->trace_open;
    ctx->trace_open              = NULL;
  }
  struct prover_trace_span* s = (struct prover_trace_span*) safe_calloc(sizeof(struct prover_trace_span), 1);
  s->name                     = strdup(name);
  s->start_ms                 = start_ms;
  ctx->trace_open             = s;
}

static inline void prover_trace_add_str(prover_ctx_t* ctx, const char* key, const char* value) {
  if (!ctx || !ctx->trace_open || !key || !value) return;
  prover_trace_kv_t* kv = (prover_trace_kv_t*) safe_malloc(sizeof(prover_trace_kv_t));
  kv->key               = strdup(key);
  kv->value             = strdup(value);
  kv->next              = ctx->trace_open->tags;
  ctx->trace_open->tags = kv;
}

static inline void prover_trace_add_u64(prover_ctx_t* ctx, const char* key, uint64_t value) {
  if (!ctx || !ctx->trace_open || !key) return;
  char buf[32];
  // Print as unsigned long long to be portable, but stored as string
  snprintf(buf, sizeof(buf), "%llu", (unsigned long long) value);
  prover_trace_add_str(ctx, key, buf);
}

static inline void prover_trace_end(prover_ctx_t* ctx) {
  if (!ctx || !ctx->trace_open) return;
  ctx->trace_open->duration_ms = current_unix_ms() - ctx->trace_open->start_ms;
  ctx->trace_open->next        = ctx->trace_spans;
  ctx->trace_spans             = ctx->trace_open;
  ctx->trace_open              = NULL;
}

#define TRACE_START(ctx, name)      prover_trace_start((ctx), (name))
#define TRACE_ADD_UINT64(ctx, k, v) prover_trace_add_u64((ctx), (k), (v))
#define TRACE_ADD_STR(ctx, k, v)    prover_trace_add_str((ctx), (k), (v))
#define TRACE_END(ctx)              prover_trace_end((ctx))
#else
#define TRACE_START(ctx, name) \
  do {                         \
  } while (0)
#define TRACE_ADD_UINT64(ctx, k, v) \
  do {                              \
  } while (0)
#define TRACE_ADD_STR(ctx, k, v) \
  do {                           \
  } while (0)
#define TRACE_END(ctx) \
  do {                 \
  } while (0)
#endif

/**
 * Macro to request execution in a worker thread for CPU-intensive operations.
 *
 * This macro should be used before computationally expensive operations that would
 * block the libuv event loop. It sets the C4_PROVER_FLAG_UV_WORKER_REQUIRED flag
 * and returns C4_PENDING to signal that the operation should be retried in a worker thread.
 *
 * IMPORTANT: All required cache entries MUST be fetched using c4_prover_cache_get()
 * BEFORE calling this macro, as cache access from worker threads is restricted to
 * prevent race conditions.
 *
 * @param ctx the prover context
 * @param cleanup optional cleanup code to execute before returning
 *
 * Usage:
 *   // Fetch all needed cache data first
 *   merkle_tree_t* tree = c4_prover_cache_get(ctx, tree_key);
 *
 *   if (tree == NULL) {
 *     // Request worker thread for heavy computation
 *     REQUEST_WORKER_THREAD(ctx);
 *
 *     tree = build_merkle_tree(...);
 *   }
 *
 *   // Now safe to do CPU-intensive work...
 */
#define REQUEST_WORKER_THREAD_CATCH(ctx, cleanup)                                                       \
  if (ctx->flags & C4_PROVER_FLAG_UV_SERVER_CTX && !(ctx->flags & C4_PROVER_FLAG_UV_WORKER_REQUIRED)) { \
    ctx->flags |= C4_PROVER_FLAG_UV_WORKER_REQUIRED;                                                    \
    cleanup;                                                                                            \
    return C4_PENDING;                                                                                  \
  }

/**
 * Simplified version of REQUEST_WORKER_THREAD_CATCH without cleanup code.
 * @param ctx the prover context
 */
#define REQUEST_WORKER_THREAD(ctx) REQUEST_WORKER_THREAD_CATCH(ctx, );

#ifdef __cplusplus
}
#endif

#endif