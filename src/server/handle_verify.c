/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "beacon.h"
#include "logger.h"
#include "plugin.h"
#include "proofer.h"
#include "server.h"
#include "verify.h"

typedef struct {
  char*        method;
  json_t       params;
  json_t       id;
  bytes_t      proof;
  verify_ctx_t ctx;
  request_t    req;
} verify_request_t;

static bytes_t get_client_state(chain_id_t chain_id) {
  static bool server_storage_initialized = false;

  // Initialize server storage on first call
  if (!server_storage_initialized) {
    c4_init_server_storage();
    server_storage_initialized = true;
  }

  uint8_t          buf[100];
  buffer_t         buffer = stack_buffer(buf);
  buffer_t         result = {0};
  storage_plugin_t storage;
  c4_get_storage_config(&storage);

  return storage.get(bprintf(&buffer, "states_%l", (uint64_t) chain_id), &result) ? result.data : NULL_BYTES;
}

static void free_verify_request(verify_request_t* verify_req) {
  c4_verify_free_data(&verify_req->ctx);
  safe_free(verify_req->method);
  safe_free(verify_req->proof.data);
  safe_free(verify_req);
}

// handles the verification process
static void verifier_handle_request(request_t* req) {
  verify_request_t* verify_req = (verify_request_t*) req->ctx;

  if (c4_check_retry_request(req)) return; // if there are data_request in the req, we either clean it up or retry in case of an error (if possible.)

  switch (c4_verify(&verify_req->ctx)) {
    case C4_SUCCESS: {
      buffer_t buf = {0};
      bprintf(&buf, "{\"id\": %J, \"result\": %Z}", verify_req->id, verify_req->ctx.data);
      c4_http_respond(req->client, 200, "application/json", buf.data);
      buffer_free(&buf);
      free_verify_request(verify_req);
      return;
    }
    case C4_ERROR: {
      buffer_t buf = {0};
      bprintf(&buf, "{\"id\": %J, \"error\":\"%S\"}", verify_req->id, verify_req->ctx.state.error);
      c4_http_respond(req->client, 200, "application/json", buf.data);
      buffer_free(&buf);
      free_verify_request(verify_req);
      return;
    }
    case C4_PENDING:
      if (c4_state_get_pending_request(&verify_req->ctx.state)) // there are pending requests, let's take care of them first
        c4_start_curl_requests(&verify_req->req, &verify_req->ctx.state);
      else {
        buffer_t buf = {0};
        bprintf(&buf, "{\"id\": %J, \"error\":\"%s\"}", verify_req->id, "No proofer available");
        c4_http_respond(req->client, 200, "application/json", buf.data);
        buffer_free(&buf);
        free_verify_request(verify_req);
      }
  }
}

// callback handler called once the proofs is ready
static void proofer_callback(client_t* client, void* data, data_request_t* req) {
  verify_request_t* verify_req         = (verify_request_t*) data;
  c4_state_t        data_request_state = {.requests = req};

  if (req->error) {
    c4_write_error_response(client, 500, req->error);
    c4_state_free(&data_request_state);
    free_verify_request(verify_req);
    return;
  }

  if (!req->response.data && c4_get_method_type(http_server.chain_id, verify_req->method) == METHOD_PROOFABLE) {
    buffer_t buffer = {0};
    bprintf(&buffer, "{\"id\": %J, \"error\":\"Internal proofer error: no proof available\"}", verify_req->id);
    c4_http_respond(client, 200, "application/json", buffer.data);
    buffer_free(&buffer);
    c4_state_free(&data_request_state);
    free_verify_request(verify_req);
    return;
  }

  // valid config?
  if (c4_verify_init(&verify_req->ctx, req->response, verify_req->method, verify_req->params, http_server.chain_id) != C4_SUCCESS) {
    buffer_t buffer = {0};
    bprintf(&buffer, "{\"id\": %J, \"error\":\"%S\"}", verify_req->id, verify_req->ctx.state.error);
    c4_http_respond(client, 200, "application/json", buffer.data);
    buffer_free(&buffer);
    c4_state_free(&data_request_state);
    free_verify_request(verify_req);
    return;
  }

  // all good, let's verify the proof
  verify_req->proof = req->response;    // keep the proof, because we will cleanup the data request
  verify_req->req.cb(&verify_req->req); // execute the verifier
  req->response = NULL_BYTES;           // we clean up the the data request, but without the proof, will be freed in the callback
  c4_state_free(&data_request_state);
}

