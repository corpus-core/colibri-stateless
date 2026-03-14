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

#ifdef PAP

#include "pap_req.h"
#include "beacon_types.h"
#include "bytes.h"
#include "crypto.h"
#include "json.h"
#include "ssz.h"
#include "state.h"
#include <string.h>

c4_status_t pap_request_proof(verify_ctx_t* ctx, const char* method, const char* params, ssz_ob_t* out) {
  buffer_t  payload = {0};
  bytes32_t req_id;
  bprintf(&payload, "{\"method\":\"%s\",\"params\":%s}", method, params);
  keccak(payload.data, req_id);

  data_request_t* req = c4_state_get_data_request_by_id(&ctx->state, req_id);
  if (req && req->response.data) {
    buffer_free(&payload);
    ssz_ob_t ob = {.bytes = req->response, .def = eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST)};

    if (!req->validated) {
      if (!ssz_is_valid(ob, true, &ctx->state))
        return C4_ERROR;
      req->validated = true;
    }

    *out = ob;
    return C4_SUCCESS;
  }

  if (!req) {
    data_request_t* new_req = safe_calloc(1, sizeof(data_request_t));
    new_req->chain_id       = ctx->chain_id;
    new_req->encoding       = C4_DATA_ENCODING_SSZ;
    new_req->type           = C4_DATA_TYPE_PROVER;
    new_req->method         = C4_DATA_METHOD_POST;
    new_req->payload        = payload.data; /* ownership transferred to request */
    memcpy(new_req->id, req_id, 32);
    c4_state_add_request(&ctx->state, new_req);
    return C4_PENDING;
  }

  buffer_free(&payload);
  if (req->error)
    c4_state_add_error(&ctx->state, req->error);
  return C4_ERROR;
}

c4_status_t pap_request_get(verify_ctx_t* ctx, const char* path, bytes_t* out) {
  bytes32_t req_id;
  keccak(bytes((uint8_t*) path, (uint32_t) strlen(path)), req_id);

  data_request_t* req = c4_state_get_data_request_by_id(&ctx->state, req_id);
  if (req && req->response.data) {
    *out = req->response;
    return C4_SUCCESS;
  }

  if (!req) {
    data_request_t* new_req = safe_calloc(1, sizeof(data_request_t));
    new_req->chain_id       = ctx->chain_id;
    new_req->encoding       = C4_DATA_ENCODING_SSZ;
    new_req->type           = C4_DATA_TYPE_PROVER;
    new_req->method         = C4_DATA_METHOD_GET;
    new_req->url            = strdup(path);
    memcpy(new_req->id, req_id, 32);
    c4_state_add_request(&ctx->state, new_req);
    return C4_PENDING;
  }

  if (req->error)
    c4_state_add_error(&ctx->state, req->error);
  return C4_ERROR;
}

#define PAP_ETH_RPC_MAX_RESPONSE (256u * 1024u)

c4_status_t pap_request_eth_rpc(verify_ctx_t* ctx, const char* method, const char* params, const char* json_check, json_t* out) {
  buffer_t  payload = {0};
  bytes32_t req_id;
  bprintf(&payload, "{\"jsonrpc\":\"2.0\",\"method\":\"%s\",\"params\":%s,\"id\":1}", method, params);
  keccak(payload.data, req_id);

  data_request_t* req = c4_state_get_data_request_by_id(&ctx->state, req_id);
  if (req && req->response.data) {
    buffer_free(&payload);

    if (req->response.len > PAP_ETH_RPC_MAX_RESPONSE) {
      c4_state_add_error(&ctx->state, "PAP: eth_rpc response exceeds size limit");
      return C4_ERROR;
    }

    json_t response = json_parse((char*) req->response.data);
    if (response.type != JSON_TYPE_OBJECT) {
      c4_state_add_error(&ctx->state, "PAP: invalid JSON-RPC response");
      return C4_ERROR;
    }

    json_t error = json_get(response, "error");
    if (error.type == JSON_TYPE_OBJECT) {
      char     err_buf[256];
      buffer_t eb = stack_buffer(err_buf);
      c4_state_add_error(&ctx->state, bprintf(&eb, "PAP: eth_rpc error for %s: %j", method, json_get(error, "message")));
      return C4_ERROR;
    }
    else if (error.type == JSON_TYPE_STRING) {
      char     err_buf[256];
      buffer_t eb = stack_buffer(err_buf);
      c4_state_add_error(&ctx->state, bprintf(&eb, "PAP: eth_rpc error for %s: %j", method, error));
      return C4_ERROR;
    }

    json_t result = json_get(response, "result");
    if (result.type == JSON_TYPE_NOT_FOUND || result.type == JSON_TYPE_INVALID) {
      c4_state_add_error(&ctx->state, "PAP: missing result in JSON-RPC response");
      return C4_ERROR;
    }

    if (!req->validated) {
      if (json_check) {
        char* err = (char*) json_validate(result, json_check, "PAP: invalid result: ");
        if (err)
          return c4_state_set_error_msg(&ctx->state, err);
      }
      req->validated = true;
    }

    *out = result;
    return C4_SUCCESS;
  }

  if (!req) {
    data_request_t* new_req = safe_calloc(1, sizeof(data_request_t));
    new_req->chain_id       = ctx->chain_id;
    new_req->encoding       = C4_DATA_ENCODING_JSON;
    new_req->type           = C4_DATA_TYPE_ETH_RPC;
    new_req->method         = C4_DATA_METHOD_POST;
    new_req->payload        = payload.data; /* ownership transferred to request */
    memcpy(new_req->id, req_id, 32);
    c4_state_add_request(&ctx->state, new_req);
    return C4_PENDING;
  }

  buffer_free(&payload);
  if (req->error)
    c4_state_add_error(&ctx->state, req->error);
  return C4_ERROR;
}

#endif /* PAP */
