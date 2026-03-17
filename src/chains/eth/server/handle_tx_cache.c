/*
 * Copyright (c) 2025,2026 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "handler.h"
#include "pap_tx_cache_types.h"
#include "server.h"
#include "tx_cache.h"
#include <string.h>

#ifdef PROVER_CACHE

#define TX_CACHE_DEFAULT_MAX_BLOCKS 256
#define TX_CACHE_MAX_BLOCKS_LIMIT   10000

uint64_t c4_get_query(char* query, char* param);

typedef struct {
  ssz_builder_t list_builder;
  uint32_t      block_count;
  uint64_t      from_block;
  uint32_t      max_blocks;
  uint32_t      skip;
  uint32_t      eligible;
} tx_cache_ssz_ctx_t;

static void count_eligible_blocks(uint64_t block_number, const bytes32_t* tx_hashes,
                                  uint32_t count, void* user_data) {
  tx_cache_ssz_ctx_t* ctx = (tx_cache_ssz_ctx_t*) user_data;
  (void) tx_hashes;
  (void) count;
  if (block_number >= ctx->from_block)
    ctx->eligible++;
}

static void build_block_ssz(uint64_t block_number, const bytes32_t* tx_hashes,
                            uint32_t count, void* user_data) {
  tx_cache_ssz_ctx_t* ctx = (tx_cache_ssz_ctx_t*) user_data;

  if (block_number < ctx->from_block) return;
  ctx->eligible++;
  if (ctx->eligible <= ctx->skip) return;
  if (ctx->block_count >= ctx->max_blocks) return;

  ssz_builder_t block_builder = ssz_builder_for_def(&PAP_TX_CACHE_BLOCK);
  ssz_add_uint64(&block_builder, block_number);
  ssz_add_bytes(&block_builder, "tx_hashes",
                bytes((uint8_t*) tx_hashes, count * BYTES32_SIZE));

  ssz_add_dynamic_list_builders(&ctx->list_builder, 0, block_builder);
  ctx->block_count++;
}

bool c4_handle_tx_cache(client_t* client) {
  if (strncmp(client->request.path, "/tx_cache", 9) != 0) return false;
  char next = client->request.path[9];
  if (next && next != '?' && next != '/') return false;

  if (client->request.method != C4_DATA_METHOD_GET) {
    c4_write_error_response(client, 405, "Method Not Allowed");
    return true;
  }

  if (c4_eth_tx_cache_size() == 0) {
    c4_http_respond(client, 200, "application/octet-stream", NULL_BYTES);
    return true;
  }

  uint64_t from_block = 0;
  uint32_t max_blocks = TX_CACHE_DEFAULT_MAX_BLOCKS;
  char*    query      = strchr(client->request.path + 9, '?');
  if (query) {
    query++;
    uint64_t fb = c4_get_query(query, "from_block");
    if (fb > 0) from_block = fb;
    uint64_t mb = c4_get_query(query, "max_blocks");
    if (mb > 0) max_blocks = mb > TX_CACHE_MAX_BLOCKS_LIMIT
                                 ? TX_CACHE_MAX_BLOCKS_LIMIT
                                 : (uint32_t) mb;
  }

  tx_cache_ssz_ctx_t ctx = {
      .from_block = from_block,
      .eligible   = 0,
  };
  c4_eth_tx_cache_visit_blocks(count_eligible_blocks, &ctx);

  uint32_t total_eligible = ctx.eligible;
  if (total_eligible == 0) {
    c4_http_respond(client, 200, "application/octet-stream", NULL_BYTES);
    return true;
  }

  ctx.list_builder = ssz_builder_for_def(&PAP_TX_CACHE_SNAPSHOT);
  ctx.block_count  = 0;
  ctx.max_blocks   = max_blocks;
  ctx.skip         = total_eligible > max_blocks ? total_eligible - max_blocks : 0;
  ctx.eligible     = 0;

  c4_eth_tx_cache_visit_blocks(build_block_ssz, &ctx);

  ssz_builder_fix_list_offsets(&ctx.list_builder, ctx.block_count);

  ssz_ob_t result = ssz_builder_to_bytes(&ctx.list_builder);
  c4_http_respond(client, 200, "application/octet-stream", result.bytes);
  safe_free(result.bytes.data);
  return true;
}

#else

bool c4_handle_tx_cache(client_t* client) {
  if (strncmp(client->request.path, "/tx_cache", 9) != 0) return false;
  char next = client->request.path[9];
  if (next && next != '?' && next != '/') return false;
  c4_write_error_response(client, 503, "Transaction cache not available (PROVER_CACHE disabled)");
  return true;
}

#endif
