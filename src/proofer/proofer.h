#ifndef C4_PROOFER_H
#define C4_PROOFER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../util/chains.h"
#include "../util/state.h"

// : APIs

// :: Internal APIs

// ::: proofer.h
// The proofer API is used to create proofs for a given method and parameters.
//
// Example:
//
// ```c
// proofer_ctx_t* ctx = c4_proofer_create("eth_getBlockByNumber", "[\"latest\", false]", C4_PROOFER_FLAG_INCLUDE_CODE);
// to use run the c4_proofer_execute in a loop:
// ```c
// data_request_t* data_request = NULL;
// bytes_t proof = {0};
// char* error = NULL;
// while (true) {
//   switch (c4_proofer_status(ctx)) {
//     case C4_SUCCESS:
//       proof = bytes_dup(ctx->proof);
//       break;
//     case C4_PENDING:
//       while ((data_request = c4_state_get_pending_request(&ctx->state))
//          fetch_data(data_request);
//       break;
//     case C4_ERROR:
//       error = strdup(ctx->state.error);
//       break;
//   }
// }
// c4_proofer_free(ctx);
// ```

/**
 * a bitmask holding flags used during the proofer context.
 */
typedef enum {
  C4_PROOFER_FLAG_INCLUDE_CODE       = 1 << 0, // includes the code of the contracts when creating the proof for eth_call, otherwise the verifier will need to fetch and cache the code as needed
  C4_PROOFER_FLAG_UV_SERVER_CTX      = 1 << 1, // the proofser is running in a UV-server and if the we expect cpu-intensice operations, we should return pending after setting the C4_PROOFER_FLAG_UV_WORKER_REQUIRED flag.
  C4_PROOFER_FLAG_UV_WORKER_REQUIRED = 1 << 2, // requests the proof execution to run in a worker thread instead of the main eventloop.
  C4_PROOFER_FLAG_CHAIN_STORE        = 1 << 3, // allows the proofer to use internal request with data from the chain stroe
} proofer_flag_types_t;

/**
 * a bitmask holding flags used during the proofer context.
 */
typedef uint32_t proofer_flags_t;

#ifdef PROOFER_CACHE
typedef void (*cache_free_cb)(void*);
typedef struct cache_entry {
  bytes32_t           key;       // cache key
  void*               value;     // cache value
  uint32_t            size;      // cache value size in order to
  uint64_t            timestamp; // cache timestamp to be removed after ttl. if this timestamp is 0. the entry will live only in the proofer_ctx, otherwise it will be stored in the global cache when proofer_free is called.
  cache_free_cb       free;      // the function to free the value.
  uint32_t            use_counter;
  struct cache_entry* next;              // next cache entry
  bool                from_global_cache; // previous cache entry
} cache_entry_t;
#endif

/**
 * a struct holding the proofer context.
 */
typedef struct {
  char*           method;       // rpc-method
  json_t          params;       // rpc- params
  bytes_t         proof;        // result or proof as bytes
  chain_id_t      chain_id;     // target chain
  c4_state_t      state;        // proofer ctx state, holind errors and requests.
  proofer_flags_t flags;        // proofer flags
  bytes_t         client_state; // optional client_state representing the synced periods and trusted blockhashes
#ifdef PROOFER_CACHE
  cache_entry_t* cache; // cache for the proofer (only active in the server context)
#endif
} proofer_ctx_t;

/**
 * create a new proofer context
 * @param method the rpc-method to proof
 * @param params the rpc-params to proof
 * @param chain_id the target chain
 * @param flags the proofer flags
 * @return the proofer context, which needs to get freed with c4_proofer_free
 */
proofer_ctx_t* c4_proofer_create(char* method, char* params, chain_id_t chain_id, proofer_flags_t flags);

/**
 * cleanup for the ctx
 * @param ctx the proofer context
 */
void c4_proofer_free(proofer_ctx_t* ctx);

/**
 * tries to create the proof, but if there are pending requests, they need to fetched before calling it again.
 * This function should be called until it returns C4_SUCCESS or C4_ERROR.
 * @param ctx the proofer context
 * @return the status of the proofer
 */
c4_status_t c4_proofer_execute(proofer_ctx_t* ctx);

/**
 * returns the status of the proofer
 * @param ctx the proofer context
 * @return the status of the proofer
 */
c4_status_t c4_proofer_status(proofer_ctx_t* ctx);

#ifdef PROOFER_CACHE
uint64_t current_ms();
uint64_t current_unix_ms();

void* c4_proofer_cache_get(proofer_ctx_t* ctx, bytes32_t key);
void  c4_proofer_cache_set(proofer_ctx_t* ctx, bytes32_t key, void* value, uint32_t size, uint64_t duration_ms, cache_free_cb free);
void  c4_proofer_cache_cleanup(uint64_t now, uint64_t extra_size);
void  c4_proofer_cache_invalidate(bytes32_t key);
void  c4_proofer_cache_stats(uint64_t* entries, uint64_t* size, uint64_t* max_size, uint64_t* capacity);
#endif

#define REQUEST_WORKER_THREAD_CATCH(ctx, cleanup)                                                         \
  if (ctx->flags & C4_PROOFER_FLAG_UV_SERVER_CTX && !(ctx->flags & C4_PROOFER_FLAG_UV_WORKER_REQUIRED)) { \
    ctx->flags |= C4_PROOFER_FLAG_UV_WORKER_REQUIRED;                                                     \
    cleanup;                                                                                              \
    return C4_PENDING;                                                                                    \
  }

#define REQUEST_WORKER_THREAD(ctx) REQUEST_WORKER_THREAD_CATCH(ctx, );

#ifdef __cplusplus
}
#endif

#endif