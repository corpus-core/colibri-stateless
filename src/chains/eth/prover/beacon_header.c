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

#include "beacon.h"
#include "beacon_types.h"
#include "crypto.h"
#include "el_header.h"
#include "eth_req.h"
#include "json.h"
#include "plugin.h"
#include "prover.h"
#include "prover_cache_url.h"
#include "verify.h"
#include "version.h"
#include <stdlib.h>
#include <string.h>

// -- Tag-to-Block-Hash Cache (hybrid mode only) --
//
// The verified header entries themselves live in the verifier-side cache
// (`verifier/header_cache.h`). Only the mapping from block tags to block hashes
// (with TTL and stale-while-revalidate handling) is prover-specific and kept here.

#define TAG_FETCH_TIMEOUT_MS 30000 /**< max time a stale-while-revalidate sentinel stays valid before allowing a retry */

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
 * `block_number` is the EL block number of `block_hash`. It is used to keep tag updates
 * monotonic: a slower in-flight `latest` fetch that resolves to an older block must not
 * overwrite the tag while the current entry is still fresh (`Promise.all` race). After the
 * TTL has expired any number wins, so a legitimate deeper reorg (shorter head) still lands.
 *
 * Thread safety: these fields are not protected by a mutex. In multi-threaded bindings
 * (Kotlin, Swift, Python) a race can at worst cause a duplicate fetch, which is acceptable.
 */
typedef struct {
  bytes32_t block_hash;
  uint64_t  block_number; /**< EL block number of `block_hash` (0 = unknown, matches uninitialized entries) */
  uint64_t  cached_at_ms;
  uint64_t  fetching_since_ms; /**< non-zero while a fetch is in progress (unprotected: benign race accepted) */
  uintptr_t fetching_ctx;      /**< opaque identity of the fetching prover context (compared, never dereferenced) */
} tag_cache_entry_t;

static tag_cache_entry_t g_header_tags[HEADER_TAG_COUNT] = {0};

// -- Persistence for the tag cache --
//
// The tag cache is currently a single process-global array (not chain-keyed),
// which is fine for the CLI where each invocation targets exactly one chain.
// We persist per-chain nonetheless so a shared cache directory can hold
// snapshots for multiple chains side by side.

#define HEADER_TAGS_BLOB_SIZE                  (sizeof(g_header_tags))
#define header_tags_storage_key(chain_id, dst) sbprintf(dst, "header_tags_%l", chain_id)

void c4_prover_header_tags_save(chain_id_t chain_id) {
  char             key[64] = {0};
  storage_plugin_t plugin  = {0};
  c4_get_storage_config(&plugin);
  if (!plugin.set) return;

  tag_cache_entry_t snapshot[HEADER_TAG_COUNT];
  memcpy(snapshot, g_header_tags, sizeof(snapshot));
  for (uint32_t i = 0; i < HEADER_TAG_COUNT; i++) {
    snapshot[i].fetching_since_ms = 0;
    snapshot[i].fetching_ctx      = 0;
  }

  header_tags_storage_key(chain_id, key);
  plugin.set(key, bytes((uint8_t*) snapshot, (uint32_t) sizeof(snapshot)));
}

bool c4_prover_header_tags_load(chain_id_t chain_id) {
  char             key[64] = {0};
  buffer_t         buf     = {0};
  storage_plugin_t plugin  = {0};
  c4_get_storage_config(&plugin);
  if (!plugin.get) return false;

  header_tags_storage_key(chain_id, key);
  if (!plugin.get(key, &buf) || buf.data.len != HEADER_TAGS_BLOB_SIZE) {
    buffer_free(&buf);
    return false;
  }

  memcpy(g_header_tags, buf.data.data, HEADER_TAGS_BLOB_SIZE);
  // Sentinels only make sense within the process that set them; a fresh run
  // must never inherit an "already fetching" state from a previous invocation.
  for (uint32_t i = 0; i < HEADER_TAG_COUNT; i++) {
    g_header_tags[i].fetching_since_ms = 0;
    g_header_tags[i].fetching_ctx      = 0;
  }
  buffer_free(&buf);
  return true;
}

void c4_prover_header_tags_clear(void) {
  memset(g_header_tags, 0, sizeof(g_header_tags));
}

