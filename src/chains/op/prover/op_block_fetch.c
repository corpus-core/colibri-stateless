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

#include "op_block_fetch.h"
#include "beacon_types.h"
#include "bytes.h"
#include "chains.h"
#include "crypto.h"
#include "json.h"
#include "op_payload.h"
#include "op_types.h"
#include "prover.h"
#include "state.h"
#include "verify.h"
#include "version.h"
#include <stdlib.h>
#include <string.h>

#define OP_TAG_FETCH_TIMEOUT_MS 30000

static c4_status_t op_hybrid_fetch_execution_from_prover(prover_ctx_t* ctx, json_t block, ssz_ob_t* execution_out) {
  const char* method          = "eth_getBlockByNumber";
  char        arg_buffer[100] = {0};
  bytes32_t   id              = {0};
  buffer_t    buffer          = {0};
  sbprintf(arg_buffer, "[%J,false]", block);
  bprintf(&buffer, "{\"method\":\"%s\",\"params\":%s", method, arg_buffer);
  sha256(buffer.data, id);
  c4_append_prover_request_props(&buffer, ctx->chain_id, ctx->flags, ctx->witness_key);
  bprintf(&buffer, "}");
  data_request_t* data_request = c4_state_get_data_request_by_id(&ctx->state, id);

  if (data_request) {
    buffer_free(&buffer);
    if (c4_state_is_pending(data_request)) return C4_PENDING;
    if (data_request->error) THROW_ERROR(data_request->error);
    if (!data_request->response.data) THROW_ERROR_WITH("empty response from remote prover for %s", method);

    const ssz_def_t* def = c4_eth_execution_payload_def(ctx->chain_id);

    if (data_request->validated) {
      *execution_out = (ssz_ob_t) {.def = def, .bytes = data_request->response};
      return C4_SUCCESS;
    }

    verify_ctx_t verify_ctx = {0};
    c4_status_t  status     = c4_verify_init(&verify_ctx, data_request->response, method, json_parse(arg_buffer), ctx->chain_id, 0);
    if (status != C4_SUCCESS) {
      c4_state_add_error(&ctx->state, verify_ctx.state.error);
      c4_verify_free_data(&verify_ctx);
      THROW_ERROR_WITH("failed to initialize verify context for %s", method);
    }

    verify_ctx.state = ctx->state;
    status           = c4_verify(&verify_ctx);
    switch (status) {
      case C4_SUCCESS: {
        data_request->validated = true;
        ssz_ob_t proof_ob       = verify_ctx.proof;
        ssz_ob_t block_proof_bp = ssz_get(&proof_ob, "block_proof");
        ssz_ob_t* ep_heap       = op_extract_verified_execution_payload(&verify_ctx, block_proof_bp, NULL, NULL);
        if (!ep_heap) {
          c4_state_add_error(&ctx->state, "OP hybrid fetch: invalid execution payload in proof");
          status = C4_ERROR;
          break;
        }
        bytes_t result = bytes_dup(ep_heap->bytes);
        safe_free(ep_heap);

        safe_free(data_request->response.data);
        data_request->response = result;
        *execution_out         = (ssz_ob_t) {.def = def, .bytes = result};
        break;
      }
      case C4_PENDING:
        break;
      case C4_ERROR:
        c4_state_add_error(&ctx->state, verify_ctx.state.error);
        break;
    }

    ctx->state.requests       = verify_ctx.state.requests;
    verify_ctx.state.requests = NULL;
    c4_verify_free_data(&verify_ctx);

    return status;
  }

  data_request = safe_calloc(1, sizeof(data_request_t));
  memcpy(data_request->id, id, 32);
  data_request->type     = C4_DATA_TYPE_PROVER;
  data_request->chain_id = ctx->chain_id;
  data_request->method   = C4_DATA_METHOD_POST;
  data_request->encoding = C4_DATA_ENCODING_SSZ;
  data_request->payload  = buffer.data;

  c4_state_add_request(&ctx->state, data_request);
  return C4_PENDING;
}

