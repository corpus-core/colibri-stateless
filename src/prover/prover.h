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
 * Prover options and runtime flags stored in `prover_ctx_t.flags`.
 *
 * Combine with bitwise OR when calling `c4_prover_create`. Some flags are set
 * internally during execution (e.g. `C4_PROVER_FLAG_UV_WORKER_REQUIRED`,
 * `C4_PROVER_FLAG_INPUT_VALIDATED`); those must not be treated as public request options.
 *
 * Per-flag semantics are documented on each enumerator below.
 */
typedef enum {
  C4_PROVER_FLAG_INCLUDE_CODE       = 1 << 0,  // includes the code of the contracts when creating the proof for eth_call, otherwise the verifier will need to fetch and cache the code as needed
  C4_PROVER_FLAG_UV_SERVER_CTX      = 1 << 1,  // the proofser is running in a UV-server and if the we expect cpu-intensice operations, we should return pending after setting the C4_PROVER_FLAG_UV_WORKER_REQUIRED flag.
  C4_PROVER_FLAG_UV_WORKER_REQUIRED = 1 << 2,  // requests the proof execution to run in a worker thread instead of the main eventloop.
  C4_PROVER_FLAG_CHAIN_STORE        = 1 << 3,  // allows the prover to use internal request with data from the chain stroe
  C4_PROVER_FLAG_UNSTABLE_LATEST    = 1 << 4,  // usually we use latest-1, but if this is set we return the real "latest"
  C4_PROVER_FLAG_INCLUDE_SYNC       = 1 << 5,  // if true, the sync data will be included in the proof (requires the client_state to be set)
  C4_PROVER_FLAG_USE_DEBUG_TRACE    = 1 << 6,  // if true, eth_call uses legacy debug_traceCall (prestateTracer) instead of the default eth_createAccessList
  C4_PROVER_FLAG_ZK_PROOF           = 1 << 7,  // if true, the the prover will try to store the zk_proof within the sync_section
  C4_PROVER_FLAG_CALL_BLOCK_CONTEXT = 1 << 8,  // unused: eth_call now reads EVM block context from the verified RLP EL header
  C4_PROVER_FLAG_HYBRID             = 1 << 9,  // hybrid mode: header proof from remote server, execution data from RPC provider
  C4_PROVER_FLAG_PROXY              = 1 << 10, // server: request used client-supplied RPC/Beacon URLs (proxy mode)
  C4_PROVER_FLAG_LIGHT_CLIENT       = 1 << 11, // light client mode: extended header cache TTL for "latest" (full block_time instead of half)
  C4_PROVER_FLAG_LOGS_COMPLETENESS  = 1 << 12, // if true, eth_getLogs generates a completeness proof over the requested block range (proves no matching log was omitted)
  C4_PROVER_FLAG_INPUT_VALIDATED    = 1 << 13, // internal/transient: set once the request input params have been validated (see CHECK_JSON_INPUT). Prevents re-validation on async re-entries and nested dispatch. Not a request option and never serialized.
  C4_PROVER_FLAG_NIMBUS             = 1 << 14, // Nimbus CL compatibility: find child headers via slot scan instead of `headers?parent_root=` (status-im/nimbus-eth2#7305) and use the Nimbus historical_summaries URL.
  C4_PROVER_FLAG_LODESTAR           = 1 << 15, // Lodestar CL compatibility: enables the unofficial `/eth/v0/beacon/proof/state/{state_id}` (CompactMultiProof) endpoint used as a self-build fallback for LightClientBootstrap and Gloas LightClientUpdate when the standard endpoints do not return data. Must be OFF for non-Lodestar beacon clients so the fallback is never attempted.
} prover_flag_types_t;

/**
 * Bitmask of `prover_flag_types_t` values stored in `prover_ctx_t.flags`.
 */
typedef uint32_t prover_flags_t;

/**
 * **CHECK_JSON_INPUT(val, def, error_prefix)** - Validate request input params once per context.
 *
 * Validates `val` against the schema `def`, but only if the input parameters have not been
 * validated yet for this prover context. On success it sets `C4_PROVER_FLAG_INPUT_VALIDATED`
 * so async re-entries (repeated `C4_PENDING` executions) and nested dispatch skip re-validation.
 *
 * Prefer this over `CHECK_JSON_CACHED` for request parameters: it avoids hashing the payload on
 * every call and instead relies on a single per-context flag. `CHECK_JSON_CACHED` remains the
 * right choice for large *results* (e.g. `eth_getBlockReceipts`) which are validated once but are
 * not the request input. Assumes a `prover_ctx_t* ctx` is in scope.
 *
 * ```c
 * CHECK_JSON_INPUT(json_at(ctx->params, 0), JSON_GET_LOGS_FILTER_FIELDS, "Invalid eth_getLogs filter: ");
 * ```
 */
