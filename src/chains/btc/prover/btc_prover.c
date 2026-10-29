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

#include "btc_prover.h"
#include "btc_client.h"
#include "btc_hash.h"
#include "btc_header.h"
#include "btc_merkle.h"
#include "btc_target.h"
#include "btc_tx.h"
#include "btc_types.h"
#include "btc_verify.h"
#include "version.h"
#include <string.h>

#define BTC_RPC_TTL (3600u * 24u)

/**
 * Sends one Bitcoin Core JSON-RPC call and returns its `result`.
 *
 * The host must NUL-terminate `response`. A call that is not finished yet is queued
 * and reported as pending. The same parameters resolve to the same request.
 *
 * @param ctx prover context that owns the request list
 * @param method Bitcoin Core method name
 * @param params JSON array text
 * @param result receives the `result` value; valid until the next state reset
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
static c4_status_t btc_rpc(prover_ctx_t* ctx, const char* method, const char* params, json_t* result) {
  bytes32_t       id  = {0};
  buffer_t        buf = {0};
  data_request_t* req;
  json_t          response;
  json_t          error;
  json_t          value;

  bprintf(&buf, "{\"jsonrpc\":\"2.0\",\"method\":\"%s\",\"params\":%s,\"id\":1}", method, params);
  sha256(buf.data, id);
  req = c4_state_get_data_request_by_id(&ctx->state, id);
  if (req) {
    buffer_free(&buf);
    if (c4_state_is_pending(req)) return C4_PENDING;
    if (req->error || !req->response.data) THROW_ERROR(req->error ? req->error : "bitcoin rpc request failed");
    // Hosts and the server cache store the body without a trailing NUL. Keep the copy on the request so `result` stays valid.
    {
      uint8_t* text = safe_malloc(req->response.len + 1u);
      memcpy(text, req->response.data, req->response.len);
      text[req->response.len] = 0;
      safe_free(req->response.data);
      req->response.data = text;
    }
    response = json_parse((char*) req->response.data);
    if (response.type != JSON_TYPE_OBJECT) THROW_ERROR("invalid bitcoin rpc response");
    error = json_get(response, "error");
    if (error.type == JSON_TYPE_OBJECT || error.type == JSON_TYPE_STRING)
      THROW_ERROR_WITH("bitcoin rpc %s failed: %j", method, error.type == JSON_TYPE_STRING ? error : json_get(error, "message"));
    value = json_get(response, "result");
    if (value.type == JSON_TYPE_NOT_FOUND || value.type == JSON_TYPE_INVALID || value.type == JSON_TYPE_NULL)
      THROW_ERROR("bitcoin rpc result is empty");
    *result = value;
    return C4_SUCCESS;
  }

  req           = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
  req->chain_id = ctx->chain_id;
  req->payload  = buf.data;
  req->encoding = C4_DATA_ENCODING_JSON;
  req->method   = C4_DATA_METHOD_POST;
  req->type     = C4_DATA_TYPE_BTC_RPC;
  req->ttl      = BTC_RPC_TTL;
  memcpy(req->id, id, 32);
  c4_state_add_request(&ctx->state, req);
  return C4_PENDING;
}

/**
 * Decodes a JSON hex string into an exact number of bytes.
 *
 * @param ctx prover context used for the error
 * @param value JSON string
 * @param buf scratch buffer the decoded bytes occupy
 * @param expect required length
 * @param what error text when the value is not that hex string
 * @return `C4_SUCCESS` or `C4_ERROR`
 */
static c4_status_t take_hex(prover_ctx_t* ctx, json_t value, buffer_t* buf, uint32_t expect, const char* what) {
  bytes_t got;
  if (value.type != JSON_TYPE_STRING) THROW_ERROR(what);
  buffer_reset(buf);
  got = json_as_bytes(value, buf);
  if (!got.data || got.len != expect) THROW_ERROR(what);
  return C4_SUCCESS;
}

/**
 * Rejects heights the header proof cannot bind.
 *
 * @param ctx prover context used for the error
 * @param height block height
 * @return `C4_SUCCESS` or `C4_ERROR`
 */