// -- Tag Write Rule --
//
// Applied after every successful `latest`/`safe`/`finalized` fetch. Kept in its own helper
// so unit tests can exercise the decision without going through the full fetch/verify path.
//
// Rules (in order):
// - No existing hash yet          → write.
// - `new_number >= old_number`    → write. `>` is normal chain progress, `==` catches a
//                                    same-height reorg (new hash on the same slot).
// - `new_number <  old_number` and TTL still fresh
//                                 → skip. This is a slower in-flight `latest` from a parallel
//                                    request (`Promise.all` race) trying to roll the tag back.
// - `new_number <  old_number` and TTL already expired
//                                 → write. After TTL any refetch is authoritative, including
//                                    the rare deeper reorg where the head really dropped.
//
// Sentinels are cleared unconditionally by the caller: they are a per-fetch marker for the
// fetching ctx and must not survive the decision, no matter which branch is taken.
static bool tag_should_apply_write(const tag_cache_entry_t* tc, uint64_t new_number, uint64_t now, uint64_t ttl_ms) {
  bool have_existing = tc->cached_at_ms && !bytes_all_zero(bytes(tc->block_hash, 32));
  if (!have_existing) return true;
  if (new_number >= tc->block_number) return true;
  bool within_ttl = (now - tc->cached_at_ms < ttl_ms);
  return !within_ttl;
}

#ifdef TEST
// -- Test-only introspection --
//
// Unit tests exercise the monotonic tag-write rule directly against the in-process cache
// (`test/unittests/test_header_tag_monotonic.c`). No production caller depends on these
// symbols; they exist only to keep the helper above `static` in normal builds.

void c4_prover_header_tags_test_set(uint32_t tag, const uint8_t* hash, uint64_t block_number, uint64_t cached_at_ms) {
  if (tag >= HEADER_TAG_COUNT) return;
  tag_cache_entry_t* tc = &g_header_tags[tag];
  if (hash)
    memcpy(tc->block_hash, hash, 32);
  else
    memset(tc->block_hash, 0, 32);
  tc->block_number = block_number;
  tc->cached_at_ms = cached_at_ms;
}

void c4_prover_header_tags_test_get(uint32_t tag, uint8_t* hash_out, uint64_t* block_number_out, uint64_t* cached_at_ms_out) {
  if (tag >= HEADER_TAG_COUNT) return;
  const tag_cache_entry_t* tc = &g_header_tags[tag];
  if (hash_out) memcpy(hash_out, tc->block_hash, 32);
  if (block_number_out) *block_number_out = tc->block_number;
  if (cached_at_ms_out) *cached_at_ms_out = tc->cached_at_ms;
}

bool c4_prover_header_tags_test_apply_write(uint32_t tag, uint64_t new_number, uint64_t now_ms, uint64_t ttl_ms) {
  if (tag >= HEADER_TAG_COUNT) return false;
  return tag_should_apply_write(&g_header_tags[tag], new_number, now_ms, ttl_ms);
}
#endif

// -- Tag TTL Calculation --

static uint64_t header_tag_ttl_ms(chain_id_t chain_id, header_tag_t tag, prover_flags_t flags) {
  const chain_spec_t* spec          = c4_eth_get_chain_spec(chain_id);
  uint64_t            block_time_ms = is_gnosis_chain(chain_id) ? 5000 : 12000;

  switch (tag) {
    case HEADER_TAG_LATEST:
      return (flags & C4_PROVER_FLAG_LIGHT_CLIENT) ? block_time_ms : block_time_ms / 2;
    case HEADER_TAG_SAFE: {
      uint64_t slots_per_epoch = 1ULL << (spec ? spec->slots_per_epoch_bits : 5);
      return (slots_per_epoch / 2) * block_time_ms;
    }
    case HEADER_TAG_FINALIZED: {
      uint64_t slots_per_epoch = 1ULL << (spec ? spec->slots_per_epoch_bits : 5);
      return slots_per_epoch * block_time_ms;
    }
    default:
      return 0;
  }
}

// -- Hybrid: Fetch + Verify from Remote Prover --

typedef enum {
  HYBRID_FETCH_HEADER,
  HYBRID_FETCH_EXECUTION
} hybrid_fetch_type_t;

typedef struct {
  bytes32_t block_hash;
  bytes_t   el_header;
  ssz_ob_t  el_body;
} local_cache_entry_t;

