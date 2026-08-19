/*
 * Copyright (c) 2025,2026 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "handler.h"
#include "pap_tx_cache_types.h"
#include "server.h"
#include "tx_cache.h"
#include "util/chain_props.h"
#include <stdio.h>
#include <string.h>

#ifdef PROVER_CACHE

#define TX_CACHE_DEFAULT_MAX_BLOCKS 256
#define TX_CACHE_MAX_BLOCKS_LIMIT   10000

uint64_t c4_get_query(char* query, char* param);

// The tx cache index is regenerated whenever a new block is observed on-chain (~every block_time
// seconds), so a shared cache/CDN may serve the SSZ snapshot for half a block time without going
// stale enough to hurt tx-hash lookups. The client fetches an incremental delta anyway, so a short
// bound is preferable over a longer one.
static void respond_txcache_ok(client_t* client, bytes_t body) {
  chain_properties_t props         = {0};
  uint64_t           block_time_ms = c4_chains_get_props((chain_id_t) http_server.chain_id, &props) ? props.block_time : 12000;
  uint32_t           max_age_s     = (uint32_t) (block_time_ms / 2 / 1000);
  if (max_age_s == 0) max_age_s = 1;
  char hdr[96];
  int  hdr_len = snprintf(hdr, sizeof hdr, "Cache-Control: public, max-age=%u\r\n", max_age_s);
  if (hdr_len < 0 || hdr_len >= (int) sizeof hdr) {
    c4_http_respond(client, 200, "application/octet-stream", body);
    return;
  }
  c4_http_respond_ex(client, 200, "application/octet-stream", body, bytes((uint8_t*) hdr, (uint32_t) hdr_len));
}

static void respond_txcache_error(client_t* client, int status, const char* msg) {
  buffer_t body = {0};
  bprintf(&body, "{\"error\":\"%S\"}", msg);
  const char* hdr = "Cache-Control: no-store\r\n";
  c4_http_respond_ex(client, status, "application/json", body.data, bytes((uint8_t*) hdr, (uint32_t) strlen(hdr)));
  buffer_free(&body);
}

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
    respond_txcache_error(client, 405, "Method Not Allowed");
    return true;
  }

  if (c4_eth_tx_cache_size() == 0) {
    respond_txcache_ok(client, NULL_BYTES);
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
    respond_txcache_ok(client, NULL_BYTES);
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
  respond_txcache_ok(client, result.bytes);
  safe_free(result.bytes.data);
  return true;
}

#else

bool c4_handle_tx_cache(client_t* client) {
  if (strncmp(client->request.path, "/tx_cache", 9) != 0) return false;
  char next = client->request.path[9];
  if (next && next != '?' && next != '/') return false;
  buffer_t body = {0};
  bprintf(&body, "{\"error\":\"%s\"}", "Transaction cache not available (PROVER_CACHE disabled)");
  const char* hdr = "Cache-Control: no-store\r\n";
  c4_http_respond_ex(client, 503, "application/json", body.data, bytes((uint8_t*) hdr, (uint32_t) strlen(hdr)));
  buffer_free(&body);
  return true;
}

#endif
