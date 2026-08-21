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

typedef struct {
  bytes32_t block_hash;
  bytes_t   el_header;
  ssz_ob_t  el_body;
} local_cache_entry_t;

static void free_local_cache_entry(local_cache_entry_t* local) {
  safe_free(local->el_header.data);
  if (local->el_body.def)
    safe_free(local->el_body.bytes.data);
  safe_free(local);
}
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
  uintptr_t fetching_ctx;      /**< opaque identity of the fetching prover context (compared, never dereferenced) */
} tag_cache_entry_t;

static tag_cache_entry_t g_header_tags[HEADER_TAG_COUNT] = {0};

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

// -- Helper: Build header_data from execution payload --

ssz_ob_t c4_build_header_data_from_execution(ssz_ob_t execution) {
  ssz_builder_t data = ssz_builder_for_type(ETH_SSZ_DATA_BLOCK_HEADER);
  ssz_add_bytes(&data, "parentHash", ssz_get(&execution, "parentHash").bytes);
  ssz_add_bytes(&data, "stateRoot", ssz_get(&execution, "stateRoot").bytes);
  ssz_add_bytes(&data, "receiptsRoot", ssz_get(&execution, "receiptsRoot").bytes);
  ssz_add_bytes(&data, "logsBloom", ssz_get(&execution, "logsBloom").bytes);
  ssz_add_bytes(&data, "blockNumber", ssz_get(&execution, "blockNumber").bytes);
  ssz_add_bytes(&data, "gasLimit", ssz_get(&execution, "gasLimit").bytes);
  ssz_add_bytes(&data, "gasUsed", ssz_get(&execution, "gasUsed").bytes);
  ssz_add_bytes(&data, "timestamp", ssz_get(&execution, "timestamp").bytes);
  ssz_add_bytes(&data, "baseFeePerGas", ssz_get(&execution, "baseFeePerGas").bytes);
  ssz_add_bytes(&data, "blockHash", ssz_get(&execution, "blockHash").bytes);
  ssz_add_bytes(&data, "blobGasUsed", ssz_get(&execution, "blobGasUsed").bytes);
  ssz_add_bytes(&data, "excessBlobGas", ssz_get(&execution, "excessBlobGas").bytes);
  ssz_add_bytes(&data, "feeRecipient", ssz_get(&execution, "feeRecipient").bytes);
  bytes32_t tx_root = {0};
  ssz_hash_tree_root(ssz_get(&execution, "transactions"), tx_root);
  ssz_add_bytes(&data, "transactionsRoot", bytes(tx_root, 32));
  return ssz_builder_to_bytes(&data);
}

// -- Hybrid: Fetch + Verify from Remote Prover --

typedef enum {
  HYBRID_FETCH_HEADER,
  HYBRID_FETCH_EXECUTION
} hybrid_fetch_type_t;

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

      if (strcmp(block.def->name, "blockHash") == 0) // this should not happens since the prover thought we already have the header.
        *el_header = NULL_BYTES;
      else if (block.def && block.def->type == SSZ_TYPE_CONTAINER && el_header)
        *el_header = ssz_get(&block, "elHeader").bytes;
      return C4_SUCCESS;
    }

    verify_ctx_t verify_ctx = {0};
    // NOTE: verify_flags are intentionally 0 here. prover_flags_t and verify_flags_t use
    // disjoint bitmasks, so the outer ctx->flags cannot be passed through verbatim. A
    // proper mapping (e.g. a future C4_PROVER_FLAG_SKIP_WSP_CHECK that translates to
    // VERIFY_FLAG_SKIP_WSP_CHECK) is tracked separately.
    c4_status_t status = c4_verify_init(&verify_ctx, data_request->response, method, json_parse(arg_buffer), ctx->chain_id, 0);
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

        if (strcmp(block.def->name, "blockHash") == 0) // this should not happens since the prover thought we already have the header.
          *el_header = NULL_BYTES;
        else if (block.def && block.def->type == SSZ_TYPE_CONTAINER && el_header)
          *el_header = ssz_get(&block, "elHeader").bytes;
        break;
      }
      case C4_PENDING:
        // If verify_ctx accumulated an error while still returning PENDING (rare, but
        // possible via nested verification paths), forward it so it is not lost when
        // c4_verify_free_data releases verify_ctx.state below.
        if (verify_ctx.state.error) c4_state_add_error(&ctx->state, verify_ctx.state.error);
        break;
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

  data_request = safe_calloc(1, sizeof(data_request_t));
  memcpy(data_request->id, id, 32);
  data_request->type     = C4_DATA_TYPE_PROVER;
  data_request->chain_id = ctx->chain_id;
  data_request->method   = C4_DATA_METHOD_GET;
  data_request->encoding = C4_DATA_ENCODING_SSZ;
  data_request->url      = c4_eth_build_delegated_block_get_url(method, block, c4_current_version_number(),
                                                                (ctx->flags & C4_PROVER_FLAG_ZK_PROOF) != 0,
                                                                ctx->client_state, ctx->witness_key);
  data_request->ttl      = c4_eth_block_ttl_s(ctx->chain_id, block, (ctx->flags & C4_PROVER_FLAG_LIGHT_CLIENT) != 0);

  c4_state_add_request(&ctx->state, data_request);
  return C4_PENDING;
}