static c4_status_t check_height(prover_ctx_t* ctx, uint32_t height) {
  if (height < BTC_BIP34_START) THROW_ERROR("block height is below BIP34");
  if ((uint64_t) height + BTC_FINALITY > UINT32_MAX) THROW_ERROR("block height is too high");
  return C4_SUCCESS;
}

/**
 * Reads the client tip. An empty blob is the compiled checkpoint.
 *
 * @param ctx prover context
 * @param client receives the tip
 * @return `C4_SUCCESS` or `C4_ERROR`
 */
static c4_status_t load_client(prover_ctx_t* ctx, btc_client_state_t* client) {
  if (!ctx->client_state.data || ctx->client_state.len == 0) {
    btc_client_state_set_checkpoint(client);
    return C4_SUCCESS;
  }
  if (!btc_client_state_decode(ctx->client_state, client)) THROW_ERROR("invalid bitcoin client state");
  return C4_SUCCESS;
}

/**
 * Lists epoch-boundary heights from the client toward `target`.
 *
 * A client at the target epoch, or ahead of it, needs no path. Each earlier step is the last
 * block of an epoch at most `BTC_TARGET_MAX_DAPS` away. The last step is `target` itself.
 *
 * @param from_height client height
 * @param target block being proved
 * @param steps receives the heights
 * @param count receives the number of steps
 * @return false when the path does not fit in `BTC_TARGET_PATH_LIMIT`
 */
static bool plan_epoch_steps(uint32_t from_height, uint32_t target, uint32_t* steps, uint32_t* count) {
  uint32_t from = btc_dap(from_height);
  uint32_t to   = btc_dap(target);
  *count        = 0;
  if (from >= to) return true;
  while (from < to) {
    uint32_t next = from + BTC_TARGET_MAX_DAPS;
    uint32_t height;
    if (next > to) next = to;
    if (next == to)
      height = target;
    else {
      uint64_t end = ((uint64_t) next + 1ull) * BTC_EPOCH_LENGTH - 1ull;
      if (end > UINT32_MAX) return false;
      height = (uint32_t) end;
    }
    if (*count >= BTC_TARGET_PATH_LIMIT) return false;
    steps[(*count)++] = height;
    from              = next;
  }
  return true;
}

