/*
 * Copyright (c) 2025,2026 corpus.core GmbH
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

#include "colibri.h"
#include "colibri_common.h"
#include "beacon_types.h"
#include "plugin.h"
#include "sync_committee.h"
#include "version.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  verify_ctx_t ctx;
  bytes_t      proof;
  bool         initialised;
} c4_verify_ctx_t;

prover_t* c4_create_prover_ctx(char* method, char* params, uint64_t chain_id, uint32_t flags) {
  return (void*) c4_prover_create(method, params, chain_id, flags);
}

char* c4_prover_execute_json_status(prover_t* prover) {
  prover_ctx_t* ctx    = (prover_ctx_t*) prover;
  c4_status_t   status = c4_prover_execute(ctx);
  return c4i_build_prover_json_status(status, &ctx->state, ctx->proof.data, ctx->proof.len, true);
}

void c4_free_prover_ctx(prover_t* ctx) {
  c4_prover_free((prover_ctx_t*) ctx);
}

void c4_req_set_response(void* req_ptr, bytes_t data, uint16_t node_index) {
  data_request_t* ctx      = (data_request_t*) req_ptr;
  ctx->response            = bytes_dup(bytes(data.data, data.len));
  ctx->response_node_index = node_index;
}

void c4_req_set_error(void* req_ptr, char* error, uint16_t node_index) {
  data_request_t* ctx      = (data_request_t*) req_ptr;
  ctx->error               = strdup(error);
  ctx->response_node_index = node_index;
}

bytes_t c4_prover_get_proof(prover_t* prover) {
  prover_ctx_t* ctx = (prover_ctx_t*) prover;
  return ctx->proof;
}

void* c4_verify_create_ctx(bytes_t proof, char* method, char* args, uint64_t chain_id, char* trusted_checkpoint, uint32_t flags) {
  c4_verify_ctx_t* ctx = calloc(1, sizeof(c4_verify_ctx_t));
  if (!ctx) return NULL;
  ctx->proof = bytes_dup(proof);
  char* method_copy = method ? strdup(method) : NULL;
  // json_parse() skips leading whitespace, so the token is not the allocation
  // base of the strdup. c4_verify_free_ctx() frees ctx->args.start, which must
  // therefore be its own buffer. Copying the token also drops the original
  // allocation before c4_verify_init(), so scan-build can see it is released.
  char*  args_buf   = args ? strdup(args) : NULL;
  json_t args_json  = {0};
  char*  args_owned = NULL;
  if (args_buf) {
    json_t parsed = json_parse(args_buf);
    if (parsed.type != JSON_TYPE_INVALID && parsed.type != JSON_TYPE_NOT_FOUND && parsed.start && parsed.len) {
      args_owned = safe_malloc(parsed.len + 1);
      memcpy(args_owned, parsed.start, parsed.len);
      args_owned[parsed.len] = 0;
      args_json              = parsed;
      args_json.start        = args_owned;
    }
    free(args_buf);
  }
  if (c4_verify_init(&ctx->ctx, ctx->proof, method_copy, args_json, (chain_id_t) chain_id, (verify_flags_t) flags) != C4_SUCCESS) {
    if (ctx->proof.data) free(ctx->proof.data);
    free(method_copy);
    free(args_owned);
    c4_state_free(&ctx->ctx.state);
    free(ctx);
    return NULL;
  }
  c4_set_checkpoint((chain_id_t) chain_id, trusted_checkpoint);
  // method_copy and args_owned are now owned by ctx (ctx->method, ctx->args.start)
  // and released in c4_verify_free_ctx().
  return (void*) ctx;
}

char* c4_verify_execute_json_status(void* ptr) {
  c4_verify_ctx_t* ctx    = (c4_verify_ctx_t*) ptr;
  c4_status_t      status = c4_verify(&ctx->ctx);
  bool             reverted = (ctx->ctx.flags & VERIFY_FLAG_REVERTED) != 0;
  return c4i_build_verifier_json_status(status, &ctx->ctx.state, ctx->ctx.data, reverted, true);
}

void c4_verify_free_ctx(void* ptr) {
  c4_verify_ctx_t* ctx = (c4_verify_ctx_t*) ptr;
  if (ctx->proof.data) free(ctx->proof.data);
  if (ctx->ctx.method) free((char*) ctx->ctx.method);
  if (ctx->ctx.args.start) free((char*) ctx->ctx.args.start);
  c4_verify_free_data(&(ctx->ctx));
  free(ctx);
}

void c4_verify_set_min_latest_block_ts(void* ptr, uint64_t ts) {
  if (!ptr) return;
  c4_verify_ctx_t* ctx     = (c4_verify_ctx_t*) ptr;
  ctx->ctx.min_latest_block_ts = ts;
}

int c4_get_method_support(uint64_t chain_id, char* method, char* params, uint32_t flags) {
  return (int) c4_get_method_type((chain_id_t) chain_id, method,
                                  params ? json_parse(params) : (json_t){0}, (verify_flags_t) flags);
}

uint32_t c4_get_current_version_number(void) {
  return c4_current_version_number();
}

/* ── Unified RPC API ── */

void* c4_create_rpc_ctx(char* method, char* params, uint64_t chain_id, uint32_t prover_flags, uint32_t verify_flags, int prover_mode) {
  return (void*) c4_rpc_ctx_create(method, params, (chain_id_t) chain_id,
                                   (prover_flags_t) prover_flags, (verify_flags_t) verify_flags,
                                   (c4_prover_mode_t) prover_mode);
}

char* c4_rpc_execute_json_status(void* ctx) {
  return c4_rpc_build_json_status((c4_rpc_ctx_t*) ctx, true);
}

void c4_rpc_set_witness_keys(void* ctx, const char* witness_keys) {
  c4_rpc_ctx_set_witness_keys((c4_rpc_ctx_t*) ctx, witness_keys);
}

void c4_rpc_set_proxy_urls(void* ctx, const char* rpc_urls, const char* beacon_urls) {
  c4_rpc_ctx_set_proxy_urls((c4_rpc_ctx_t*) ctx, rpc_urls, beacon_urls);
}

void c4_rpc_set_min_latest_block_ts(void* ctx, uint64_t ts) {
  c4_rpc_ctx_set_min_latest_block_ts((c4_rpc_ctx_t*) ctx, ts);
}

void c4_free_rpc_ctx(void* ctx) {
  c4_rpc_ctx_free((c4_rpc_ctx_t*) ctx);
}
// `c4_configure_chain` is implemented in colibri_common.c so the Emscripten
// and Node.js bindings (which link against colibri_common but not against this
// translation unit) can call it directly. Its declaration lives in both
// `colibri.h` (public C-API) and `colibri_common.h` (shared binding header).