// -- Hybrid: Resolve Block Identifier and Return Header-Only beacon_block_t --

static c4_status_t create_beacon_block(prover_ctx_t* ctx, beacon_block_t* beacon_block, local_cache_entry_t* local) {
  memset(beacon_block, 0, sizeof(beacon_block_t));
  beacon_block->header_only = true;
  memcpy(beacon_block->el_block_hash, local->block_hash, 32);
  beacon_block->el_header = local->el_header;
  if (local->el_body.def)
    beacon_block->el_body = local->el_body;
  beacon_block->slot = eth_el_header_get_uint64(local->el_header, EL_BLOCK_NUMBER);
  return C4_SUCCESS;
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
      uint64_t ttl                    = header_tag_ttl_ms(ctx->chain_id, tag, ctx->flags);
      bool     within_ttl             = (now - tc->cached_at_ms < ttl);
      bool     stale_while_revalidate = !within_ttl && tc->fetching_since_ms && tc->fetching_ctx != (uintptr_t) ctx && (now - tc->fetching_since_ms < TAG_FETCH_TIMEOUT_MS);
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
#ifdef PROVER_CACHE
  // check local cache first
  local_cache_entry_t* local = c4_prover_cache_get_local(ctx, cache_key);
  if (local && (!with_body || local->el_body.def))
    return create_beacon_block(ctx, beacon_block, local);

#endif

  // now check header_cache
  verified_header_entry_t* cached = NULL;
  if (block_number)
    cached = c4_header_cache_get_by_number(ctx->chain_id, block_number);
  else if (!bytes_all_zero(bytes(block_hash, 32)))
    cached = c4_header_cache_get_by_hash(ctx->chain_id, block_hash);

  if (cached && (!with_body || cached->el_body.def)) {
#ifdef PROVER_CACHE
    // check local cache first
    uint32_t             size  = sizeof(local_cache_entry_t) + cached->el_header.len + (cached->el_body.def ? cached->el_body.bytes.len : 0);
    local_cache_entry_t* local = safe_calloc(1, sizeof(local_cache_entry_t));
    memcpy(local->block_hash, cached->block_hash, 32);
    local->el_header = bytes_dup(cached->el_header);
    if (cached->el_body.def) {
      local->el_body.def   = cached->el_body.def;
      local->el_body.bytes = bytes_dup(cached->el_body.bytes);
    }
    c4_prover_cache_set(ctx, cache_key, local, size, 0, free_local_cache_entry);
    return create_beacon_block(ctx, beacon_block, local);
#else
    THROW_ERROR("PROVER_CACHE is not enabled, but needed for hybrid");
#endif
  }

  // no cached entry yet, we need a request.

  if (tag < HEADER_TAG_COUNT && !g_header_tags[tag].fetching_since_ms) {
    g_header_tags[tag].fetching_since_ms = current_ms();
    g_header_tags[tag].fetching_ctx      = (uintptr_t) ctx;
  }
  c4_status_t status = hybrid_fetch_and_verify(ctx, block, with_body ? HYBRID_FETCH_EXECUTION : HYBRID_FETCH_HEADER, &el_header, with_body ? &el_body : NULL);

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

  block_number = eth_el_header_get_uint64(el_header, EL_BLOCK_NUMBER);
  keccak(el_header, result.block_hash);
  c4_header_cache_put(ctx->chain_id, block_number, result.block_hash, el_header, with_body ? &result.el_body : NULL);
  if (tag < HEADER_TAG_COUNT && !bytes_all_zero(bytes(block_hash, 32))) {
    memcpy(g_header_tags[tag].block_hash, result.block_hash, 32);
    g_header_tags[tag].cached_at_ms      = current_ms();
    g_header_tags[tag].fetching_since_ms = 0;
    g_header_tags[tag].fetching_ctx      = 0;
  }

  return create_beacon_block(ctx, beacon_block, &result);
}
