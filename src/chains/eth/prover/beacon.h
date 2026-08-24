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
#include "header_cache.h"
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

typedef struct {
  bool      header_only;              // true when only header data is available (hybrid mode)
  bytes_t   el_header;                // the rlp serialized execution layer header
  ssz_ob_t  el_body;                  // the body containing either the full execution paylod or at least the transaction and withdrawal fields
  bytes32_t el_block_hash;            // the block hash of the execution block
  bytes_t   block_hash_branch;        // the branch of the block hash, used for the block proof
  uint64_t  block_hash_branch_gindex; // the gindex of the block hash branch, used for the block proof
  uint64_t  slot;                     // slot of the block
  bytes32_t cl_body_root;             // the body root of the block
  ssz_ob_t  cl_header;                // block header
  ssz_ob_t  cl_body;                  // body of the block (empty in hybrid mode)
  ssz_ob_t  sync_aggregate;           // sync aggregate with the signature of the block (empty in hybrid mode)
  bytes32_t sign_parent_root;         // the parentRoot of the block containing the signature
  bytes32_t data_block_root;          // the blockroot used for the data block
} eth_block_t;

// :: Verified Header Cache
//
// The cache of verified headers lives in the verifier (`verifier/header_cache.h`,
// included above), since the verifier resolves `blockHash`-only block proofs from it.
// The prover uses the same cache for hybrid-mode header/execution reuse. The
// tag-to-block-hash mapping (`latest`/`safe`/`finalized` with TTLs) is prover-only
// and stays private to `prover/beacon_header.c`.

// get the beacon block for the given eth block number or hash
c4_status_t c4_eth_get_signblock_and_parent(prover_ctx_t* ctx, bytes32_t sig_root, bytes32_t data_root, ssz_ob_t* sig_block, ssz_ob_t* data_block, bytes32_t data_root_result);
c4_status_t c4_beacon_get_block_for_eth(prover_ctx_t* ctx, json_t block, eth_block_t* beacon_block);
c4_status_t c4_beacon_get_block_for_eth_with_body(prover_ctx_t* ctx, json_t block, eth_block_t* beacon_block);

// :: Hybrid Mode (beacon_header.c)

/**
 * Resolves the block identifier and fetches/caches the verified block header
 * from the remote prover. Returns a header-only `eth_block_t`.
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
c4_status_t c4_hybrid_get_block_for_eth(prover_ctx_t* ctx, json_t block, eth_block_t* beacon_block, bool with_body);

/**
 * Builds an SSZ-encoded `ETH_BLOCK_HEADER_DATA` (14 fields) from a full execution payload.
 * Computes `transactionsRoot` via `ssz_hash_tree_root(transactions)`.
 * The returned `ssz_ob_t.bytes` is heap-allocated and must be freed by the caller.
 *
 * @param execution full SSZ execution payload
 * @return SSZ object with `ETH_BLOCK_HEADER_DATA` def and heap-allocated bytes
 */
ssz_ob_t c4_build_header_data_from_execution(ssz_ob_t execution);

/**
 * Ensures the verifier header cache holds the RLP-encoded EL header for the block
 * of the given (already verified) execution payload, so proofs built in hybrid mode
 * can reference the block by hash only (`blockHash` variant of `ETH_BLOCK_PROOF_UNION`).
 *
 * On a cache miss the block is fetched via `eth_getBlockByHash` from the user's RPC
 * (untrusted), the RLP header is rebuilt from the JSON and only cached when
 * `keccak(rlp) == blockHash` of the verified execution payload. This bridge becomes
 * obsolete once the remote block proofs deliver the RLP `elHeader` directly.
 *
 * No-op when the `EL_HEADER_CACHE` build option is disabled.
 *
 * @param ctx prover context (must have `C4_PROVER_FLAG_HYBRID` set)
 * @param execution verified full SSZ execution payload of the block
 * @return `C4_SUCCESS` when cached, `C4_PENDING` while fetching, `C4_ERROR` on failure
 */
c4_status_t c4_hybrid_ensure_el_header(prover_ctx_t* ctx, ssz_ob_t execution);

/**
 * Persists the process-global tag → block-hash cache (`latest` / `safe` / `finalized`)
 * via the configured `storage_plugin_t` under the key `header_tags_<chain_id>`.
 * The payload is the raw bytes of the tag cache array (fixed size), so the
 * consumer must be built with a matching layout. In-flight sentinels
 * (`fetching_since_ms`, `fetching_ctx`) are dropped on save because they are
 * only meaningful within a single process.
 *
 * No-op when no storage backend is configured.
 *
 * @param chain_id the chain whose tag cache should be persisted
 */
void c4_prover_header_tags_save(chain_id_t chain_id);

/**
 * Restores the tag cache from persistent storage (key `header_tags_<chain_id>`).
 * Rejects payloads that do not match the expected fixed size. In-flight
 * sentinels are always cleared on load so the fresh process starts without
 * any stale "fetching" state.
 *
 * @param chain_id the chain to load
 * @return true if a valid payload was found and applied
 */
bool c4_prover_header_tags_load(chain_id_t chain_id);

/**
 * Clears the in-process header-tag cache (`latest` / `safe` / `finalized`).
 *
 * Does not delete persisted `header_tags_<chain_id>` blobs from storage.
 */
void c4_prover_header_tags_clear(void);

#ifdef TEST
// Test-only introspection into the header-tag cache. Not part of the public API; only the
// unit tests link against these. The `tag` parameter is a raw index (0=LATEST, 1=SAFE,
// 2=FINALIZED) so tests do not need to import the private enum from beacon_header.c.
void c4_prover_header_tags_test_set(uint32_t tag, const uint8_t* hash, uint64_t block_number, uint64_t cached_at_ms);
void c4_prover_header_tags_test_get(uint32_t tag, uint8_t* hash_out, uint64_t* block_number_out, uint64_t* cached_at_ms_out);
bool c4_prover_header_tags_test_apply_write(uint32_t tag, uint64_t new_number, uint64_t now_ms, uint64_t ttl_ms);

// Drives the private `resolve_el_header_from_block` helper against a synthesized
// `blockHash`-variant proof. Only linked when `TEST=ON`.
c4_status_t c4_hybrid_test_resolve_block_hash(prover_ctx_t* ctx, const uint8_t* hash, bytes_t* el_header);
#endif

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

/*
 *  Updates the beacon block data in the cache.
 *
 *  This uses the following keys in the cache:
 *  - B<beacon_block_root> -> eth_block_t
 *  - Slatest -> beacon_head_t
 *  - S<exec_block_hash> -> beacon_head_t
 *  - S<exec_block_number> -> beacon_head_t
 *
 *  @param ctx The context of the prover
 *  @param beacon_block The beacon block to update
 *  @param latest_timestamp The latest timestamp of the block
 *  @param block_root The root of the block
 */
void c4_beacon_cache_update_blockdata(prover_ctx_t* ctx, eth_block_t* beacon_block, uint64_t latest_timestamp, bytes32_t block_root);

#endif
c4_status_t c4_beacon_fill_becaon_block_from_eth(prover_ctx_t* ctx,
                                                 eth_block_t* beacon_block, bytes32_t data_root, ssz_ob_t data_block, ssz_ob_t sig_block);

#ifdef __cplusplus
}
#endif

#endif