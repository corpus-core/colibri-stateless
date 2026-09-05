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
#define BEACON_SUPPORTS_DEBUG_ENDPOINTS      (BEACON_CLIENT_NIMBUS | BEACON_CLIENT_LIGHTHOUSE)
#define BEACON_SUPPORTS_PARENT_ROOT_HEADERS  (BEACON_CLIENT_LODESTAR) // Nimbus: status-im/nimbus-eth2#7305
#define BEACON_SUPPORTS_STATE_PROOF          (BEACON_CLIENT_LODESTAR) // /eth/v0/beacon/proof/state/{state_id}

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

typedef enum {
  C4_BLOCK_PROOF_TYPE_NONE      = 0, // hybrid / cache: only `el_header` is set, and `el_body` if requested
  C4_BLOCK_PROOF_TYPE_BEACON    = 1, // `beacon` is set; a consensus-layer block proof can be built
  C4_BLOCK_PROOF_TYPE_SEQUENCER = 2, // `sequencer` is set
} c4_block_proof_type_t;

typedef struct {
  bytes_t   block_hash_branch;        // merkle branch from execution blockHash to the CL body root
  uint64_t  block_hash_branch_gindex; // gindex of that branch
  bytes32_t cl_body_root;             // hash tree root of the beacon body
  ssz_ob_t  cl_header;                // beacon block header
  ssz_ob_t  cl_body;                  // beacon block body
  ssz_ob_t  sync_aggregate;           // sync aggregate that signs this (or the parent) block
  bytes32_t sign_parent_root;         // parentRoot of the block that carries the signature
  bytes32_t data_block_root;          // block root of the data block
} eth_block_beacon_t;

typedef struct {
  bytes_t payload;   // OP preconf payload (no signature), borrowed from the request
  bytes_t signature; // 65-byte sequencer signature, borrowed from the request
} eth_block_sequencer_t;