#define CHECK_JSON_INPUT(val, def, error_prefix)          \
  do {                                                    \
    if (!(ctx->flags & C4_PROVER_FLAG_INPUT_VALIDATED)) { \
      CHECK_JSON(val, def, error_prefix);                 \
      ctx->flags |= C4_PROVER_FLAG_INPUT_VALIDATED;       \
    }                                                     \
  } while (0)

#ifdef PROVER_CACHE
typedef union {
  uint8_t  bytes32[32];
  uint64_t uint64[4];
} prover_cache_key_t;

typedef void (*cache_free_cb)(void*);
typedef struct cache_entry {
  prover_cache_key_t  key;               ///< 32-byte cache key
  void*               value;             ///< Cached payload (ownership per `free` callback)
  uint32_t            size;              ///< Size of `value` in bytes
  uint64_t            timestamp;         ///< TTL: relative ms until global promotion, or absolute expiry in global cache; `0` = local-only or invalidated
  cache_free_cb       free;              ///< Frees `value` when the entry is evicted
  uint32_t            use_counter;       ///< References from local copies of a global entry
  struct cache_entry* next;              ///< Next entry in the per-context linked list
  bool                from_global_cache; ///< True if this local entry mirrors a global cache hit
} cache_entry_t;
#endif

/**
 * State for a single proof generation run.
 *
 * Create with `c4_prover_create`, drive `c4_prover_execute` until not `C4_PENDING`,
 * then read `proof`. On failure, inspect `state.error`.
 */
#ifdef PROVER_TRACE
// Forward declaration for pointer fields
typedef struct prover_trace_span prover_trace_span_t;
#endif
typedef struct {
  char*          method;          ///< RPC method name (heap-owned)
  json_t         params;          ///< RPC parameters as parsed JSON array (owns `start` buffer)
  bytes_t        proof;           ///< Encoded proof bytes when generation succeeds (heap-owned)
  chain_id_t     chain_id;        ///< Target chain for module dispatch
  c4_state_t     state;           ///< Pending requests and error message
  prover_flags_t flags;           ///< Bitmask of `prover_flag_types_t`
  bytes_t        client_state;    ///< Optional synced-period / checkpoint snapshot from the client
  bytes32_t      last_block_hash; ///< Cached EL block hash; skips redundant block proofs when it matches the request
  bytes_t        witness_key;     ///< Witness signer key material for checkpoint signing
#ifdef PROVER_CACHE
  cache_entry_t* cache; ///< Per-request cache list (server builds; promotes to global on free)
#endif
#ifdef HTTP_SERVER
  uint32_t client_type; ///< Beacon client compatibility hint for server-side routing
#endif
  uint32_t version;       ///< Requesting client version (affects proof URL layout and features)
  uint64_t compute_units; ///< Work units accumulated for the `Compute-Units` HTTP response header

#ifdef PROVER_TRACE
  prover_trace_span_t* trace_spans; ///< Finished spans (consumed by server export)
  prover_trace_span_t* trace_open;  ///< Currently open span, if any
#endif
} prover_ctx_t;

/**
 * Allocates and initializes a prover context for one RPC proof request.
 *
 * Always returns a non-NULL context. On invalid input, `ctx->state.error` is set
 * and subsequent `c4_prover_execute` calls return `C4_ERROR`.
 *
 * @param method RPC method to prove (must not be NULL)
 * @param params JSON array string, or NULL for `"[]"`
 * @param chain_id target chain id
 * @param flags bitmask of `prover_flag_types_t`
 * @return newly allocated context; release with `c4_prover_free`
 */
prover_ctx_t* c4_prover_create(char* method, char* params, chain_id_t chain_id, prover_flags_t flags) M_RET;

/**
 * Releases all resources owned by `ctx`, including the context struct.
 *
 * @param ctx prover context to destroy (may be NULL)
 * @return none
 */
void c4_prover_free(prover_ctx_t* ctx);