/**
 * Fetches one header by its display-order hash.
 *
 * @param ctx prover context
 * @param display 32-byte display-order hash
 * @param out receives the 80-byte header
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
static c4_status_t fetch_header_at(prover_ctx_t* ctx, const uint8_t display[32], uint8_t out[80]) {
  uint8_t  params_raw[96] = {0};
  uint8_t  hex_raw[96];
  buffer_t params = stack_buffer(params_raw);
  buffer_t hex    = stack_buffer(hex_raw);
  json_t   result;
  uint8_t  internal[32];
  uint8_t  actual[32];

  bprintf(&params, "[\"%x\",false]", bytes((uint8_t*) display, 32));
  TRY_ASYNC(btc_rpc(ctx, "getblockheader", (char*) params.data.data, &result));
  TRY_ASYNC(take_hex(ctx, result, &hex, BTC_HEADER_SIZE, "bitcoin header is invalid"));
  memcpy(out, hex.data.data, BTC_HEADER_SIZE);
  btc_hash256(bytes(out, BTC_HEADER_SIZE), internal);
  btc_hash_reverse(internal, actual);
  if (memcmp(actual, display, 32) != 0) THROW_ERROR("bitcoin header does not match the requested hash");
  return C4_SUCCESS;
}

/**
 * Fetches one header by height.
 *
 * @param ctx prover context
 * @param height block height
 * @param out receives the 80-byte header
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
static c4_status_t fetch_header(prover_ctx_t* ctx, uint32_t height, uint8_t out[80]) {
  uint8_t  params_raw[32] = {0};
  uint8_t  hash_raw[40];
  buffer_t params = stack_buffer(params_raw);
  buffer_t hash   = stack_buffer(hash_raw);
  json_t   result;

  bprintf(&params, "[%d]", height);
  TRY_ASYNC(btc_rpc(ctx, "getblockhash", (char*) params.data.data, &result));
  TRY_ASYNC(take_hex(ctx, result, &hash, 32, "bitcoin block hash is invalid"));
  return fetch_header_at(ctx, hash.data.data, out);
}

/**
 * Reads height, transaction count, and the coinbase display id from `getblock` verbosity 1.
 *
 * @param ctx prover context
 * @param display 32-byte display-order block hash
 * @param height receives the block height
 * @param tx_count receives `nTx`
 * @param txid receives the first transaction id in display order
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
static c4_status_t fetch_block(prover_ctx_t* ctx, const uint8_t display[32], uint32_t* height, uint32_t* tx_count, uint8_t txid[32]) {
  uint8_t  params_raw[96] = {0};
  uint8_t  id_raw[40];
  buffer_t params = stack_buffer(params_raw);
  buffer_t idbuf  = stack_buffer(id_raw);
  json_t   block;
  json_t   tx;
  json_t   hash;
  uint64_t h;
  uint64_t n;
  bytes_t  got;

  bprintf(&params, "[\"%x\",1]", bytes((uint8_t*) display, 32));
  TRY_ASYNC(btc_rpc(ctx, "getblock", (char*) params.data.data, &block));
  if (block.type != JSON_TYPE_OBJECT) THROW_ERROR("bitcoin block is not an object");
  hash = json_get(block, "hash");
  if (hash.type != JSON_TYPE_STRING) THROW_ERROR("bitcoin block hash does not match");
  {
    uint8_t  tmp[40];
    buffer_t got_hash = stack_buffer(tmp);
    bytes_t  hash_bytes;
    buffer_reset(&got_hash);
    hash_bytes = json_as_bytes(hash, &got_hash);
    if (hash_bytes.len != 32 || memcmp(hash_bytes.data, display, 32) != 0) THROW_ERROR("bitcoin block hash does not match");
  }
  if (json_get(block, "height").type != JSON_TYPE_NUMBER || json_get(block, "nTx").type != JSON_TYPE_NUMBER)
    THROW_ERROR("bitcoin block is invalid");
  h = json_as_uint64(json_get(block, "height"));
  n = json_as_uint64(json_get(block, "nTx"));
  if (h > UINT32_MAX || n == 0 || n > UINT32_MAX) THROW_ERROR("bitcoin block is invalid");
  tx = json_get(block, "tx");
  if (tx.type != JSON_TYPE_ARRAY || json_len(tx) == 0) THROW_ERROR("bitcoin block transactions are missing");
  buffer_reset(&idbuf);
  got = json_as_bytes(json_at(tx, 0), &idbuf);
  if (got.len != 32) THROW_ERROR("bitcoin coinbase id is invalid");
  *height   = (uint32_t) h;
  *tx_count = (uint32_t) n;
  memcpy(txid, got.data, 32);
  return C4_SUCCESS;
}

/**
 * Fetches the raw coinbase and its merkleblock.
 *
 * @param ctx prover context
 * @param txid display-order coinbase id
 * @param block display-order block hash
 * @param coin receives the raw transaction; the caller frees it
 * @param merkle receives the merkleblock; the caller frees it
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
static c4_status_t fetch_coinbase(prover_ctx_t* ctx, const uint8_t txid[32], const uint8_t block[32], buffer_t* coin, buffer_t* merkle) {
  uint8_t  tx_raw[192] = {0};
  uint8_t  mb_raw[192] = {0};
  buffer_t tx_params   = stack_buffer(tx_raw);
  buffer_t mb_params   = stack_buffer(mb_raw);
  json_t   tx_hex      = {0};
  json_t   mb_hex      = {0};
  bytes_t  got;

  c4_status_t status = C4_SUCCESS;

  bprintf(&tx_params, "[\"%x\",false,\"%x\"]", bytes((uint8_t*) txid, 32), bytes((uint8_t*) block, 32));
  bprintf(&mb_params, "[[\"%x\"],\"%x\"]", bytes((uint8_t*) txid, 32), bytes((uint8_t*) block, 32));
  TRY_ADD_ASYNC(status, btc_rpc(ctx, "getrawtransaction", (char*) tx_params.data.data, &tx_hex));
  TRY_ADD_ASYNC(status, btc_rpc(ctx, "gettxoutproof", (char*) mb_params.data.data, &mb_hex));
  if (status != C4_SUCCESS) return status;
  if (tx_hex.type != JSON_TYPE_STRING || mb_hex.type != JSON_TYPE_STRING) THROW_ERROR("bitcoin coinbase proof is invalid");
  buffer_reset(coin);
  buffer_reset(merkle);
  got = json_as_bytes(tx_hex, coin);
  if (!got.data || got.len == 0 || got.len > 1000000u) {
    buffer_free(coin);
    THROW_ERROR("bitcoin coinbase is invalid");
  }
  got = json_as_bytes(mb_hex, merkle);
  if (!got.data || got.len == 0 || got.len > 8192u) {
    buffer_free(coin);
    buffer_free(merkle);
    THROW_ERROR("bitcoin merkle proof is invalid");
  }
  return C4_SUCCESS;
}

/**
 * Writes display order of a header hash.
 *
 * @param header 80-byte header
 * @param display receives 32 bytes
 */
