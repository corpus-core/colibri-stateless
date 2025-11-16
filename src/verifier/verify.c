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

#include "verify.h"
#include "../util/json.h"
#include "../util/ssz.h"
#include VERIFIERS_PATH
#include <stdlib.h>
#include <string.h>

// Macro to initialize an empty SSZ object
#define INIT_EMPTY_SSZ_OBJ(obj)           \
  do {                                    \
    (obj).def        = &ssz_none;         \
    (obj).bytes.data = (void*) &ssz_none; \
  } while (0)

const ssz_def_t* c4_get_request_type(chain_type_t chain_type) {
  return request_container(chain_type);
}

c4_status_t c4_verify_init(verify_ctx_t* ctx, bytes_t request_bytes, char* method, json_t args, chain_id_t chain_id) {
  chain_type_t chain_type = c4_chain_type(chain_id);
  // Input validation
  if (!ctx) return C4_ERROR;
  if (!method) THROW_ERROR("method cannot be NULL");

  memset(ctx, 0, sizeof(verify_ctx_t));
  if (request_bytes.len == 0) {
    method_type_t method_type = c4_get_method_type(chain_id, method);
    if (method_type == METHOD_UNDEFINED)
      THROW_ERROR("method not known");
    else if (method_type == METHOD_NOT_SUPPORTED)
      THROW_ERROR("method not supported");
    else if (method_type == METHOD_PROOFABLE)
      THROW_ERROR("missing proof!");
    INIT_EMPTY_SSZ_OBJ(ctx->data);
    INIT_EMPTY_SSZ_OBJ(ctx->proof);
    INIT_EMPTY_SSZ_OBJ(ctx->sync_data);
  }
  else {
    if (chain_type != c4_get_chain_type_from_req(request_bytes))
      THROW_ERROR_WITH("chain type (%d) does not match the proof (%d)", chain_type, c4_get_chain_type_from_req(request_bytes));
    ssz_ob_t request = {.bytes = request_bytes, .def = request_container(chain_type)};
    if (!request.def) THROW_ERROR("chain not supported");
    if (!ssz_is_valid(request, true, &ctx->state)) return C4_ERROR;
    ctx->data      = ssz_get(&request, "data");
    ctx->proof     = ssz_get(&request, "proof");
    ctx->sync_data = ssz_get(&request, "sync_data");
  }
  ctx->chain_id = chain_id;
  ctx->method   = method;
  ctx->args     = args;
  return C4_SUCCESS;
}

c4_status_t c4_verify_from_bytes(verify_ctx_t* ctx, bytes_t request_bytes, char* method, json_t args, chain_id_t chain_id) {
  TRY_ASYNC(c4_verify_init(ctx, request_bytes, method, args, chain_id));
  return c4_verify(ctx);
}

c4_status_t c4_verify(verify_ctx_t* ctx) {
  // make sure the state is clean
  if (ctx->state.error) return C4_ERROR;
  if (c4_state_get_pending_request(&ctx->state)) return C4_PENDING;

  // verify the proof
  if (!handle_verification(ctx))
    ctx->state.error = bprintf(NULL, "verification for proof of chain %l is not supported", ctx->chain_id);

  return (ctx->state.error ? C4_ERROR : (c4_state_get_pending_request(&ctx->state) ? C4_PENDING : C4_SUCCESS));
}

chain_type_t c4_get_chain_type_from_req(bytes_t request_bytes) {
  // Chain type is encoded in the first byte of the request
  // Default to Ethereum if request is too short or invalid
  if (request_bytes.len < 4 || !request_bytes.data)
    return C4_CHAIN_TYPE_ETHEREUM;
  else
    // First byte corresponds to the chain_type_t enum value
    return request_bytes.data[0];
}

const ssz_def_t* c4_get_req_type_from_req(bytes_t request_bytes) {
  return c4_get_request_type(c4_get_chain_type_from_req(request_bytes));
}

void c4_verify_free_data(verify_ctx_t* ctx) {
  if (ctx->flags & VERIFY_FLAG_FREE_DATA) {
    safe_free(ctx->data.bytes.data);
    ctx->data.bytes.data = NULL;
  }
  c4_state_free(&ctx->state);
}
