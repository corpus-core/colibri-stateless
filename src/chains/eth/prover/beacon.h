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

#ifndef C4_BEACON_H
#define C4_BEACON_H

#include "../util/json.h"
#include "../util/ssz.h"
#include "prover.h"

// Bitmask-based beacon client types for feature detection
#define BEACON_CLIENT_UNKNOWN    0x00000000 // No specific client requirement
#define BEACON_CLIENT_NIMBUS     0x00000001 // (1 << 0)
#define BEACON_CLIENT_LODESTAR   0x00000002 // (1 << 1)
#define BEACON_CLIENT_PRYSM      0x00000004 // (1 << 2)
#define BEACON_CLIENT_LIGHTHOUSE 0x00000008 // (1 << 3)
#define BEACON_CLIENT_TEKU       0x00000010 // (1 << 4)
#define BEACON_CLIENT_GRANDINE   0x00000020 // (1 << 5)
// Add more clients as needed...

// Feature-based client combinations
#define BEACON_SUPPORTS_LIGHTCLIENT_UPDATE   (BEACON_CLIENT_NIMBUS | BEACON_CLIENT_LODESTAR)
#define BEACON_SUPPORTS_HISTORICAL_SUMMARIES (BEACON_CLIENT_NIMBUS | BEACON_CLIENT_LODESTAR)
#define BEACON_SUPPORTS_PARENT_ROOT_HEADERS  (BEACON_CLIENT_LODESTAR)
#define BEACON_SUPPORTS_DEBUG_ENDPOINTS      (BEACON_CLIENT_NIMBUS | BEACON_CLIENT_LIGHTHOUSE)