// Resolves the `ETH_BLOCK_PROOF_UNION` variant of a hybrid response into the RLP EL header.
//
// - `clProof` variant: the union carries the full block proof and `elHeader` is inlined in
//   the PROVER response. We return a pointer into that response (borrow), same as before.
// - `blockHash` variant: the remote prover already knew that this client's `header_cache`
//   holds the verified header, so it omitted the block proof. We recover the RLP header
//   from either
//     (a) a `C4_DATA_TYPE_CACHE` snapshot that `verify_block_by_blockhash` (called inside
//         `c4_verify`) already attached to `ctx->state` on the fresh-verify path, or
//     (b) `c4_header_cache_get_el_header` (owned copy) which we then attach as a new
//         `C4_DATA_TYPE_CACHE` snapshot ourselves so the borrowed bytes stay valid for the
//         lifetime of the prover ctx and are freed by `c4_state_free` — same lifetime rule
//         as the verifier applies. Necessary for the `validated`-reentry path (no verify
//         run, so no snapshot exists yet) and as a safety net if the LRU already evicted
//         the snapshot between the first and later reentry.
//
// Cache-miss (advertised hash was evicted between fetch and response) becomes a hard error
// rather than silently returning `NULL_BYTES`, which used to blow up downstream with a NULL
// header — see the callers of `hybrid_fetch_and_verify` and the completeness path in
// `proof_logs_completeness.c` that checks `el_header.data` after this returns.
static c4_status_t resolve_el_header_from_block(prover_ctx_t* ctx, ssz_ob_t block, bytes_t* el_header) {
  if (!block.def) THROW_ERROR("invalid block proof: missing def");

  if (strcmp(block.def->name, "blockHash") == 0) {
    if (block.bytes.len != 32) THROW_ERROR("invalid block hash length in blockHash variant");
    const uint8_t* block_hash = block.bytes.data;

    data_request_t* snapshot = c4_state_get_data_request_by_id(&ctx->state, (uint8_t*) block_hash);
    if (snapshot && snapshot->type == C4_DATA_TYPE_CACHE && snapshot->response.data) {
      *el_header = snapshot->response;
      return C4_SUCCESS;
    }

    bytes_t cached = c4_header_cache_get_el_header(ctx->chain_id, block_hash, NULL);
    if (!cached.data)
      THROW_ERROR("blockHash variant references a header not in the verifier cache");

    snapshot           = safe_calloc(1, sizeof(data_request_t));
    snapshot->chain_id = ctx->chain_id;
    snapshot->type     = C4_DATA_TYPE_CACHE;
    snapshot->encoding = C4_DATA_ENCODING_SSZ;
    snapshot->response = cached;
    memcpy(snapshot->id, block_hash, 32);
    c4_state_add_request(&ctx->state, snapshot);
    *el_header = cached;
    return C4_SUCCESS;
  }

  if (block.def->type == SSZ_TYPE_CONTAINER) {
    *el_header = ssz_get(&block, "elHeader").bytes;
    return C4_SUCCESS;
  }

  THROW_ERROR("unexpected block proof variant");
}

#ifdef TEST
// Test-only entry point: builds a synthetic `blockHash`-variant ssz_ob_t and drives the
// resolve helper. Exists so `test/unittests/test_hybrid_blockhash_resolve.c` can exercise
// the three branches (state snapshot, header_cache copy, miss) without spinning up a full
// remote-prover fetch pipeline.
c4_status_t c4_hybrid_test_resolve_block_hash(prover_ctx_t* ctx, const uint8_t* hash, bytes_t* el_header) {
  static const ssz_def_t s_block_hash_def = SSZ_BYTES32("blockHash");
  ssz_ob_t               block            = {.def = &s_block_hash_def, .bytes = bytes((uint8_t*) hash, 32)};
  return resolve_el_header_from_block(ctx, block, el_header);
}
#endif