/**
 * Runs one step of proof generation for the chain module selected by `ctx->chain_id`.
 *
 * When the return value is `C4_PENDING`, satisfy requests in `ctx->state` and call
 * again. Repeat until `C4_SUCCESS` (`ctx->proof` populated) or `C4_ERROR`
 * (`ctx->state.error`).
 *
 * @param ctx prover context (must not be NULL)
 * @return `C4_PENDING`, `C4_SUCCESS`, or `C4_ERROR`
 */
c4_status_t c4_prover_execute(prover_ctx_t* ctx);

/**
 * Derives the current prover state without advancing execution.
 *
 * @param ctx prover context (must not be NULL)
 * @return `C4_ERROR` if `state.error` is set, `C4_SUCCESS` if `proof` is ready, otherwise `C4_PENDING`
 */
c4_status_t c4_prover_status(prover_ctx_t* ctx);

#ifdef PROVER_CACHE
/**
 * Retrieve a cached value by key from the local list, then the global cache.
 *
 * If found globally, copies metadata into the local list for thread-safe reuse.
 *
 * @param ctx prover context
 * @param key 32-byte cache key
 * @return read-only pointer to cached value, or NULL if not found (do not mutate; valid until cache cleanup or `c4_prover_free`)
 */
const void* c4_prover_cache_get(prover_ctx_t* ctx, bytes32_t key);

/**
 * Retrieve a cached value by key from the local per-context list only.
 *
 * @param ctx prover context
 * @param key 32-byte cache key
 * @return read-only pointer to cached value, or NULL if not found
 */
const void* c4_prover_cache_get_local(prover_ctx_t* ctx, bytes32_t key);
/**
 * Store a value in the local cache.
 *
 * Entries with `duration_ms > 0` may be promoted to the global cache when
 * `c4_prover_free` runs; `duration_ms == 0` keeps the entry local-only.
 *
 * @param ctx prover context
 * @param key 32-byte cache key
 * @param value cached payload (`free` takes ownership)
 * @param size size of `value` in bytes
 * @param duration_ms TTL in milliseconds for global promotion (`0` = local-only)
 * @param free callback invoked when the entry is evicted
 * @return none
 */
void c4_prover_cache_set(prover_ctx_t* ctx, bytes32_t key, void* value, uint32_t size, uint64_t duration_ms, cache_free_cb free);

/**
 * Evicts expired or oversized entries from the global cache.
 *
 * @param now current time in milliseconds (same clock as `current_ms`)
 * @param extra_size reserve this many bytes before enforcing the size cap
 * @return none
 */
void c4_prover_cache_cleanup(uint64_t now, uint64_t extra_size);

/**
 * Marks a global cache entry as invalid (immediate expiry).
 *
 * @param key 32-byte cache key to invalidate
 * @return none
 */
void c4_prover_cache_invalidate(bytes32_t key);

/**
 * Returns statistics for the global prover cache.
 *
 * @param entries number of entries in the global cache
 * @param size total bytes stored in cached payloads
 * @param max_size configured maximum cache size in bytes
 * @param capacity allocated slot capacity of the global array
 * @return none
 */
void c4_prover_cache_stats(uint64_t* entries, uint64_t* size, uint64_t* max_size, uint64_t* capacity);
#endif

/**
 * Clears chain-specific in-process prover caches.
 *
 * Each chain module may register a `RESET_CACHES` hook via CMake. Hooks
 * are invoked in registration order. Persistent storage is left untouched.
 *
 * @return none
 */
void c4_reset_prover_caches(void);

/**
 * Monotonic millisecond clock for cache TTL and server timing.
 *
 * Uses libuv when `C4_PROVER_USE_UV_TIME` is defined, otherwise `current_unix_ms`.
 *
 * @return milliseconds since an implementation-defined epoch (consistent within the process)
 */
uint64_t current_ms();

/**
 * Wall-clock time in milliseconds since the Unix epoch.
 *
 * @return Unix time in milliseconds
 */
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
  sbprintf(buf, "%l", value);
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
#define REQUEST_WORKER_THREAD_CATCH(ctx, cleanup)                                                         \
  {                                                                                                       \
    if (ctx->flags & C4_PROVER_FLAG_UV_SERVER_CTX && !(ctx->flags & C4_PROVER_FLAG_UV_WORKER_REQUIRED)) { \
      ctx->flags |= C4_PROVER_FLAG_UV_WORKER_REQUIRED;                                                    \
      cleanup;                                                                                            \
      return C4_PENDING;                                                                                  \
    }                                                                                                     \
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