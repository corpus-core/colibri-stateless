#include "proofer.h"
#include "../util/json.h"
#include "../util/state.h"
#include "proofs.h"
#include <stdlib.h>
#include <string.h>

proofer_ctx_t* c4_proofer_create(char* method, char* params, chain_id_t chain_id) {
  json_t params_json = json_parse(params);
  if (params_json.type != JSON_TYPE_ARRAY) return NULL;
  char* params_str = malloc(params_json.len + 1);
  memcpy(params_str, params_json.start, params_json.len);
  params_str[params_json.len] = 0;
  params_json.start           = params_str;
  proofer_ctx_t* ctx          = calloc(1, sizeof(proofer_ctx_t));
  ctx->method                 = strdup(method);
  ctx->params                 = params_json;
  ctx->chain_id               = chain_id;
  return ctx;
}

void c4_proofer_free(proofer_ctx_t* ctx) {
  c4_state_free(&ctx->state);
  if (ctx->method) free(ctx->method);
  if (ctx->params.start) free((void*) ctx->params.start);
  if (ctx->proof.data) free(ctx->proof.data);
  free(ctx);
}

c4_status_t c4_proofer_execute(proofer_ctx_t* ctx) {
  if (c4_state_get_pending_request(&ctx->state)) return C4_PENDING;
  if (ctx->state.error) return C4_ERROR;

  if (strcmp(ctx->method, "eth_getBalance") == 0 || strcmp(ctx->method, "eth_getCode") == 0 || strcmp(ctx->method, "eth_getNonce") == 0 || strcmp(ctx->method, "eth_getProof") == 0 || strcmp(ctx->method, "eth_getStorageAt") == 0)
    c4_proof_account(ctx);
  else if (strcmp(ctx->method, "eth_getTransactionByHash") == 0 || strcmp(ctx->method, "eth_getCode") == 0 || strcmp(ctx->method, "eth_getNonce") == 0 || strcmp(ctx->method, "eth_getProof") == 0 || strcmp(ctx->method, "eth_getStorageAt") == 0)
    c4_proof_transaction(ctx);
  else
    ctx->state.error = strdup("Unsupported method");

  return c4_proofer_status(ctx);
}

c4_status_t c4_proofer_status(proofer_ctx_t* ctx) {
  if (ctx->state.error) return C4_ERROR;
  if (ctx->proof.data) return C4_SUCCESS;
  if (c4_state_get_pending_request(&ctx->state)) return C4_PENDING;
  return C4_PENDING;
}

c4_status_t c4_send_eth_rpc(proofer_ctx_t* ctx, char* method, char* params, json_t* result) {
  bytes32_t id     = {0};
  buffer_t  buffer = {0};
  buffer_add_chars(&buffer, "{\"jsonrpc\":\"2.0\",\"method\":\"");
  buffer_add_chars(&buffer, method);
  buffer_add_chars(&buffer, "\",\"params\":");
  buffer_add_chars(&buffer, params);
  buffer_add_chars(&buffer, ",\"id\":1}");
  sha256(buffer.data, id);
  data_request_t* data_request = c4_state_get_data_request_by_id(&ctx->state, id);
  if (data_request) {
    buffer_free(&buffer);
    if (c4_state_is_pending(&ctx->state)) return C4_PENDING;
    if (!data_request->error && data_request->response.data) {
      json_t response = json_parse((char*) data_request->response.data);
      if (response.type != JSON_TYPE_OBJECT) {
        ctx->state.error = strdup("Invalid JSON response");
        return C4_ERROR;
      }

      json_t error = json_get(response, "error");
      if (error.type == JSON_TYPE_OBJECT) {
        error            = json_get(error, "message");
        ctx->state.error = json_new_string(error);
        return C4_ERROR;
      }
      else if (error.type == JSON_TYPE_STRING) {
        ctx->state.error = json_new_string(error);
        return C4_ERROR;
      }

      json_t res = json_get(response, "result");
      if (res.type == JSON_TYPE_NOT_FOUND || res.type == JSON_TYPE_INVALID) {
        ctx->state.error = strdup("Invalid JSON response");
        return C4_ERROR;
      }

      *result = res;
      return C4_SUCCESS;
    }
    else {
      ctx->state.error = strdup(data_request->error ? data_request->error : "Data request failed");
      return C4_ERROR;
    }
  }
  else {
    data_request = calloc(1, sizeof(data_request_t));
    memcpy(data_request->id, id, 32);
    data_request->payload  = buffer.data;
    data_request->encoding = C4_DATA_ENCODING_JSON;
    data_request->method   = C4_DATA_METHOD_POST;
    data_request->type     = C4_DATA_TYPE_ETH_RPC;
    c4_state_add_request(&ctx->state, data_request);
    return C4_PENDING;
  }

  return C4_SUCCESS;
}

