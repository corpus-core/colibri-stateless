/*
 * Copyright (c) 2025,2026 corpus.core
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

#include "../colibri_common.h"
#include "plugin.h"
#include "sync_committee.h"
#include "version.h"
#include <emscripten.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct {
  bytes_t      proof;
  verify_ctx_t verify;
} c4w_verify_ctx_t;

/* ── Prover API ── */

prover_ctx_t* EMSCRIPTEN_KEEPALIVE c4w_create_proof_ctx(char* method, char* args, uint64_t chain_id, uint32_t flags) {
  return c4_prover_create(method, args, chain_id, flags);
}

void EMSCRIPTEN_KEEPALIVE c4w_free_proof_ctx(prover_ctx_t* ctx) {
  c4_prover_free(ctx);
}

char* EMSCRIPTEN_KEEPALIVE c4w_execute_proof_ctx(prover_ctx_t* ctx) {
  c4_status_t status = c4_prover_execute(ctx);
  return c4i_build_prover_json_status(status, &ctx->state, ctx->proof.data, ctx->proof.len, false);
}

/* ── Request handling ── */

void EMSCRIPTEN_KEEPALIVE c4w_req_set_response(data_request_t* ctx, void* data, size_t len, uint16_t node_index) {
  ctx->response            = bytes(data, len);
  ctx->response_node_index = node_index;
}

void EMSCRIPTEN_KEEPALIVE c4w_req_set_error(data_request_t* ctx, char* error, uint16_t node_index) {
  ctx->error               = strdup(error);
  ctx->response_node_index = node_index;
}

/* ── Verify API ── */

void* EMSCRIPTEN_KEEPALIVE c4w_create_verify_ctx(uint8_t* proof, size_t proof_len, char* method, char* args, uint64_t chain_id, char* trusted_checkpoint, char* witness_keys, uint32_t flags) {
  c4_set_checkpoint((chain_id_t) chain_id, trusted_checkpoint);
  if (method == NULL || strlen(method) == 0) return NULL;

  c4w_verify_ctx_t* ctx = calloc(1, sizeof(c4w_verify_ctx_t));
  ctx->proof            = bytes_dup(bytes(proof, proof_len));
  c4_verify_init(&ctx->verify, ctx->proof, strdup(method), args ? json_parse(strdup(args)) : ((json_t){.len = 0, .start = "[]", .type = JSON_TYPE_ARRAY}), (chain_id_t) chain_id, (verify_flags_t) flags);

  if (witness_keys && strlen(witness_keys) > 40 && witness_keys[0] == '0' && witness_keys[1] == 'x') {
    bytes_t witness_key_bytes = bytes(safe_malloc(strlen(witness_keys) / 2), (strlen(witness_keys) - 2) / 2);
    hex_to_bytes(witness_keys + 2, -1, witness_key_bytes);
    ctx->verify.witness_keys = witness_key_bytes;
  }

  return (void*) ctx;
}

void EMSCRIPTEN_KEEPALIVE c4w_free_verify_ctx(void* ptr) {
  c4w_verify_ctx_t* ctx = (c4w_verify_ctx_t*) ptr;
  if (ctx->verify.method) free((char*) ctx->verify.method);
  if (ctx->verify.args.len) free((char*) ctx->verify.args.start);
  if (ctx->proof.data) free(ctx->proof.data);
  c4_verify_free_data(&ctx->verify);
  free(ctx);
}

char* EMSCRIPTEN_KEEPALIVE c4w_verify_proof(void* ptr) {
  verify_ctx_t* ctx    = &((c4w_verify_ctx_t*) ptr)->verify;
  c4_status_t   status = c4_verify(ctx);
  return c4i_build_verifier_json_status(status, &ctx->state, ctx->data, false);
}

/* ── Method type ── */

method_type_t EMSCRIPTEN_KEEPALIVE c4w_get_method_type(uint64_t chain_id, char* method, char* params, uint32_t flags) {
  return c4_get_method_type((chain_id_t) chain_id, method,
                            params ? json_parse(params) : (json_t){0}, (verify_flags_t) flags);
}

/* ── Unified RPC API ── */

void* EMSCRIPTEN_KEEPALIVE c4w_create_rpc_ctx(char* method, char* params, uint64_t chain_id, uint32_t prover_flags, uint32_t verify_flags, int prover_mode) {
  return (void*) c4_rpc_ctx_create(method, params, (chain_id_t) chain_id,
                                   (prover_flags_t) prover_flags, (verify_flags_t) verify_flags,
                                   (c4_prover_mode_t) prover_mode);
}

char* EMSCRIPTEN_KEEPALIVE c4w_execute_rpc_ctx(void* ctx) {
  return c4_rpc_build_json_status((c4_rpc_ctx_t*) ctx, false);
}

void EMSCRIPTEN_KEEPALIVE c4w_free_rpc_ctx(void* ctx) {
  c4_rpc_ctx_free((c4_rpc_ctx_t*) ctx);
}

void EMSCRIPTEN_KEEPALIVE c4w_set_checkpoint(uint64_t chain_id, char* checkpoint) {
  c4_set_checkpoint((chain_id_t) chain_id, checkpoint);
}

void EMSCRIPTEN_KEEPALIVE c4w_rpc_ctx_set_witness_keys(void* ctx, char* keys) {
  c4_rpc_ctx_set_witness_keys((c4_rpc_ctx_t*) ctx, keys);
}

/* ── Utilities ── */

char* EMSCRIPTEN_KEEPALIVE c4w_decode_proof(uint8_t* data, size_t len) {
  bytes_t          req_data = bytes(data, len);
  const ssz_def_t* def      = c4_get_req_type_from_req(req_data);
  if (!def) return NULL;
  return bprintf(NULL, "%Z", (ssz_ob_t){.def = def, .bytes = req_data});
}

void EMSCRIPTEN_KEEPALIVE c4w_req_free(data_request_t* client_update) {
  if (client_update->error) free(client_update->error);
  if (client_update->response.data) free(client_update->response.data);
  free(client_update);
}

uint8_t* EMSCRIPTEN_KEEPALIVE c4w_buffer_alloc(buffer_t* buf, size_t len) {
  buffer_grow(buf, len + 1);
  buf->data.len = len;
  return buf->data.data;
}

/* ── Storage plugin (WASM ↔ JS bridge) ── */

static bool file_get(char* key, buffer_t* buffer) {
  return EM_ASM_INT({
    var keyStr = UTF8ToString($0);
    var data = Module.storage.get(keyStr);
    if (data) {
      var bufferPtr =  _c4w_buffer_alloc($1,data.length);
      HEAPU8.set(data, bufferPtr);
      return 1;
    }
    return 0; }, key, buffer);
}

static void file_set(char* key, bytes_t data) {
  EM_ASM({
    var keyStr = UTF8ToString($0);
    var array = new Uint8Array($2);
    array.set(HEAPU8.subarray($1, $1 + $2));
    Module.storage.set(keyStr,array ); }, key, data.data, data.len);
}

static void file_delete(char* key) {
  EM_ASM({
    var keyStr = UTF8ToString($0);
    Module.storage.del(keyStr); }, key);
}

void EMSCRIPTEN_KEEPALIVE init_storage(void* ptr) {
  storage_plugin_t plgn = {
      .del             = file_delete,
      .get             = file_get,
      .set             = file_set,
      .max_sync_states = 3};
  c4_set_storage_config(&plgn);
}

uint32_t EMSCRIPTEN_KEEPALIVE c4w_get_current_version_number(void) {
  return c4_current_version_number();
}
