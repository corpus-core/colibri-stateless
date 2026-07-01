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
#include "json.h"
#include "prover.h"
#include "verify.h"
#include "version.h"
#include <stdlib.h>
#include <string.h>

// -- Global Header Cache (hybrid mode only) --

#define TAG_FETCH_TIMEOUT_MS 30000 /**< max time a stale-while-revalidate sentinel stays valid before allowing a retry */

static verified_header_cache_t g_header_cache = {0};

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

// -- Hybrid: Cache-friendly GET request construction --

char* eth_build_delegated_block_get_url(const char* method, json_t block, uint32_t version,
                                        prover_flags_t flags, bytes_t client_state, bytes_t witness_key) {
  buffer_t url = {0};
  // `%j` emits the raw block identifier without surrounding quotes (e.g. `latest`, `0x1234`),
  // which is URL-safe for tags and hex values alike. The `includeTx` boolean is intentionally
  // omitted (irrelevant for the proof).
  bprintf(&url, "proof/%s/%j/%d/%s/", method, block, version,
          (flags & C4_PROVER_FLAG_ZK_PROOF) ? "zk" : "std");
  if (client_state.data && client_state.len)
    bprintf(&url, "0x%x", client_state);
  else
    bprintf(&url, "0x");
  // Signer/witness keys go into a query parameter: the proof stays deterministic per signer set
  // and therefore cacheable, while keeping the (large) key list out of the cache-key path segments.
  if (witness_key.data && witness_key.len)
    bprintf(&url, "?signers=0x%x", witness_key);
  return (char*) url.data.data;
}

// Maps a block identifier to a cache freshness bound (seconds) mirroring `header_tag_ttl_ms`.
// Concrete block numbers/hashes are immutable and need no bound (returns 0).
static uint32_t hybrid_block_cache_max_age(chain_id_t chain_id, json_t block, prover_flags_t flags) {
  header_tag_t tag = HEADER_TAG_COUNT;
  if (strncmp(block.start, "\"latest\"", 8) == 0)
    tag = HEADER_TAG_LATEST;
  else if (strncmp(block.start, "\"safe\"", 6) == 0 || strncmp(block.start, "\"justified\"", 11) == 0)
    tag = HEADER_TAG_SAFE;
  else if (strncmp(block.start, "\"finalized\"", 11) == 0)
    tag = HEADER_TAG_FINALIZED;
  if (tag >= HEADER_TAG_COUNT) return 0;
  return (uint32_t) (header_tag_ttl_ms(chain_id, tag, flags) / 1000);
}

// -- Header Cache Implementation --

const verified_header_entry_t* c4_header_cache_get_by_number(const verified_header_cache_t* cache, chain_id_t chain_id, uint64_t block_number) {
  if (!cache) return NULL;
  for (uint32_t i = 0; i < cache->count; i++) {
    const verified_header_entry_t* e = &cache->entries[i];
    if (e->chain_id == chain_id && e->block_number == block_number && e->header_data.bytes.data)
      return e;
  }
  return NULL;
}

const verified_header_entry_t* c4_header_cache_get_by_hash(const verified_header_cache_t* cache, chain_id_t chain_id, const uint8_t* block_hash) {
  if (!cache || !block_hash) return NULL;
  for (uint32_t i = 0; i < cache->count; i++) {
    const verified_header_entry_t* e = &cache->entries[i];
    if (e->chain_id == chain_id && e->header_data.bytes.data && memcmp(e->block_hash, block_hash, 32) == 0)
      return e;
  }
  return NULL;
}

static void header_cache_free_entry(verified_header_entry_t* e) {
  safe_free(e->header_data.bytes.data);
  safe_free(e->execution.bytes.data);
  e->header_data = (ssz_ob_t) {0};
  e->execution   = (ssz_ob_t) {0};
}