#ifdef __cplusplus
extern "C" {
#endif

#define FINALITY_KEY "FinalityRoots"
#define DEFAULT_TTL  (3600 * 24) // 1 day
// beacon block including the relevant parts for the proof

typedef struct {
  uint64_t  slot; // slot of the block
  bytes32_t root; // root of the block
} beacon_head_t;

#ifdef PROVER_CACHE
#define BODY_MERKLE_TREE_SIZE 32 /**< depth 4: gindex 1..31, covers up to 16 body fields */
#define EP_MERKLE_TREE_SIZE   64 /**< depth 5: gindex 1..63, covers up to 32 EP fields */

/**
 * Pre-computed Merkle tree node hashes for the beacon block body and execution payload.
 * Allows proof extraction via pure lookups instead of repeated SHA256 computation.
 * Arrays are indexed by generalized index (gindex 0 unused).
 */
typedef struct {
  bytes32_t body[BODY_MERKLE_TREE_SIZE]; /**< body tree nodes indexed by gindex */
  bytes32_t ep[EP_MERKLE_TREE_SIZE];     /**< execution payload tree nodes indexed by gindex */
  uint8_t   body_field_count;            /**< actual body field count (11 Deneb, 12 Electra) */
  uint8_t   ep_field_count;              /**< actual EP field count (17) */
  uint8_t   ep_field_index;              /**< index of executionPayload within body container */
  gindex_t  ep_body_gindex;              /**< body-level gindex of executionPayload (e.g. 25) */
  bool      valid;                       /**< true once leaf + internal hashes are computed */
} beacon_body_merkle_cache_t;
#endif

typedef struct {
  uint64_t  slot;             // slot of the block
  ssz_ob_t  header;           // block header
  ssz_ob_t  execution;        // execution payload or SSZ-encoded header data (14 fields) when header_only
  ssz_ob_t  body;             // body of the block (empty in hybrid mode)
  ssz_ob_t  sync_aggregate;   // sync aggregate with the signature of the block (empty in hybrid mode)
  bytes32_t sign_parent_root; // the parentRoot of the block containing the signature
  bytes32_t data_block_root;  // the blockroot used for the data block
  bool      header_only;      // true when only header data is available (hybrid mode)
#ifdef PROVER_CACHE
  beacon_body_merkle_cache_t merkle_cache; /**< pre-computed body/EP Merkle trees */
#endif
} beacon_block_t;

// :: Verified Header Cache

#define HEADER_CACHE_SIZE 256

/**
 * A single cached verified block header entry.
 * Stores the SSZ-encoded `ETH_BLOCK_HEADER_DATA` (14 fields, ~540 bytes).
 * Optionally stores the full SSZ execution payload when fetched via
 * `eth_getBlockByNumber` (for transaction/receipt proofs that need the full block).
 * Memory for `header_data.bytes` and `execution.bytes` is independently allocated via `bytes_dup()`.
 */
typedef struct {
  chain_id_t chain_id;
  uint64_t   block_number;
  bytes32_t  block_hash;
  ssz_ob_t   header_data;
  ssz_ob_t   execution; // optional: full SSZ execution payload (NULL when only header is cached)
  uint64_t   cached_at_ms;
} verified_header_entry_t;

/** Block tag indices for the tag-to-block-hash cache. */
typedef enum {
  HEADER_TAG_LATEST    = 0,
  HEADER_TAG_SAFE      = 1,
  HEADER_TAG_FINALIZED = 2,
  HEADER_TAG_COUNT     = 3
} header_tag_t;

/**
 * Maps a block tag (latest/safe/finalized) to a cached block hash with a timestamp for TTL checks.
 *
 * The `fetching_since_ms` / `fetching_ctx` pair implements a stale-while-revalidate sentinel
 * to prevent thundering-herd fetches when the tag TTL expires while multiple requests are in flight.
 * The first request to miss sets the sentinel; subsequent requests serve the stale entry.
 * `fetching_ctx` is only compared by pointer value (never dereferenced) so that the fetching
 * context's own re-entry can bypass the stale path and process its response.
 *
 * Thread safety: these fields are not protected by a mutex. In multi-threaded bindings
 * (Kotlin, Swift, Python) a race can at worst cause a duplicate fetch, which is acceptable.
 */
typedef struct {
  bytes32_t block_hash;
  uint64_t  cached_at_ms;
  uint64_t  fetching_since_ms; /**< non-zero while a fetch is in progress (unprotected: benign race accepted) */
  uintptr_t fetching_ctx;     /**< opaque identity of the fetching prover context (compared, never dereferenced) */
} tag_cache_entry_t;

/**
 * Ring-buffer cache of verified block headers for prover-side optimization.
 * Avoids redundant `eth_getBlockHeader` requests when multiple RPC calls
 * target the same block. The verifier does NOT read from this cache.
 *
 * The `tags` array caches the mapping from special block identifiers
 * (`latest`, `safe`/`justified`, `finalized`) to their resolved block hash.
 * Each mapping has a TTL: `latest` ~ block_time/2, `safe` ~ half epoch,
 * `finalized` ~ one epoch.
 */
typedef struct {
  verified_header_entry_t entries[HEADER_CACHE_SIZE];
  uint32_t                count;
  uint32_t                head_idx;
  tag_cache_entry_t       tags[HEADER_TAG_COUNT];
} verified_header_cache_t;

/**
 * Looks up a verified header by block number.
 *
 * @param cache the header cache
 * @param chain_id the chain to match
 * @param block_number the block number to look up
 * @return pointer to the cached entry, or NULL on miss
 */
const verified_header_entry_t* c4_header_cache_get_by_number(const verified_header_cache_t* cache, chain_id_t chain_id, uint64_t block_number);

/**
 * Looks up a verified header by block hash.
 *
 * @param cache the header cache
 * @param chain_id the chain to match
 * @param block_hash 32-byte block hash to look up
 * @return pointer to the cached entry, or NULL on miss
 */
const verified_header_entry_t* c4_header_cache_get_by_hash(const verified_header_cache_t* cache, chain_id_t chain_id, const uint8_t* block_hash);

/**
 * Inserts a verified header into the cache. Overwrites the oldest entry when full.
 * The `header_data.bytes` are duplicated via `bytes_dup()`.
 *
 * @param cache the header cache
 * @param chain_id the chain ID
 * @param block_number the block number
 * @param block_hash 32-byte block hash
 * @param header_data SSZ-encoded `ETH_BLOCK_HEADER_DATA` (will be copied)
 */
void c4_header_cache_put(verified_header_cache_t* cache, chain_id_t chain_id, uint64_t block_number, const uint8_t* block_hash, ssz_ob_t header_data);

/**
 * Attaches a full SSZ execution payload to an existing cache entry.
 * This allows subsequent calls (e.g. `eth_getTransactionReceipt` after
 * `eth_getTransactionByHash`) to reuse the execution payload from cache.
 * No-op if the entry does not exist. The `execution.bytes` are duplicated.
 *
 * @param cache the header cache
 * @param chain_id the chain ID
 * @param block_number the block number of the entry to update
 * @param execution full SSZ execution payload (will be copied)
 */
void c4_header_cache_set_execution(verified_header_cache_t* cache, chain_id_t chain_id, uint64_t block_number, ssz_ob_t execution);

/**
 * Returns the process-wide verified header cache used by hybrid mode (ETH + OP).
 *
 * @return pointer to the global cache singleton
 */
verified_header_cache_t* c4_header_cache_global(void);

/**
 * TTL in milliseconds for a logical block tag (`latest` / `safe` / `finalized`) in the header cache.
 *
 * @param chain_id chain identifier
 * @param tag block tag
 * @param flags prover flags (affects `latest` TTL when light-client flag is set)
 * @return TTL in milliseconds
 */
uint64_t c4_header_tag_ttl_ms(chain_id_t chain_id, header_tag_t tag, prover_flags_t flags);

/**
 * Fetches execution payload for the given chain: Ethereum beacon/hybrid or OP preconf/hybrid.
 *
 * @param ctx prover context
 * @param block JSON block parameter (e.g. `"latest"`, `"0x…"` number or hash)
 * @param beacon_block output execution payload and metadata
 * @return status
 */
c4_status_t c4_get_execution_for_chain(prover_ctx_t* ctx, json_t block, beacon_block_t* beacon_block);

/**
 * Fetches block context for state proofs: beacon block (ETH) or OP execution/preconf analogue.
 *
 * @param ctx prover context
 * @param block JSON block parameter
 * @param beacon_block output
 * @return status
 */
c4_status_t c4_get_block_for_chain(prover_ctx_t* ctx, json_t block, beacon_block_t* beacon_block);

// get the beacon block for the given eth block number or hash
c4_status_t c4_eth_get_signblock_and_parent(prover_ctx_t* ctx, bytes32_t sig_root, bytes32_t data_root, ssz_ob_t* sig_block, ssz_ob_t* data_block, bytes32_t data_root_result);
c4_status_t c4_beacon_get_block_for_eth(prover_ctx_t* ctx, json_t block, beacon_block_t* beacon_block);

// :: Hybrid Mode (beacon_header.c)

/**
 * Resolves the block identifier and fetches/caches the verified block header
 * from the remote prover. Returns a header-only `beacon_block_t`.
 *
 * Handles all block identifier formats: `"latest"`, `"safe"`, `"justified"`,
 * `"finalized"`, block hash (`0x...` 32 bytes), and block number (`0x...`).
 * Tag-based identifiers are cached with TTLs (latest ~ block_time/2,
 * safe ~ half epoch, finalized ~ one epoch).
 *
 * @param ctx prover context (must have `C4_PROVER_FLAG_HYBRID` set)
 * @param block JSON block identifier
 * @param beacon_block output: populated with header-only data
 * @return `C4_SUCCESS` when header is ready, `C4_PENDING` while waiting, `C4_ERROR` on failure
 */
c4_status_t c4_hybrid_get_block_for_eth(prover_ctx_t* ctx, json_t block, beacon_block_t* beacon_block);

/**
 * Fetches the full SSZ execution payload for the given block in hybrid mode.
 * Checks the header cache for a cached execution payload first. On miss,
 * fetches `eth_getBlockByNumber` from the remote prover, verifies the response,
 * and caches both the header data and execution payload.
 *
 * In non-hybrid mode, delegates to `c4_beacon_get_block_for_eth`.
 *
 * @param ctx prover context
 * @param block JSON block identifier
 * @param beacon_block output: populated with execution payload (`header_only = false`)
 * @return `C4_SUCCESS` when ready, `C4_PENDING` while waiting, `C4_ERROR` on failure
 */
c4_status_t c4_beacon_get_execution_for_eth(prover_ctx_t* ctx, json_t block, beacon_block_t* beacon_block);

/**
 * Internal hybrid implementation: fetches the full execution payload from the
 * remote prover via `eth_getBlockByNumber`, verifies it, and populates
 * `beacon_block->execution` with the full SSZ execution payload.
 *
 * @param ctx prover context (must have `C4_PROVER_FLAG_HYBRID` set)
 * @param block JSON block identifier
 * @param beacon_block output: populated with full execution payload
 * @return `C4_SUCCESS` when ready, `C4_PENDING` while waiting, `C4_ERROR` on failure
 */
c4_status_t c4_hybrid_get_execution_for_eth(prover_ctx_t* ctx, json_t block, beacon_block_t* beacon_block);

/**
 * Builds an SSZ-encoded `ETH_BLOCK_HEADER_DATA` (14 fields) from a full execution payload.
 * Computes `transactionsRoot` via `ssz_hash_tree_root(transactions)`.
 * The returned `ssz_ob_t.bytes` is heap-allocated and must be freed by the caller.
 *
 * @param execution full SSZ execution payload
 * @return SSZ object with `ETH_BLOCK_HEADER_DATA` def and heap-allocated bytes
 */
ssz_ob_t c4_build_header_data_from_execution(ssz_ob_t execution);

// creates a new header with the body_root passed and returns the ssz_builder_t, which must be freed
ssz_builder_t c4_proof_add_header(ssz_ob_t header, bytes32_t body_root);

c4_status_t c4_send_beacon_json(prover_ctx_t* ctx, char* path, char* query, uint32_t ttl, json_t* result);
c4_status_t c4_send_beacon_ssz(prover_ctx_t* ctx, char* path, char* query, const ssz_def_t* def, uint32_t ttl, ssz_ob_t* result);
c4_status_t c4_send_beacon_json_with_client_type(prover_ctx_t* ctx, char* path, char* query, uint32_t ttl, json_t* result, uint32_t client_type);
c4_status_t c4_send_beacon_ssz_with_client_type(prover_ctx_t* ctx, char* path, char* query, const ssz_def_t* def, uint32_t ttl, ssz_ob_t* result, uint32_t client_type);
c4_status_t c4_send_internal_request(prover_ctx_t* ctx, char* path, char* query, uint32_t ttl, bytes_t* result);
#ifdef PROVER_CACHE
c4_status_t c4_set_latest_block(prover_ctx_t* ctx, uint64_t latest_block_number);
c4_status_t c4_eth_update_finality(prover_ctx_t* ctx, bytes32_t checkpoint, uint64_t* slot);

/**
 * Pre-computes all Merkle tree nodes for the body and execution payload containers.
 * After this call, `block->merkle_cache.valid` is true and proofs can be extracted
 * via `ssz_create_multi_proof_from_body_cache()` without any SHA256 computation.
 *
 * @param block The beacon block whose body/EP hashes to pre-compute
 */
void c4_beacon_compute_merkle_cache(beacon_block_t* block);

/**
 * Creates a multi-Merkle proof by looking up pre-computed hashes from the body cache.
 * Falls back to NULL_BYTES if any gindex falls outside the cached body/EP tree range.
 *
 * @param cache The pre-computed merkle cache (must have valid==true)
 * @param root_hash Output: receives the body hash_tree_root from cache->body[1]
 * @param gindex Array of generalized indices to prove
 * @param gindex_len Number of generalized indices
 * @return Allocated proof bytes (caller must free), or NULL_BYTES on cache miss
 */
bytes_t ssz_create_multi_proof_from_body_cache(
    const beacon_body_merkle_cache_t* cache,
    bytes32_t root_hash,
    const gindex_t* gindex,
    int gindex_len);

/*
 *  Updates the beacon block data in the cache.
 *
 *  This uses the following keys in the cache:
 *  - B<beacon_block_root> -> beacon_block_t
 *  - Slatest -> beacon_head_t
 *  - S<exec_block_hash> -> beacon_head_t
 *  - S<exec_block_number> -> beacon_head_t
 *
 *  @param ctx The context of the prover
 *  @param beacon_block The beacon block to update
 *  @param latest_timestamp The latest timestamp of the block
 *  @param block_root The root of the block
 */
void c4_beacon_cache_update_blockdata(prover_ctx_t* ctx, beacon_block_t* beacon_block, uint64_t latest_timestamp, bytes32_t block_root);

#endif

#ifdef __cplusplus
}
#endif

#endif