bool c4_handle_verify_request(client_t* client) {
  // do we want to handle this request?
  if (client->request.method != C4_DATA_METHOD_POST || strncmp(client->request.path, "/rpc", 4) != 0)
    return false;

  json_t rpc_req = json_parse((char*) client->request.payload);
  if (rpc_req.type != JSON_TYPE_OBJECT) {
    c4_write_error_response(client, 400, "Invalid request, expected a JSON-RPC request");
    return true;
  }

  verify_request_t* verify_req = (verify_request_t*) safe_calloc(1, sizeof(verify_request_t));
  verify_req->req.client       = client;
  verify_req->req.cb           = verifier_handle_request;
  verify_req->req.ctx          = verify_req;

  json_t method      = json_get(rpc_req, "method");
  verify_req->params = json_get(rpc_req, "params");
  verify_req->id     = json_get(rpc_req, "id");

  if (method.type != JSON_TYPE_STRING || verify_req->params.type != JSON_TYPE_ARRAY) {
    safe_free(verify_req);
    c4_write_error_response(client, 400, "Invalid request");
    return true;
  }
  else
    verify_req->method = bprintf(NULL, "%j", method);

  // check method
  switch (c4_get_method_type(http_server.chain_id, verify_req->method)) {

    case METHOD_UNDEFINED: {
      free_verify_request(verify_req);
      c4_write_error_response(client, 400, "Method not known");
      return true;
    }

    case METHOD_NOT_SUPPORTED: {
      free_verify_request(verify_req);
      c4_write_error_response(client, 400, "Method not supported");
      return true;
    }

    case METHOD_UNPROOFABLE: {
      free_verify_request(verify_req);
      c4_write_error_response(client, 400, "Method unproofable");
      return true;
    }

    case METHOD_LOCAL: {
      proofer_callback(client, verify_req, (data_request_t*) safe_calloc(1, sizeof(data_request_t)));
      return true;
    }

    case METHOD_PROOFABLE: {

      // get client_state
      bytes_t client_state = get_client_state(http_server.chain_id);

      if (c4_get_server_list(C4_DATA_TYPE_PROOFER)->count) {
        // we have a remote proofer, so we use it directly
        buffer_t buffer = {0};
        bprintf(&buffer, "{\"method\":\"%s\",\"params\":%j,\"c4\":\"0x%x\"}", verify_req->method, verify_req->params, client_state);
        safe_free(client_state.data);

        data_request_t* req = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
        req->method         = C4_DATA_METHOD_POST;
        req->chain_id       = http_server.chain_id;
        req->type           = C4_DATA_TYPE_PROOFER;
        req->encoding       = C4_DATA_ENCODING_SSZ;
        req->payload        = buffer.data;
        c4_add_request(client, req, verify_req, proofer_callback);
      }
      else {
        // we use the local proofer
        buffer_t       client_state_buf = {0};
        char*          params_str       = bprintf(NULL, "%J", verify_req->params);
        request_t*     req              = (request_t*) safe_calloc(1, sizeof(request_t));
        proofer_ctx_t* ctx              = c4_proofer_create(verify_req->method, params_str, (chain_id_t) http_server.chain_id, C4_PROOFER_FLAG_UV_SERVER_CTX | C4_PROOFER_FLAG_INCLUDE_CODE | (http_server.period_store ? C4_PROOFER_FLAG_CHAIN_STORE : 0));
        req->start_time                 = current_ms();
        req->client                     = client;
        req->cb                         = c4_proofer_handle_request;
        req->ctx                        = ctx;
        req->parent_ctx                 = verify_req;
        req->parent_cb                  = proofer_callback;
        ctx->client_state               = client_state;

        safe_free(params_str);
        req->cb(req);
      }
    }
  }

  return true;
}