void c4_header_cache_put(verified_header_cache_t* cache, chain_id_t chain_id, uint64_t block_number, const uint8_t* block_hash, ssz_ob_t header_data) {
  if (!cache || !block_hash || !header_data.bytes.data) return;

  for (uint32_t i = 0; i < cache->count; i++) {
    verified_header_entry_t* e = &cache->entries[i];
    if (e->chain_id == chain_id && e->block_number == block_number) {
      header_cache_free_entry(e);
      e->header_data.bytes = bytes_dup(header_data.bytes);
      e->header_data.def   = header_data.def;
      e->cached_at_ms      = current_ms();
      memcpy(e->block_hash, block_hash, 32);
      return;
    }
  }

  verified_header_entry_t* slot = &cache->entries[cache->head_idx];
  header_cache_free_entry(slot);
  slot->chain_id     = chain_id;
  slot->block_number = block_number;
  memcpy(slot->block_hash, block_hash, 32);
  slot->header_data.bytes = bytes_dup(header_data.bytes);
  slot->header_data.def   = header_data.def;
  slot->cached_at_ms      = current_ms();

  cache->head_idx = (cache->head_idx + 1) % HEADER_CACHE_SIZE;
  if (cache->count < HEADER_CACHE_SIZE)
    cache->count++;
}