// fetches the blockheader or block from the prover and verifies it.
static c4_status_t hybrid_fetch_and_verify(prover_ctx_t* ctx, json_t block, hybrid_fetch_type_t type, bytes_t* el_header, ssz_ob_t* el_body) {
  const char* method          = type == HYBRID_FETCH_EXECUTION ? "eth_getBlockByNumber" : "eth_getBlockHeader";
  char        arg_buffer[200] = {0};
  char        pl_buffer[200]  = {0};
  bytes32_t   id              = {0};
  buffer_t    args            = stack_buffer(arg_buffer);
  buffer_t    pl              = stack_buffer(pl_buffer);

  bprintf(&args, "[%J%s]", block, type == HYBRID_FETCH_EXECUTION ? ",false" : "");
  bprintf(&pl, "{\"method\":\"%s\",\"params\":%s}", method, buffer_as_string(args));
  sha256(pl.data, id); // id derives from method+params only; version/client_state/flags go into the URL, not the id.
  data_request_t* data_request = c4_state_get_data_request_by_id(&ctx->state, id);

  if (data_request) {
    if (c4_state_is_pending(data_request)) return C4_PENDING;
    if (data_request->error) THROW_ERROR(data_request->error);
    if (!data_request->response.data) THROW_ERROR_WITH("empty response from remote prover for %s", method);
    if (data_request->validated) {
      ssz_ob_t response = {.def = c4_get_request_type(c4_chain_type(ctx->chain_id)), .bytes = data_request->response};
      ssz_ob_t proof    = ssz_get(&response, "proof");
      ssz_ob_t block    = ssz_get(&proof, "block");
      ssz_ob_t body     = ssz_get(&proof, "body");

      if (body.def && body.def->type == SSZ_TYPE_CONTAINER && el_body)
        *el_body = body;
      if (!el_header) return C4_SUCCESS;
      return resolve_el_header_from_block(ctx, block, el_header);
    }

    // NOTE: verify_flags are intentionally 0 here. prover_flags_t and verify_flags_t use
    // disjoint bitmasks, so the outer ctx->flags cannot be passed through verbatim. A
    // proper mapping (e.g. a future C4_PROVER_FLAG_SKIP_WSP_CHECK that translates to
    // VERIFY_FLAG_SKIP_WSP_CHECK) is tracked separately.
    verify_ctx_t verify_ctx = {0};
    c4_status_t  status     = c4_verify_init(&verify_ctx, data_request->response, method, json_parse(arg_buffer), ctx->chain_id, 0);
    if (status != C4_SUCCESS) {
      if (verify_ctx.state.error) c4_state_add_error(&ctx->state, verify_ctx.state.error);
      c4_verify_free_data(&verify_ctx);
      THROW_ERROR_WITH("failed to initialize verify context for %s", method);
    }

    // Lend the persistent request list of the outer prover ctx to verify_ctx so URL/id
    // lookups inside c4_verify (LC updates, checkpointz WSP anchor, ...) find responses
    // the host has already fulfilled on previous iterations. Move everything back
    // immediately so the prover ctx remains the single owner of the request list across
    // retries. (Previously this was implemented as a shallow struct copy
    // `verify_ctx.state = ctx->state;` -- functionally correct for the loop, but it
    // aliased the `error` pointer between both contexts and caused subtle double-free /
    // error-loss bugs on edge cases.)
    c4_state_take_requests(&verify_ctx.state, &ctx->state);
    status = c4_verify(&verify_ctx);
    c4_state_take_requests(&ctx->state, &verify_ctx.state);

    switch (status) {
      case C4_SUCCESS: {
        data_request->validated = true;
        ssz_ob_t block          = ssz_get(&verify_ctx.proof, "block");
        ssz_ob_t body           = ssz_get(&verify_ctx.proof, "body");

        if (body.def && body.def->type == SSZ_TYPE_CONTAINER && el_body)
          *el_body = body;
        // The verify_ctx's requests (including any header snapshot attached by
        // `verify_block_by_blockhash`) have already been moved back to ctx->state above,
        // so `resolve_el_header_from_block` can look up the snapshot there directly.
        if (el_header) status = resolve_el_header_from_block(ctx, block, el_header);
        break;
      }
      case C4_PENDING:
      case C4_ERROR:
        if (verify_ctx.state.error) c4_state_add_error(&ctx->state, verify_ctx.state.error);
        break;
    }

    // Safety net: if any unexpected requests remain on verify_ctx after the lend/return
    // (e.g. a future helper that adds requests after c4_verify returns), forward them
    // to the prover ctx instead of leaking them through c4_state_free below.
    c4_state_take_requests(&ctx->state, &verify_ctx.state);
    c4_verify_free_data(&verify_ctx);

    return status;
  }

  data_request           = safe_calloc(1, sizeof(data_request_t));
  data_request->type     = C4_DATA_TYPE_PROVER;
  data_request->chain_id = ctx->chain_id;
  data_request->method   = C4_DATA_METHOD_GET;
  data_request->encoding = C4_DATA_ENCODING_SSZ;
  data_request->url      = c4_eth_build_delegated_block_get_url(method, block, c4_current_version_number(),
                                                                (ctx->flags & C4_PROVER_FLAG_ZK_PROOF) != 0,
                                                                ctx->client_state, ctx->witness_key);
  data_request->ttl      = c4_eth_block_ttl_s(ctx->chain_id, block, (ctx->flags & C4_PROVER_FLAG_LIGHT_CLIENT) != 0);
  memcpy(data_request->id, id, 32);

  c4_state_add_request(&ctx->state, data_request);
  return C4_PENDING;
}