static void header_display(const uint8_t header[80], uint8_t display[32]) {
  uint8_t internal[32];
  btc_hash256(bytes((uint8_t*) header, BTC_HEADER_SIZE), internal);
  btc_hash_reverse(internal, display);
}

/**
 * Serializes the header proof and the optional epoch path into `ctx->proof`.
 *
 * @param ctx prover context that receives the proof bytes
 * @param by_hash true when the result is the display-order block hash
 * @param height proven height
 * @param headers target header followed by `BTC_FINALITY` headers
 * @param tx_count transaction count
 * @param siblings internal-order coinbase siblings
 * @param sibling_count number of siblings
 * @param coinbase raw coinbase transaction
 * @param step_heights epoch-step heights
 * @param step_headers one 80-byte header per step
 * @param step_count number of epoch steps; zero omits `sync_data`
 */
static void write_proof(prover_ctx_t* ctx, bool by_hash, uint32_t height, uint8_t headers[][BTC_HEADER_SIZE], uint32_t tx_count, uint8_t* siblings, uint32_t sibling_count, bytes_t coinbase, const uint32_t* step_heights, uint8_t step_headers[][BTC_HEADER_SIZE], uint32_t step_count) {
  const ssz_def_t* proof_def = btc_ssz_verification_type(BTC_SSZ_VERIFY_HEADER_PROOF);
  const ssz_def_t* data_def  = btc_ssz_verification_type(by_hash ? BTC_SSZ_DATA_BLOCK_HASH : BTC_SSZ_DATA_HEADER);
  ssz_builder_t    proof     = ssz_builder_for_def(proof_def);
  ssz_builder_t    finals    = ssz_builder_for_def(&proof_def->def.container.elements[2]);
  ssz_builder_t    merkle    = ssz_builder_for_def(&proof_def->def.container.elements[4]);
  ssz_builder_t    coin      = ssz_builder_for_def(&proof_def->def.container.elements[5]);
  ssz_builder_t    data      = ssz_builder_for_def(data_def);
  ssz_builder_t    request   = ssz_builder_for_def(btc_ssz_verification_type(BTC_SSZ_VERIFY_REQUEST));
  ssz_builder_t    sync      = {0};
  uint32_t         i;
  uint8_t          display[32];

  ssz_add_uint32(&proof, height);
  ssz_add_bytes(&proof, "header", bytes(headers[0], BTC_HEADER_SIZE));
  for (i = 0; i < BTC_FINALITY; i++)
    ssz_add_dynamic_list_bytes(&finals, 0, bytes(headers[i + 1], BTC_HEADER_SIZE));
  ssz_add_builders(&proof, "final_headers", finals);
  ssz_add_uint32(&proof, tx_count);
  for (i = 0; i < sibling_count; i++)
    ssz_add_dynamic_list_bytes(&merkle, 0, bytes(siblings + (size_t) i * 32u, 32));
  ssz_add_builders(&proof, "merkle", merkle);
  buffer_append(&coin.fixed, coinbase);
  ssz_add_builders(&proof, "coinbase", coin);

  header_display(headers[0], display);
  if (by_hash)
    buffer_append(&data.fixed, bytes(display, 32));
  else
    buffer_append(&data.fixed, bytes(headers[0], BTC_HEADER_SIZE));

  if (step_count > 0) {
    const ssz_def_t* path_def = btc_ssz_verification_type(BTC_SSZ_VERIFY_EPOCH_PATH);
    const ssz_def_t* step_def = path_def->def.container.elements[0].def.vector.type;
    ssz_builder_t    steps    = ssz_builder_for_def(&path_def->def.container.elements[0]);
    sync                      = ssz_builder_for_def(path_def);
    for (i = 0; i < step_count; i++) {
      ssz_builder_t step  = ssz_builder_for_def(step_def);
      ssz_builder_t trail = ssz_builder_for_def(&step_def->def.container.elements[2]);
      ssz_add_uint32(&step, step_heights[i]);
      ssz_add_bytes(&step, "header", bytes(step_headers[i], BTC_HEADER_SIZE));
      ssz_add_builders(&step, "final_headers", trail);
      ssz_add_dynamic_list_builders(&steps, (int) i, step);
    }
    ssz_builder_fix_list_offsets(&steps, step_count);
    ssz_add_builders(&sync, "steps", steps);
  }

  ssz_add_bytes(&request, "version", bytes((uint8_t*) c4_protocol_version_bytes, 4));
  ssz_add_builders(&request, "data", data);
  ssz_add_builders(&request, "proof", proof);
  if (step_count > 0)
    ssz_add_builders(&request, "sync_data", sync);
  else
    ssz_add_bytes(&request, "sync_data", bytes(NULL, 1));
  request.fixed.data.data[0] = (uint8_t) c4_chain_type(ctx->chain_id);
  ctx->proof                 = ssz_builder_to_bytes(&request).bytes;
}