c4_status_t c4_send_beacon_json(proofer_ctx_t* ctx, char* path, char* query, json_t* result) {
  bytes32_t id     = {0};
  buffer_t  buffer = {0};
  buffer_add_chars(&buffer, path);
  if (query) {
    buffer_add_chars(&buffer, "?");
    buffer_add_chars(&buffer, query);
  }
  sha256(buffer.data, id);
  data_request_t* data_request = c4_state_get_data_request_by_id(&ctx->state, id);
  if (data_request) {
    buffer_free(&buffer);
    if (c4_state_is_pending(&ctx->state)) return C4_PENDING;
    if (!data_request->error && data_request->response.data) {
      json_t response = json_parse((char*) data_request->response.data);
      if (response.type == JSON_TYPE_INVALID) {
        ctx->state.error = strdup("Invalid JSON response");
        return C4_ERROR;
      }

      *result = response;
      return C4_SUCCESS;
    }
    else {
      ctx->state.error = strdup(data_request->error ? data_request->error : "Data request failed");
      return C4_ERROR;
    }
  }
  else {
    data_request = calloc(1, sizeof(data_request_t));
    memcpy(data_request->id, id, 32);
    data_request->url      = (char*) buffer.data.data;
    data_request->encoding = C4_DATA_ENCODING_JSON;
    data_request->method   = C4_DATA_METHOD_GET;
    data_request->type     = C4_DATA_TYPE_BEACON_API;
    c4_state_add_request(&ctx->state, data_request);
    return C4_PENDING;
  }

  return C4_SUCCESS;
}

c4_status_t c4_send_beacon_ssz(proofer_ctx_t* ctx, char* path, char* query, bytes_t* result) {
  bytes32_t id     = {0};
  buffer_t  buffer = {0};
  buffer_add_chars(&buffer, path);
  if (query) {
    buffer_add_chars(&buffer, "?");
    buffer_add_chars(&buffer, query);
  }
  sha256(buffer.data, id);
  data_request_t* data_request = c4_state_get_data_request_by_id(&ctx->state, id);
  if (data_request) {
    buffer_free(&buffer);
    if (c4_state_is_pending(&ctx->state)) return C4_PENDING;
    if (!data_request->error && data_request->response.data) {
      *result = data_request->response;
      return C4_SUCCESS;
    }
    else {
      ctx->state.error = strdup(data_request->error ? data_request->error : "Data request failed");
      return C4_ERROR;
    }
  }
  else {
    data_request = calloc(1, sizeof(data_request_t));
    memcpy(data_request->id, id, 32);
    data_request->url      = (char*) buffer.data.data;
    data_request->encoding = C4_DATA_ENCODING_SSZ;
    data_request->method   = C4_DATA_METHOD_GET;
    data_request->type     = C4_DATA_TYPE_BEACON_API;
    c4_state_add_request(&ctx->state, data_request);
    return C4_PENDING;
  }

  return C4_SUCCESS;
}