void c4_header_cache_set_execution(verified_header_cache_t* cache, chain_id_t chain_id, uint64_t block_number, ssz_ob_t execution) {
  if (!cache || !execution.bytes.data) return;
  for (uint32_t i = 0; i < cache->count; i++) {
    verified_header_entry_t* e = &cache->entries[i];
    if (e->chain_id == chain_id && e->block_number == block_number && e->header_data.bytes.data) {
      safe_free(e->execution.bytes.data);
      e->execution.bytes = bytes_dup(execution.bytes);
      e->execution.def   = execution.def;
      return;
    }
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

static c4_status_t hybrid_fetch_and_verify(prover_ctx_t* ctx, json_t block, hybrid_fetch_type_t type, ssz_ob_t* result_out) {
  const char* method          = type == HYBRID_FETCH_EXECUTION ? "eth_getBlockByNumber" : "eth_getBlockHeader";
  char        arg_buffer[100] = {0};
  bytes32_t   id              = {0};
  buffer_t    id_buffer       = {0};
  // The trailing ",false" (includeTx) for eth_getBlockByNumber is irrelevant for the proof itself
  // (the full execution payload is always included). It is only kept here so the local dedup id
  // stays stable and unambiguous per method+block.
  sbprintf(arg_buffer, "[%J%s]", block, type == HYBRID_FETCH_EXECUTION ? ",false" : "");
  bprintf(&id_buffer, "{\"method\":\"%s\",\"params\":%s}", method, arg_buffer);
  sha256(id_buffer.data, id); // id derives from method+params only; version/client_state/flags go into the URL, not the id.
  buffer_free(&id_buffer);
  data_request_t* data_request = c4_state_get_data_request_by_id(&ctx->state, id);

  if (data_request) {
    if (c4_state_is_pending(data_request)) return C4_PENDING;
    if (data_request->error) THROW_ERROR(data_request->error);
    if (!data_request->response.data) THROW_ERROR_WITH("empty response from remote prover for %s", method);

    const ssz_def_t* def = type == HYBRID_FETCH_EXECUTION
                               ? c4_eth_execution_payload_def(ctx->chain_id)
                               : eth_ssz_verification_type(ETH_SSZ_DATA_BLOCK_HEADER);

    if (data_request->validated) {
      *result_out = (ssz_ob_t) {.def = def, .bytes = data_request->response};
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
        bytes_t result;
        if (type == HYBRID_FETCH_EXECUTION) {
          ssz_ob_t ep = ssz_get(&verify_ctx.proof, "executionPayload");
          if (!ep.bytes.data) {
            status = c4_state_add_error(&ctx->state, "no executionPayload in proof");
            break;
          }
          else
            result = bytes_dup(ep.bytes);
        }
        else
          result = bytes_dup(verify_ctx.data.bytes);

        // after we have verified the proof, we replace the response with the verified result, so it will be freed by the prover_ctx.
        safe_free(data_request->response.data);
        data_request->response = result;
        *result_out            = (ssz_ob_t) {.def = def, .bytes = result};
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
  data_request->type          = C4_DATA_TYPE_PROVER;
  data_request->chain_id      = ctx->chain_id;
  data_request->method        = C4_DATA_METHOD_GET;
  data_request->encoding      = C4_DATA_ENCODING_SSZ;
  data_request->url           = eth_build_delegated_block_get_url(method, block, c4_current_version_number(),
                                                                  ctx->flags, ctx->client_state, ctx->witness_key);
  data_request->cache_max_age = hybrid_block_cache_max_age(ctx->chain_id, block, ctx->flags);

  c4_state_add_request(&ctx->state, data_request);
  return C4_PENDING;
}

// -- Hybrid: Resolve Block Identifier and Return Header-Only beacon_block_t --

c4_status_t c4_hybrid_get_block_for_eth(prover_ctx_t* ctx, json_t block, beacon_block_t* beacon_block) {
  ssz_ob_t                       header_data = {0};
  const verified_header_entry_t* cached      = NULL;
  header_tag_t                   tag         = HEADER_TAG_COUNT;

  if (strncmp(block.start, "\"latest\"", 8) == 0)
    tag = HEADER_TAG_LATEST;
  else if (strncmp(block.start, "\"safe\"", 6) == 0 || strncmp(block.start, "\"justified\"", 11) == 0)
    tag = HEADER_TAG_SAFE;
  else if (strncmp(block.start, "\"finalized\"", 11) == 0)
    tag = HEADER_TAG_FINALIZED;

  if (tag < HEADER_TAG_COUNT) {
    tag_cache_entry_t* tc  = &g_header_cache.tags[tag];
    uint64_t           now = current_ms();
    if (tc->cached_at_ms && !bytes_all_zero(bytes(tc->block_hash, 32))) {
      uint64_t ttl                    = header_tag_ttl_ms(ctx->chain_id, tag, ctx->flags);
      bool     within_ttl             = (now - tc->cached_at_ms < ttl);
      bool     stale_while_revalidate = !within_ttl && tc->fetching_since_ms && tc->fetching_ctx != (uintptr_t) ctx && (now - tc->fetching_since_ms < TAG_FETCH_TIMEOUT_MS);
      if (within_ttl || stale_while_revalidate)
        cached = c4_header_cache_get_by_hash(&g_header_cache, ctx->chain_id, tc->block_hash);
    }
  }
  else if (block.type == JSON_TYPE_STRING && block.len == 68) {
    bytes32_t hash_buf = {0};
    hex_to_bytes(block.start + 1, 66, bytes(hash_buf, 32));
    cached = c4_header_cache_get_by_hash(&g_header_cache, ctx->chain_id, hash_buf);
  }
  else if (block.type == JSON_TYPE_STRING && block.len > 4 && block.start[1] == '0' && block.start[2] == 'x') {
    uint64_t bn = json_as_uint64(block);
    if (bn) cached = c4_header_cache_get_by_number(&g_header_cache, ctx->chain_id, bn);
  }

  if (cached) {
    header_data.def   = cached->header_data.def;
    header_data.bytes = bytes_dup(cached->header_data.bytes);
  }
  else {
    if (tag < HEADER_TAG_COUNT && !g_header_cache.tags[tag].fetching_since_ms) {
      g_header_cache.tags[tag].fetching_since_ms = current_ms();
      g_header_cache.tags[tag].fetching_ctx      = (uintptr_t) ctx;
    }
    c4_status_t status = hybrid_fetch_and_verify(ctx, block, HYBRID_FETCH_HEADER, &header_data);
    if (status != C4_SUCCESS) {
      if (status == C4_ERROR && tag < HEADER_TAG_COUNT) {
        g_header_cache.tags[tag].fetching_since_ms = 0;
        g_header_cache.tags[tag].fetching_ctx      = 0;
      }
      return status;
    }
    uint64_t hdr_bn = ssz_get_uint64(&header_data, "blockNumber");
    bytes_t  bh     = ssz_get(&header_data, "blockHash").bytes;
    c4_header_cache_put(&g_header_cache, ctx->chain_id, hdr_bn, bh.data, header_data);
    if (tag < HEADER_TAG_COUNT && bh.data && bh.len == 32) {
      memcpy(g_header_cache.tags[tag].block_hash, bh.data, 32);
      g_header_cache.tags[tag].cached_at_ms      = current_ms();
      g_header_cache.tags[tag].fetching_since_ms = 0;
      g_header_cache.tags[tag].fetching_ctx      = 0;
    }
  }

  memset(beacon_block, 0, sizeof(beacon_block_t));
  beacon_block->header_only = true;
  beacon_block->execution   = header_data;
  beacon_block->slot        = ssz_get_uint64(&header_data, "blockNumber");
  bytes_t bh                = ssz_get(&header_data, "blockHash").bytes;
  if (bh.data && bh.len == 32) memcpy(beacon_block->data_block_root, bh.data, 32);
  return C4_SUCCESS;
}

// -- Hybrid: Resolve Block + Fetch Full Execution Payload --

c4_status_t c4_hybrid_get_execution_for_eth(prover_ctx_t* ctx, json_t block, beacon_block_t* beacon_block) {
  const verified_header_entry_t* cached = NULL;
  header_tag_t                   tag    = HEADER_TAG_COUNT;

  if (strncmp(block.start, "\"latest\"", 8) == 0)
    tag = HEADER_TAG_LATEST;
  else if (strncmp(block.start, "\"safe\"", 6) == 0 || strncmp(block.start, "\"justified\"", 11) == 0)
    tag = HEADER_TAG_SAFE;
  else if (strncmp(block.start, "\"finalized\"", 11) == 0)
    tag = HEADER_TAG_FINALIZED;

  if (tag < HEADER_TAG_COUNT) {
    tag_cache_entry_t* tc  = &g_header_cache.tags[tag];
    uint64_t           now = current_ms();
    if (tc->cached_at_ms && !bytes_all_zero(bytes(tc->block_hash, 32))) {
      uint64_t ttl                    = header_tag_ttl_ms(ctx->chain_id, tag, ctx->flags);
      bool     within_ttl             = (now - tc->cached_at_ms < ttl);
      bool     stale_while_revalidate = !within_ttl && tc->fetching_since_ms && tc->fetching_ctx != (uintptr_t) ctx && (now - tc->fetching_since_ms < TAG_FETCH_TIMEOUT_MS);
      if (within_ttl || stale_while_revalidate)
        cached = c4_header_cache_get_by_hash(&g_header_cache, ctx->chain_id, tc->block_hash);
    }
  }
  else if (block.type == JSON_TYPE_STRING && block.len == 68) {
    bytes32_t hash_buf = {0};
    hex_to_bytes(block.start + 1, 66, bytes(hash_buf, 32));
    cached = c4_header_cache_get_by_hash(&g_header_cache, ctx->chain_id, hash_buf);
  }
  else if (block.type == JSON_TYPE_STRING && block.len > 4 && block.start[1] == '0' && block.start[2] == 'x') {
    uint64_t bn = json_as_uint64(block);
    if (bn) cached = c4_header_cache_get_by_number(&g_header_cache, ctx->chain_id, bn);
  }

  if (cached && cached->execution.bytes.data) {
    memset(beacon_block, 0, sizeof(beacon_block_t));
    beacon_block->execution = cached->execution;
    beacon_block->slot      = cached->block_number;
    memcpy(beacon_block->data_block_root, cached->block_hash, 32);
    return C4_SUCCESS;
  }

  if (tag < HEADER_TAG_COUNT && !g_header_cache.tags[tag].fetching_since_ms) {
    g_header_cache.tags[tag].fetching_since_ms = current_ms();
    g_header_cache.tags[tag].fetching_ctx      = (uintptr_t) ctx;
  }

  ssz_ob_t    execution = {0};
  c4_status_t status    = hybrid_fetch_and_verify(ctx, block, HYBRID_FETCH_EXECUTION, &execution);
  if (status != C4_SUCCESS) {
    if (status == C4_ERROR && tag < HEADER_TAG_COUNT) {
      g_header_cache.tags[tag].fetching_since_ms = 0;
      g_header_cache.tags[tag].fetching_ctx      = 0;
    }
    return status;
  }

  uint64_t ep_bn = ssz_get_uint64(&execution, "blockNumber");
  bytes_t  bh    = ssz_get(&execution, "blockHash").bytes;

  const verified_header_entry_t* hdr_cached = c4_header_cache_get_by_number(&g_header_cache, ctx->chain_id, ep_bn);
  if (!hdr_cached) {
    ssz_ob_t header_data = c4_build_header_data_from_execution(execution);
    c4_header_cache_put(&g_header_cache, ctx->chain_id, ep_bn, bh.data, header_data);
    safe_free(header_data.bytes.data);
  }
  c4_header_cache_set_execution(&g_header_cache, ctx->chain_id, ep_bn, execution);

  if (tag < HEADER_TAG_COUNT && bh.data && bh.len == 32) {
    memcpy(g_header_cache.tags[tag].block_hash, bh.data, 32);
    g_header_cache.tags[tag].cached_at_ms      = current_ms();
    g_header_cache.tags[tag].fetching_since_ms = 0;
    g_header_cache.tags[tag].fetching_ctx      = 0;
  }

  memset(beacon_block, 0, sizeof(beacon_block_t));
  beacon_block->execution = execution;
  beacon_block->slot      = ep_bn;
  if (bh.data && bh.len == 32) memcpy(beacon_block->data_block_root, bh.data, 32);
  return C4_SUCCESS;
}