typedef struct {
  c4_block_proof_type_t proof_type;    // which side of the union is valid
  bytes_t               el_header;     // RLP-serialized execution-layer header
  ssz_ob_t              el_body;       // execution payload, or at least transactions + withdrawals
  bytes32_t             el_block_hash; // keccak(el_header)
  uint64_t              slot;          // beacon slot, or EL block number when no CL header exists
  union {
    eth_block_beacon_t    beacon;
    eth_block_sequencer_t sequencer;
  };
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

/**
 * Resolves an execution-layer block identifier into `eth_block_t`.
 * `proof_type` may be `BEACON` (default), `NONE` (hybrid / header cache), or
 * `SEQUENCER` (OP preconf). Callers must check `proof_type` before reading
 * `.beacon` or `.sequencer`.
 *
 * @param ctx prover context
 * @param block JSON block identifier
 * @param beacon_block output block
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
c4_status_t c4_beacon_get_block_for_eth(prover_ctx_t* ctx, json_t block, eth_block_t* beacon_block);
c4_status_t c4_beacon_get_block_for_eth_with_body(prover_ctx_t* ctx, json_t block, eth_block_t* beacon_block);

// :: Hybrid Mode (beacon_header.c)

/**
 * Resolves the block identifier and fetches/caches the verified block header
 * from the remote prover. Returns an `eth_block_t` with `proof_type = NONE`.
 *
 * Handles all block identifier formats: `"latest"`, `"safe"`, `"justified"`,
 * `"finalized"`, block hash (`0x...` 32 bytes), and block number (`0x...`).
 * Tag-based identifiers are cached with TTLs (latest ~ block_time/2,
 * safe ~ half epoch, finalized ~ one epoch).
 *
 * @param ctx prover context (must have `C4_PROVER_FLAG_HYBRID` set)
 * @param block JSON block identifier
 * @param beacon_block output: populated with EL header (and body if requested)
 * @return `C4_SUCCESS` when header is ready, `C4_PENDING` while waiting, `C4_ERROR` on failure
 */
c4_status_t c4_hybrid_get_block_for_eth(prover_ctx_t* ctx, json_t block, eth_block_t* beacon_block, bool with_body);

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

#define JSON_BEACON_HEADER_MESSAGE     "{slot:suint,proposer_index:suint,parent_root:bytes32,state_root:bytes32,body_root:bytes32}"
#define JSON_BEACON_HEADER_ENTRY       "{root:bytes32,canonical?:bool,header:{message:" JSON_BEACON_HEADER_MESSAGE "}}"
#define JSON_BEACON_HEADER_OBJECT      "{data:" JSON_BEACON_HEADER_ENTRY "}"
#define JSON_BEACON_HEADER_LIST        "{data:[" JSON_BEACON_HEADER_ENTRY "]}"
#define JSON_BEACON_FINALITY           "{data:{current_justified:{epoch:suint,root:bytes32},finalized:{epoch:suint,root:bytes32}}}"
#define JSON_BEACON_HISTORICAL_SUMMARIES "{data:{historical_summaries:[{block_summary_root:bytes32,state_summary_root:bytes32}],proof:[bytes32]}}"

c4_status_t c4_send_beacon_json(prover_ctx_t* ctx, char* path, char* query, uint32_t ttl, json_t* result, data_request_t** req);
c4_status_t c4_send_beacon_ssz(prover_ctx_t* ctx, char* path, char* query, const ssz_def_t* def, uint32_t ttl, ssz_ob_t* result, data_request_t** req);
c4_status_t c4_send_beacon_json_with_client_type(prover_ctx_t* ctx, char* path, char* query, uint32_t ttl, json_t* result, uint32_t client_type, data_request_t** req);
c4_status_t c4_send_beacon_ssz_with_client_type(prover_ctx_t* ctx, char* path, char* query, const ssz_def_t* def, uint32_t ttl, ssz_ob_t* result, uint32_t client_type, data_request_t** req);
c4_status_t c4_send_internal_request(prover_ctx_t* ctx, char* path, char* query, uint32_t ttl, bytes_t* result);

/**
 * Non-throwing variant of `c4_send_beacon_json`. Enqueues the request (or
 * reuses an existing one) and returns the underlying `data_request_t*`
 * without touching `ctx->state.error`. This lets callers handle transport
 * errors locally -- e.g. clearing them, falling back to another endpoint,
 * or mapping "404 / not found" to a domain-level empty result.
 *
 * @param ctx     prover context
 * @param path    beacon API path (e.g. `"eth/v1/beacon/headers?slot=..."`)
 * @param query   optional query string (appended after `?`); may be NULL
 * @param ttl     cache TTL in seconds (0 = default)
 * @param out_req receives the underlying `data_request_t*`; never NULL on
 *                any of the three returned statuses
 *
 * @return
 *   - `C4_PENDING`: request was enqueued or is still awaiting a response.
 *   - `C4_SUCCESS`: response is available. Read `(*out_req)->response`.
 *     The caller is responsible for JSON-parsing / validating the body.
 *   - `C4_ERROR`: request completed with an error. Read `(*out_req)->error`.
 *     `ctx->state.error` is NOT set; the caller decides whether to propagate
 *     via `THROW_ERROR(...)` or to recover.
 */
c4_status_t c4_send_beacon_json_no_throw(prover_ctx_t* ctx, char* path, char* query, uint32_t ttl, data_request_t** out_req);

/**
 * Non-throwing variant of `c4_send_beacon_ssz`. See
 * `c4_send_beacon_json_no_throw` for the general contract. On `C4_SUCCESS`
 * the caller receives raw response bytes via `(*out_req)->response`; no
 * SSZ definition is attached and no SSZ validation is performed.
 */
c4_status_t c4_send_beacon_ssz_no_throw(prover_ctx_t* ctx, char* path, char* query, uint32_t ttl, data_request_t** out_req);

/**
 * Non-throwing variant of `c4_send_internal_request`. See
 * `c4_send_beacon_json_no_throw` for the general contract. Used to talk to
 * in-process server handlers (`c4_handle_*`) via the same `data_request_t`
 * pipeline as external endpoints, but with local error handling.
 */
c4_status_t c4_send_internal_request_no_throw(prover_ctx_t* ctx, char* path, char* query, uint32_t ttl, data_request_t** out_req);
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