c4_status_t c4_op_create_block_proof(prover_ctx_t* ctx, json_t block_number, ssz_builder_t* block_proof) {
  uint8_t  path[200]    = {0};
  buffer_t buf2         = stack_buffer(path);
  bytes_t  preconf_data = {0};

  if ((ctx->flags & C4_PROVER_FLAG_UNSTABLE_LATEST) == 0 && block_number.start[1] == 'l')
    bprintf(&buf2, "preconf/pre_latest");
  else
    bprintf(&buf2, "preconf/%j", block_number);

  TRY_ASYNC(c4_send_internal_request(ctx, (char*) buf2.data.data, NULL, 0, &preconf_data));
  if (!preconf_data.len) THROW_ERROR("No preconf data found, currently only supports preconfs");
  bytes_t payload   = bytes_slice(preconf_data, 0, preconf_data.len - 65);
  bytes_t signature = bytes_slice(preconf_data, preconf_data.len - 65, 65);

  ssz_builder_t preconf_proof              = ssz_builder_for_op_type(OP_SSZ_VERIFY_PRECONF_PROOF);
  ssz_builder_t payload_builder_compressed = ssz_builder_for_def(ssz_get_def(preconf_proof.def, "payload")->def.container.elements + 0);
  buffer_append(&payload_builder_compressed.fixed, payload);
  ssz_add_builders(&preconf_proof, "payload", payload_builder_compressed);
  ssz_add_bytes(&preconf_proof, "signature", signature);
  *block_proof = preconf_proof;

  return C4_SUCCESS;
}

c4_status_t c4_op_preconf_load_block(prover_ctx_t* ctx, json_t block, beacon_block_t* bb, ssz_builder_t* preconf_proof_out) {
  ssz_builder_t pb = {0};
  TRY_ASYNC(c4_op_create_block_proof(ctx, block, &pb));
  ssz_ob_t* ep = op_decode_preconf_builder_execution_payload(&pb);
  if (!ep) {
    ssz_builder_free(&pb);
    THROW_ERROR("failed to decode OP execution payload");
  }
  memset(bb, 0, sizeof(*bb));
  bb->execution.def   = ep->def;
  bb->execution.bytes = bytes_dup(ep->bytes);
  bb->slot            = ssz_get_uint64(ep, "blockNumber");
  bytes_t bh          = ssz_get(ep, "blockHash").bytes;
  if (bh.data && bh.len == 32)
    memcpy(bb->data_block_root, bh.data, 32);
  safe_free(ep);
  if (preconf_proof_out)
    *preconf_proof_out = pb;
  else
    ssz_builder_free(&pb);
  return C4_SUCCESS;
}

c4_status_t c4_op_hybrid_get_execution_for_chain(prover_ctx_t* ctx, json_t block, beacon_block_t* beacon_block) {
  verified_header_cache_t*       cache   = c4_header_cache_global();
  const verified_header_entry_t* cached  = NULL;
  header_tag_t                   tag     = HEADER_TAG_COUNT;

  if (strncmp(block.start, "\"latest\"", 8) == 0)
    tag = HEADER_TAG_LATEST;
  else if (strncmp(block.start, "\"safe\"", 6) == 0 || strncmp(block.start, "\"justified\"", 11) == 0)
    tag = HEADER_TAG_SAFE;
  else if (strncmp(block.start, "\"finalized\"", 11) == 0)
    tag = HEADER_TAG_FINALIZED;

  if (tag < HEADER_TAG_COUNT) {
    tag_cache_entry_t* tc  = &cache->tags[tag];
    uint64_t           now = current_ms();
    if (tc->cached_at_ms && !bytes_all_zero(bytes(tc->block_hash, 32))) {
      uint64_t ttl                    = c4_header_tag_ttl_ms(ctx->chain_id, tag, ctx->flags);
      bool     within_ttl             = (now - tc->cached_at_ms < ttl);
      bool     stale_while_revalidate = !within_ttl && tc->fetching_since_ms && tc->fetching_ctx != (uintptr_t) ctx && (now - tc->fetching_since_ms < OP_TAG_FETCH_TIMEOUT_MS);
      if (within_ttl || stale_while_revalidate)
        cached = c4_header_cache_get_by_hash(cache, ctx->chain_id, tc->block_hash);
    }
  }
  else if (block.type == JSON_TYPE_STRING && block.len == 68) {
    bytes32_t hash_buf = {0};
    hex_to_bytes(block.start + 1, 66, bytes(hash_buf, 32));
    cached = c4_header_cache_get_by_hash(cache, ctx->chain_id, hash_buf);
  }
  else if (block.type == JSON_TYPE_STRING && block.len > 4 && block.start[1] == '0' && block.start[2] == 'x') {
    uint64_t bn = json_as_uint64(block);
    if (bn) cached = c4_header_cache_get_by_number(cache, ctx->chain_id, bn);
  }

  if (cached && cached->execution.bytes.data) {
    memset(beacon_block, 0, sizeof(beacon_block_t));
    beacon_block->execution = cached->execution;
    beacon_block->slot      = cached->block_number;
    memcpy(beacon_block->data_block_root, cached->block_hash, 32);
    return C4_SUCCESS;
  }

  if (tag < HEADER_TAG_COUNT && !cache->tags[tag].fetching_since_ms) {
    cache->tags[tag].fetching_since_ms = current_ms();
    cache->tags[tag].fetching_ctx      = (uintptr_t) ctx;
  }

  ssz_ob_t    execution = {0};
  c4_status_t status    = op_hybrid_fetch_execution_from_prover(ctx, block, &execution);
  if (status != C4_SUCCESS) {
    if (status == C4_ERROR && tag < HEADER_TAG_COUNT) {
      cache->tags[tag].fetching_since_ms = 0;
      cache->tags[tag].fetching_ctx      = 0;
    }
    return status;
  }

  uint64_t ep_bn = ssz_get_uint64(&execution, "blockNumber");
  bytes_t  bh    = ssz_get(&execution, "blockHash").bytes;

  const verified_header_entry_t* hdr_cached = c4_header_cache_get_by_number(cache, ctx->chain_id, ep_bn);
  if (!hdr_cached) {
    ssz_ob_t header_data = c4_build_header_data_from_execution(execution);
    c4_header_cache_put(cache, ctx->chain_id, ep_bn, bh.data, header_data);
    safe_free(header_data.bytes.data);
  }
  c4_header_cache_set_execution(cache, ctx->chain_id, ep_bn, execution);

  if (tag < HEADER_TAG_COUNT && bh.data && bh.len == 32) {
    memcpy(cache->tags[tag].block_hash, bh.data, 32);
    cache->tags[tag].cached_at_ms      = current_ms();
    cache->tags[tag].fetching_since_ms = 0;
    cache->tags[tag].fetching_ctx      = 0;
  }

  memset(beacon_block, 0, sizeof(beacon_block_t));
  beacon_block->execution = execution;
  beacon_block->slot      = ep_bn;
  if (bh.data && bh.len == 32) memcpy(beacon_block->data_block_root, bh.data, 32);
  return C4_SUCCESS;
}