static c4_status_t requested_hash(prover_ctx_t* ctx, uint8_t out[32]);

/**
 * Builds a header proof for `getblockhash` or raw `getblockheader`.
 *
 * @param ctx prover context
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
static c4_status_t prove_header(prover_ctx_t* ctx) {
  bool               by_hash = strcmp(ctx->method, "getblockhash") == 0;
  btc_client_state_t client;
  uint32_t           height   = 0;
  uint32_t           tx_count = 0;
  uint32_t           step_heights[BTC_TARGET_PATH_LIMIT];
  uint32_t           step_count = 0;
  uint32_t           i;
  uint8_t            requested[32] = {0};
  uint8_t            display[32];
  uint8_t            txid[32];
  uint8_t            headers[1u + BTC_FINALITY][BTC_HEADER_SIZE];
  uint8_t            step_headers[BTC_TARGET_PATH_LIMIT][BTC_HEADER_SIZE];
  uint8_t            siblings[32 * 32];
  uint8_t            mb_header[BTC_HEADER_SIZE];
  uint8_t            coin_id[32];
  uint8_t            txid_internal[32];
  uint32_t           mb_count      = 0;
  uint32_t           sibling_count = 0;
  buffer_t           coin          = {0};
  buffer_t           merkle        = {0};
  btc_tx_t           tx;
  c4_status_t        status;

  TRY_ASYNC(load_client(ctx, &client));
  if (by_hash) {
    height = (uint32_t) json_as_uint64(json_at(ctx->params, 0));
    TRY_ASYNC(check_height(ctx, height));
    if (!plan_epoch_steps(client.height, height, step_heights, &step_count)) THROW_ERROR("bitcoin epoch path is too long");
  }
  else {
    TRY_ASYNC(requested_hash(ctx, requested));
    TRY_ASYNC(fetch_block(ctx, requested, &height, &tx_count, txid));
    TRY_ASYNC(check_height(ctx, height));
    if (!plan_epoch_steps(client.height, height, step_heights, &step_count)) THROW_ERROR("bitcoin epoch path is too long");
  }

  status = C4_SUCCESS;
  for (i = 0; i < 1u + BTC_FINALITY; i++)
    TRY_ADD_ASYNC(status, fetch_header(ctx, height + i, headers[i]));
  for (i = 0; i < step_count; i++)
    TRY_ADD_ASYNC(status, fetch_header(ctx, step_heights[i], step_headers[i]));
  if (status != C4_SUCCESS) return status;

  header_display(headers[0], display);
  if (!by_hash && memcmp(display, requested, 32) != 0) THROW_ERROR("block hash does not match");
  if (by_hash) TRY_ASYNC(fetch_block(ctx, display, &height, &tx_count, txid));
  if (by_hash && height != (uint32_t) json_as_uint64(json_at(ctx->params, 0))) THROW_ERROR("bitcoin block height does not match");
  for (i = 0; i < BTC_FINALITY; i++) {
    uint8_t prev[32];
    btc_hash256(bytes(headers[i], BTC_HEADER_SIZE), prev);
    if (memcmp(headers[i + 1] + BTC_HEADER_PARENT, prev, 32) != 0) THROW_ERROR("following header does not link");
  }

  TRY_ASYNC(fetch_coinbase(ctx, txid, display, &coin, &merkle));
  if (!btc_merkleblock_coinbase(merkle.data, mb_header, &mb_count, siblings, &sibling_count) || mb_count != tx_count || memcmp(mb_header, headers[0], BTC_HEADER_SIZE) != 0) {
    buffer_free(&coin);
    buffer_free(&merkle);
    THROW_ERROR("bitcoin merkle proof is invalid");
  }
  if (!btc_tx_parse(coin.data, &tx) || !btc_tx_id(&tx, coin_id)) {
    buffer_free(&coin);
    buffer_free(&merkle);
    THROW_ERROR("bitcoin coinbase is invalid");
  }
  btc_hash_reverse(txid, txid_internal);
  if (memcmp(coin_id, txid_internal, 32) != 0 || !btc_merkle_verify(coin_id, 0, tx_count, bytes(siblings, sibling_count * 32u), headers[0] + BTC_HEADER_MERKLE)) {
    buffer_free(&coin);
    buffer_free(&merkle);
    THROW_ERROR("bitcoin coinbase merkle proof does not match");
  }

  write_proof(ctx, by_hash, by_hash ? (uint32_t) json_as_uint64(json_at(ctx->params, 0)) : height, headers, tx_count, siblings, sibling_count, coin.data, step_heights, step_headers, step_count);
  buffer_free(&coin);
  buffer_free(&merkle);
  return C4_SUCCESS;
}

/**
 * Reads the display-order hash argument of `getblockheader`.
 *
 * @param ctx prover context
 * @param out receives 32 bytes
 * @return `C4_SUCCESS` or `C4_ERROR`
 */
static c4_status_t requested_hash(prover_ctx_t* ctx, uint8_t out[32]) {
  uint8_t  tmp[40];
  buffer_t buf = stack_buffer(tmp);
  bytes_t  got = json_as_bytes(json_at(ctx->params, 0), &buf);
  if (got.len != 32) THROW_ERROR("block hash mismatch");
  memcpy(out, got.data, 32);
  return C4_SUCCESS;
}

bool btc_prover_execute(prover_ctx_t* ctx) {
  if (!ctx || c4_chain_type(ctx->chain_id) != C4_CHAIN_TYPE_BITCOIN) return false;
  if (ctx->chain_id != C4_CHAIN_BTC_MAINNET) {
    c4_state_add_error(&ctx->state, "bitcoin network is not supported");
    return true;
  }
  if (c4_btc_get_method_type(ctx->chain_id, ctx->method, ctx->params, 0) != METHOD_PROOFABLE) {
    c4_state_add_error(&ctx->state, "bitcoin proof generation is not implemented");
    return true;
  }
  prove_header(ctx);
  return true;
}
