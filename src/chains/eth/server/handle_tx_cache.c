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

typedef struct {
  ssz_builder_t list_builder;
  uint32_t      block_count;
} tx_cache_ssz_ctx_t;

static void build_block_ssz(uint64_t block_number, const bytes32_t* tx_hashes,
                            uint32_t count, void* user_data) {
  tx_cache_ssz_ctx_t* ctx = (tx_cache_ssz_ctx_t*) user_data;

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

  tx_cache_ssz_ctx_t ctx = {
      .list_builder = ssz_builder_for_def(&PAP_TX_CACHE_SNAPSHOT),
      .block_count  = 0,
  };

  c4_eth_tx_cache_visit_blocks(build_block_ssz, &ctx);

  // fix offsets in the list builder
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