// -- Hybrid: Resolve Block Identifier and Return Header-Only beacon_block_t --

static c4_status_t create_beacon_block(prover_ctx_t* ctx, beacon_block_t* beacon_block, local_cache_entry_t* local) {
  memset(beacon_block, 0, sizeof(beacon_block_t));
  memcpy(beacon_block->el_block_hash, local->block_hash, 32);
  beacon_block->header_only = true;
  beacon_block->el_header   = local->el_header;
  if (local->el_body.def) beacon_block->el_body = local->el_body;
  beacon_block->slot = eth_el_header_get_uint64(local->el_header, EL_BLOCK_NUMBER);
  return C4_SUCCESS;
}
static inline bytes_t bytes_cpy(void* dst, size_t offset, bytes_t src) {
  memcpy(((uint8_t*) dst) + offset, src.data, src.len);
  return bytes((char*) dst + offset, src.len);
}
static c4_status_t store_local_cache_entry(prover_ctx_t* ctx, bytes32_t cache_key, verified_header_entry_t* cached, beacon_block_t* beacon_block) {
  uint32_t        size         = sizeof(local_cache_entry_t) + cached->el_header.len + (cached->el_body.def ? cached->el_body.bytes.len : 0);
  data_request_t* data_request = c4_state_get_data_request_by_id(&ctx->state, cache_key);
  if (data_request && data_request->validated && data_request->response.data)
    return create_beacon_block(ctx, beacon_block, (void*) data_request->response.data);

  local_cache_entry_t* local = safe_calloc(1, size);
  memcpy(local->block_hash, cached->block_hash, 32);
  local->el_header = bytes_cpy(local, sizeof(local_cache_entry_t), cached->el_header);
  if (cached->el_body.def) {
    local->el_body.def   = cached->el_body.def;
    local->el_body.bytes = bytes_cpy(local, sizeof(local_cache_entry_t) + cached->el_header.len, cached->el_body.bytes);
  }
  data_request            = safe_calloc(1, sizeof(data_request_t));
  data_request->type      = C4_DATA_TYPE_CACHE;
  data_request->chain_id  = ctx->chain_id;
  data_request->method    = C4_DATA_METHOD_GET;
  data_request->encoding  = C4_DATA_ENCODING_SSZ;
  data_request->response  = bytes((void*) local, size);
  data_request->validated = true;
  memcpy(data_request->id, cache_key, 32);
  c4_state_add_request(&ctx->state, data_request);

  return create_beacon_block(ctx, beacon_block, local);
}

