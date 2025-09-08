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

#include "../ssz/beacon_types.h"
#include "beacon.h"
#include "eth_req.h"
#include "eth_tools.h"
#include "historic_proof.h"
#include "json.h"
#include "proofer.h"
#include "ssz.h"
#include "version.h"
#include "witness.h"
#include <inttypes.h> // Include this header for PRIu64 and PRIx64
#include <stdlib.h>
#include <string.h>

/*
{
  "methode":"c4_witness",
  "params": [
    "blockhash", // type
    "0x1234567890" // blockNumber
  ]
}
const ssz_def_t C4_BLOCK_HASH_WITNESS[3] = {
    SSZ_UINT64("chainId"),          // the chainId
    SSZ_UINT64("blockNumber"),      // blocknumber
    SSZ_BYTES32("blockHash"),       // the blockhash
    SSZ_BYTES32("stateRoot"),       // the state root
    SSZ_BYTES32("receiptsRoot"),    // the receipts root
    SSZ_BYTES32("transactionsRoot") // the transactions root
}; // the blockhash s
*/

static c4_status_t c4_proof_witness_blockhash(proofer_ctx_t* ctx, json_t block_number, ssz_builder_t* witness_builder) {
  if (block_number.type != JSON_TYPE_STRING) THROW_ERROR_WITH("Invalid or missing block number : %j", block_number);
  uint8_t  tmp[200] = {0};
  buffer_t buffer   = stack_buffer(tmp);
  json_t   result   = {0};

  TRY_ASYNC(c4_send_eth_rpc(ctx, block_number.len == 68 ? "eth_getBlockByHash" : "eth_getBlockByNumber", bprintf(&buffer, "[%J,false]", block_number), block_number.len == 68 ? DEFAULT_TTL : 12, &result));

  ssz_builder_t witness = ssz_builder_for_def(c4_witness_get_def(C4_BLOCK_HASH_WITNESS_ID));
  ssz_add_uint64(&witness, ctx->chain_id);
  ssz_add_uint64(&witness, json_as_uint64(json_get(result, "number")));
  ssz_add_bytes(&witness, "blockHash", json_get_bytes(result, "hash", &buffer));
  ssz_add_bytes(&witness, "stateRoot", json_get_bytes(result, "stateRoot", &buffer));
  ssz_add_bytes(&witness, "receiptsRoot", json_get_bytes(result, "receiptsRoot", &buffer));
  ssz_add_bytes(&witness, "transactionsRoot", json_get_bytes(result, "transactionsRoot", &buffer));
  *witness_builder = witness;

  return C4_SUCCESS;
}

static void create_cache_key(json_t params, bytes32_t cache_key) {
  uint8_t  tmp[200] = {0};
  buffer_t buffer   = stack_buffer(tmp);
  int      p        = 0;
  memset(cache_key, 0, 32); // Initialize cache_key to zero

  json_for_each_value(params, param) {
    if (param.type == JSON_TYPE_STRING) {
      bytes_t b = {0};
      if (param.start[1] == '0' && param.start[2] == 'x')
        b = json_as_bytes(param, &buffer);
      else
        b = bytes(param.start + 1, param.len - 2);
      if (p + b.len > 32) b.len = 32 - p;
      memcpy(cache_key + p, b.data, b.len);
      p += b.len;
    }
    if (p > 32) break;
  }
}

c4_status_t c4_proof_witness(proofer_ctx_t* ctx) {
  json_t        wit_type = json_at(ctx->params, 0);
  ssz_builder_t witness  = {0};
  if (ctx->witness_key.len != 32) THROW_ERROR("Witness key is not set");
#ifdef PROOFER_CACHE
  bytes32_t cache_key = {0};
  create_cache_key(ctx->params, &cache_key);

  ssz_ob_t* witness_cache = (ssz_ob_t*) c4_proofer_cache_get(ctx, cache_key);
  if (witness_cache) {
    ctx->proof = eth_create_proof_request(
        ctx->chain_id,
        NULL_SSZ_BUILDER,
        (ssz_builder_t) {
            .def     = witness_cache->def,
            .fixed   = {.data = bytes_dup(witness_cache->bytes), .allocated = witness_cache->bytes.len},
            .dynamic = {0}},
        NULL_SSZ_BUILDER);
    return C4_SUCCESS;
  }
#endif
#ifdef WITNESS_SIGNER
  if (wit_type.type == JSON_TYPE_STRING && wit_type.len == 11 && strncmp(wit_type.start, "\"blockhash\"", wit_type.len) == 0)
    TRY_ASYNC(c4_proof_witness_blockhash(ctx, json_at(ctx->params, 1), &witness));
  else
    THROW_ERROR_WITH("Invalid witness type : %j", wit_type);

  if (witness.def == NULL) THROW_ERROR_WITH("Invalid witness builder");

  ssz_builder_t witness_signed = c4_witness_sign(witness, ctx->witness_key.data);
#ifdef PROOFER_CACHE
  ssz_ob_t* cache_entry = (ssz_ob_t*) safe_malloc(sizeof(ssz_ob_t) + witness_signed.fixed.data.len + witness_signed.dynamic.data.len);
  cache_entry->bytes    = bytes(((void*) cache_entry) + sizeof(ssz_ob_t), witness_signed.fixed.data.len + witness_signed.dynamic.data.len);
  cache_entry->def      = witness_signed.def;
  if (witness_signed.fixed.data.len) memcpy(cache_entry->bytes.data, witness_signed.fixed.data.data, witness_signed.fixed.data.len);
  if (witness_signed.dynamic.data.len) memcpy(cache_entry->bytes.data + witness_signed.fixed.data.len, witness_signed.dynamic.data.data, witness_signed.dynamic.data.len);
  c4_proofer_cache_set(ctx, cache_key, cache_entry, sizeof(ssz_ob_t) + cache_entry->bytes.len, DEFAULT_TTL, safe_free);
#endif
  ctx->proof = eth_create_proof_request(
      ctx->chain_id,
      NULL_SSZ_BUILDER,
      witness_signed,
      NULL_SSZ_BUILDER);
#else
  THROW_ERROR_WITH("Witness signing is not enabled");
#endif
  return C4_SUCCESS;
}