c4_status_t c4_op_hybrid_get_block_for_chain(prover_ctx_t* ctx, json_t block, beacon_block_t* beacon_block) {
  verified_header_cache_t*       cache       = c4_header_cache_global();
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
    tag_cache_entry_t* tc  = &cache->tags[tag];
    uint64_t           now = current_ms();
    if (tc->cached_at_ms && !bytes_all_zero(bytes(tc->block_hash, 32))) {
      uint64_t ttl                    = c4_header_tag_ttl_ms(ctx->chain_id, tag, ctx->flags);
      bool     within_ttl             = (now - tc->cached_at_ms < ttl);
      bool     stale_while_revalidate = !within_ttl && tc->fetching_since_ms && tc->fetching_ctx != (uintptr_t) ctx && (now - tc->fetching_since_ms < OP_TAG_FETCH_TIMEOUT_MS);
      if (within_ttl || stale_while_revalidate)
        cached = c4_header_cache_get_by_hash(cache, ctx->chain_id, tc->block_hash);
    }
  }
  else if (block.type == JSON_TYPE_STRING && block.len == 68) {
    bytes32_t hash_buf = {0};
    hex_to_bytes(block.start + 1, 66, bytes(hash_buf, 32));
    cached = c4_header_cache_get_by_hash(cache, ctx->chain_id, hash_buf);
  }
  else if (block.type == JSON_TYPE_STRING && block.len > 4 && block.start[1] == '0' && block.start[2] == 'x') {
    uint64_t bn = json_as_uint64(block);
    if (bn) cached = c4_header_cache_get_by_number(cache, ctx->chain_id, bn);
  }

  if (cached) {
    header_data.def   = cached->header_data.def;
    header_data.bytes = bytes_dup(cached->header_data.bytes);
  }
  else {
    if (tag < HEADER_TAG_COUNT && !cache->tags[tag].fetching_since_ms) {
      cache->tags[tag].fetching_since_ms = current_ms();
      cache->tags[tag].fetching_ctx      = (uintptr_t) ctx;
    }
    ssz_ob_t execution = {0};
    c4_status_t status = op_hybrid_fetch_execution_from_prover(ctx, block, &execution);
    if (status != C4_SUCCESS) {
      if (status == C4_ERROR && tag < HEADER_TAG_COUNT) {
        cache->tags[tag].fetching_since_ms = 0;
        cache->tags[tag].fetching_ctx      = 0;
      }
      return status;
    }
    header_data = c4_build_header_data_from_execution(execution);
    safe_free(execution.bytes.data);

    uint64_t hdr_bn = ssz_get_uint64(&header_data, "blockNumber");
    bytes_t  bh     = ssz_get(&header_data, "blockHash").bytes;
    c4_header_cache_put(cache, ctx->chain_id, hdr_bn, bh.data, header_data);
    if (tag < HEADER_TAG_COUNT && bh.data && bh.len == 32) {
      memcpy(cache->tags[tag].block_hash, bh.data, 32);
      cache->tags[tag].cached_at_ms      = current_ms();
      cache->tags[tag].fetching_since_ms = 0;
      cache->tags[tag].fetching_ctx      = 0;
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