c4_status_t c4_hybrid_get_block_for_eth(prover_ctx_t* ctx, json_t block, beacon_block_t* beacon_block, bool with_body) {
  header_tag_t tag          = HEADER_TAG_COUNT;
  bytes_t      el_header    = NULL_BYTES;
  ssz_ob_t     el_body      = {0};
  bytes32_t    cache_key    = {0};
  uint64_t     block_number = 0;
  bytes32_t    block_hash   = {0};

  if (strncmp(block.start, "\"latest\"", 8) == 0)
    tag = HEADER_TAG_LATEST;
  else if (strncmp(block.start, "\"safe\"", 6) == 0 || strncmp(block.start, "\"justified\"", 11) == 0)
    tag = HEADER_TAG_SAFE;
  else if (strncmp(block.start, "\"finalized\"", 11) == 0)
    tag = HEADER_TAG_FINALIZED;

  if (tag < HEADER_TAG_COUNT) {
    memcpy(cache_key, "tag", 3);
    memcpy(cache_key + 3, &tag, 4);
    tag_cache_entry_t* tc  = &g_header_tags[tag];
    uint64_t           now = current_ms();
    if (tc->cached_at_ms && !bytes_all_zero(bytes(tc->block_hash, 32))) {
      bool within_ttl             = (now - tc->cached_at_ms < header_tag_ttl_ms(ctx->chain_id, tag, ctx->flags));
      bool stale_while_revalidate = !within_ttl && tc->fetching_since_ms && tc->fetching_ctx != (uintptr_t) ctx && (now - tc->fetching_since_ms < TAG_FETCH_TIMEOUT_MS);
      if (within_ttl || stale_while_revalidate)
        memcpy(block_hash, tc->block_hash, 32);
    }
  }
  else if (block.type == JSON_TYPE_STRING && block.len == 68) {
    hex_to_bytes(block.start + 1, 66, bytes(block_hash, 32));
    memcpy(cache_key, block_hash, 32);
    memcpy(cache_key, "hash", 4);
  }
  else if (block.type == JSON_TYPE_STRING && block.len > 4 && block.start[1] == '0' && block.start[2] == 'x') {
    block_number = json_as_uint64(block);
    memcpy(cache_key, "number", 6);
    memcpy(cache_key + 6, &block_number, 8);
  }
  cache_key[31] = (uint8_t) with_body;

  // check local cache first
  data_request_t*      data_request = c4_state_get_data_request_by_id(&ctx->state, cache_key);
  local_cache_entry_t* local        = data_request ? (local_cache_entry_t*) data_request->response.data : NULL;
  if (local && (!with_body || local->el_body.def))
    return create_beacon_block(ctx, beacon_block, local);

  // now check header_cache
  verified_header_entry_t* cached = NULL;
  if (block_number)
    cached = c4_header_cache_get_by_number(ctx->chain_id, block_number);
  else if (!bytes_all_zero(bytes(block_hash, 32)))
    cached = c4_header_cache_get_by_hash(ctx->chain_id, block_hash);

  if (cached && (!with_body || cached->el_body.def))
    return store_local_cache_entry(ctx, cache_key, cached, beacon_block);

  // no cached entry yet, we need a request.
  if (tag < HEADER_TAG_COUNT && !g_header_tags[tag].fetching_since_ms) {
    g_header_tags[tag].fetching_since_ms = current_ms();
    g_header_tags[tag].fetching_ctx      = (uintptr_t) ctx;
  }
  c4_status_t status = hybrid_fetch_and_verify(ctx, block, with_body ? HYBRID_FETCH_EXECUTION : HYBRID_FETCH_HEADER, &el_header, with_body ? &el_body : NULL);

  // Defensive: `resolve_el_header_from_block` already turns a `blockHash`-variant cache miss
  // into `C4_ERROR`, but downstream callers (`keccak`, `eth_el_header_get_uint64`) still deref
  // el_header directly, so guard against any future path that would leak an empty header.
  if (status == C4_SUCCESS && (!el_header.data || !el_header.len)) {
    c4_state_add_error(&ctx->state, "hybrid fetch succeeded but returned no execution header");
    status = C4_ERROR;
  }

  if (status != C4_SUCCESS) {
    if (status == C4_ERROR && tag < HEADER_TAG_COUNT) {
      g_header_tags[tag].fetching_since_ms = 0;
      g_header_tags[tag].fetching_ctx      = 0;
    }
    return status;
  }

  local_cache_entry_t result = {
      .el_body   = el_body,
      .el_header = el_header};
  memcpy(result.block_hash, block_hash, 32);

  keccak(el_header, result.block_hash);
  uint64_t new_block_number = eth_el_header_get_uint64(el_header, EL_BLOCK_NUMBER);
  c4_header_cache_put(ctx->chain_id, new_block_number, result.block_hash, el_header, with_body ? &result.el_body : NULL);
  if (tag < HEADER_TAG_COUNT) {
    tag_cache_entry_t* tc = &g_header_tags[tag];
    // Sentinels are cleared unconditionally: they are a per-fetch marker for the fetching
    // ctx and must not survive, no matter whether the tag value ends up being written.
    tc->fetching_since_ms = 0;
    tc->fetching_ctx      = 0;
    if (!bytes_all_zero(bytes(result.block_hash, 32))) {
      uint64_t now    = current_ms();
      uint64_t ttl_ms = header_tag_ttl_ms(ctx->chain_id, tag, ctx->flags);
      if (tag_should_apply_write(tc, new_block_number, now, ttl_ms)) {
        memcpy(tc->block_hash, result.block_hash, 32);
        tc->block_number = new_block_number;
        tc->cached_at_ms = now;
      }
    }
  }

  return create_beacon_block(ctx, beacon_block, &result);
}
