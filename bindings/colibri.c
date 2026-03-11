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
  char* args_copy   = args ? strdup(args) : NULL;
  json_t args_json  = args_copy ? json_parse(args_copy) : (json_t){0};
  if (c4_verify_init(&ctx->ctx, ctx->proof, method_copy, args_json, (chain_id_t) chain_id, (verify_flags_t) flags) != C4_SUCCESS) {
    if (ctx->proof.data) free(ctx->proof.data);
    free(method_copy);
    free(args_copy);
    free(ctx);
    return NULL;
  }
  c4_set_checkpoint((chain_id_t) chain_id, trusted_checkpoint);
  /* method_copy and args_copy are transferred to ctx (ctx->ctx.method, ctx->ctx.args.start) and
   * freed in c4_verify_free_ctx() below. scan-build reports a false-positive leak here because
   * it does not track inter-procedural ownership; suppressed in scripts/scan-build-suppressions.txt.
   * Valgrind confirms no leak. */
  return (void*) ctx;
}

char* c4_verify_execute_json_status(void* ptr) {
  c4_verify_ctx_t* ctx    = (c4_verify_ctx_t*) ptr;
  c4_status_t      status = c4_verify(&ctx->ctx);
  return c4i_build_verifier_json_status(status, &ctx->ctx.state, ctx->ctx.data, true);
}

void c4_verify_free_ctx(void* ptr) {
  c4_verify_ctx_t* ctx = (c4_verify_ctx_t*) ptr;
  if (ctx->proof.data) free(ctx->proof.data);
  if (ctx->ctx.method) free((char*) ctx->ctx.method);
  if (ctx->ctx.args.start) free((char*) ctx->ctx.args.start);
  c4_verify_free_data(&(ctx->ctx));
  free(ctx);
}

int c4_get_method_support(uint64_t chain_id, char* method, char* params, uint32_t flags) {
  return (int) c4_get_method_type((chain_id_t) chain_id, method,
                                  params ? json_parse(params) : (json_t){0}, (verify_flags_t) flags);
}

uint32_t c4_get_current_version_number(void) {
  return c4_current_version_number();
}

/* ── Unified RPC API ── */

void* c4_create_rpc_ctx(char* method, char* params, uint64_t chain_id, uint32_t prover_flags, uint32_t verify_flags, int use_remote_prover) {
  return (void*) c4_rpc_ctx_create(method, params, (chain_id_t) chain_id,
                                   (prover_flags_t) prover_flags, (verify_flags_t) verify_flags,
                                   use_remote_prover != 0);
}

char* c4_rpc_execute_json_status(void* ctx) {
  return c4_rpc_build_json_status((c4_rpc_ctx_t*) ctx, true);
}

void c4_rpc_set_witness_keys(void* ctx, const char* witness_keys) {
  c4_rpc_ctx_set_witness_keys((c4_rpc_ctx_t*) ctx, witness_keys);
}

void c4_free_rpc_ctx(void* ctx) {
  c4_rpc_ctx_free((c4_rpc_ctx_t*) ctx);
}
