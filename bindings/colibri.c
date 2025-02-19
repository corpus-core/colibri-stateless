#include "colibri.h"
#include "../src/proofer/proofer.h"
#include "../src/verifier/sync_committee.h"
#include "../src/verifier/verify.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

proofer_t* create_proofer_ctx(char* method, char* params, uint64_t chain_id) {
  return (void*) c4_proofer_create(method, params, chain_id);
}

static const char* status_to_string(c4_status_t status) {
  switch (status) {
    case C4_SUCCESS:
      return "success";
    case C4_ERROR:
      return "error";
    case C4_PENDING:
      return "waiting";
  }
}

static const char* encoding_to_string(data_request_encoding_t encoding) {
  switch (encoding) {
    case C4_DATA_ENCODING_SSZ:
      return "ssz";
    case C4_DATA_ENCODING_JSON:
      return "json";
  }
}

static const char* method_to_string(data_request_method_t method) {
  switch (method) {
    case C4_DATA_METHOD_GET:
      return "get";
    case C4_DATA_METHOD_POST:
      return "post";
    case C4_DATA_METHOD_PUT:
      return "put";
    case C4_DATA_METHOD_DELETE:
      return "delete";
  }
}

static const char* data_request_type_to_string(data_request_type_t type) {
  switch (type) {
    case C4_DATA_TYPE_BEACON_API:
      return "beacon_api";
    case C4_DATA_TYPE_ETH_RPC:
      return "eth_rpc";
    case C4_DATA_TYPE_REST_API:
      return "rest_api";
  }
}
static void add_data_request(buffer_t* result, data_request_t* data_request) {
  bprintf(result, "{\"req_ptr\": %l,", (uint64_t) data_request);
  bprintf(result, "\"chain_id\": %d,", (uint32_t) data_request->chain_id);
  bprintf(result, "\"encoding\": \"%s\",", encoding_to_string(data_request->encoding));
  bprintf(result, "\"exclude_mask\": \"%d\",", (uint32_t) data_request->node_exclude_mask);
  bprintf(result, "\"method\": \"%s\",", method_to_string(data_request->method));
  bprintf(result, "\"url\": \"%s\",", data_request->url);
  if (data_request->payload.data)
    bprintf(result, "\"payload\": %j,", (json_t) {.len = data_request->payload.len, .start = (char*) data_request->payload.data, .type = JSON_TYPE_OBJECT});
  bprintf(result, "\"type\": \"%s\"}", data_request_type_to_string(data_request->type));
}

char* proofer_execute_json_status(proofer_t* proofer) {
  buffer_t       result = {0};
  proofer_ctx_t* ctx    = (proofer_ctx_t*) proofer;
  c4_status_t    status = c4_proofer_execute(ctx);
  bprintf(&result, "{\"status\": \"%s\",", status_to_string(status));
  switch (status) {
    case C4_SUCCESS:
      bprintf(&result, "\"result\": \"0x%lx\", \"result_len\": %d", (uint64_t) ctx->proof.data, ctx->proof.len);
      break;
    case C4_ERROR:
      bprintf(&result, "\"error\": %\"s\"", ctx->state.error);
      break;
    case C4_PENDING: {
      bprintf(&result, "\"requests\": [");
      data_request_t* data_request = c4_state_get_pending_request(&ctx->state);
      while (data_request) {
        if (!data_request->response.data && !data_request->error) {
          if (result.data.data[result.data.len - 1] != '[') bprintf(&result, ",");
          add_data_request(&result, data_request);
        }
        data_request = data_request->next;
      }

      bprintf(&result, "]");
      break;
    }
  }
  bprintf(&result, "}");
  return buffer_as_string(result);
}

void free_proofer_ctx(proofer_t* ctx) {
  c4_proofer_free((proofer_ctx_t*) ctx);
}
void req_set_response(void* req_ptr, bytes_t data, uint16_t node_index) {
  data_request_t* ctx      = (data_request_t*) req_ptr;
  ctx->response            = bytes(data.data, data.len);
  ctx->response_node_index = node_index;
}

void req_set_error(void* req_ptr, char* error, uint16_t node_index) {
  data_request_t* ctx      = (data_request_t*) req_ptr;
  ctx->error               = strdup(error);
  ctx->response_node_index = node_index;
}

char* verify_proof(uint8_t* proof, uint32_t proof_len, char* method, char* args, uint64_t chain_id) {
  verify_ctx_t ctx = {0};
  buffer_t     buf = {0};
  c4_verify_from_bytes(&ctx, bytes(proof, proof_len), method, json_parse(args), chain_id);

  if (ctx.success)
    bprintf(&buf, "{\"result\": %z}", ctx.data);
  else if (ctx.first_missing_period) {
    char url[200] = {0};
    sprintf(url, "eth/v1/beacon/light_client/updates?start_period=%d&count=%d", (uint32_t) ctx.first_missing_period - 1, (uint32_t) (ctx.last_missing_period - ctx.first_missing_period + 1));
    data_request_t* req = malloc(sizeof(data_request_t));
    *req                = (data_request_t) {
                       .encoding = C4_DATA_ENCODING_SSZ,
                       .error    = NULL,
                       .id       = {0},
                       .method   = C4_DATA_METHOD_GET,
                       .payload  = {0},
                       .response = {0},
                       .type     = C4_DATA_TYPE_BEACON_API,
                       .url      = url,
                       .chain_id = chain_id};
    sha256(bytes((uint8_t*) url, strlen(url)), req->id);
    bprintf(&buf, "{\"error\": \"%s\", \"client_updates\": [", ctx.state.error);
    add_data_request(&buf, req);
    bprintf(&buf, "]}");
  }
  else
    bprintf(&buf, "{\"error\": \"%s\"}", ctx.state.error);

  if (ctx.state.error) free(ctx.state.error);

  return (char*) buf.data.data;
}

bool c4w_handle_client_updates(data_request_t* client_update, uint64_t chain_id) {
  return c4_handle_client_updates(client_update->response, chain_id, NULL);
}

void c4w_req_free(data_request_t* client_update) {
  if (client_update->error) free(client_update->error);
  if (client_update->response.data) free(client_update->response.data);
  free(client_update);
}

uint8_t* c4w_buffer_alloc(buffer_t* buf, size_t len) {
  buffer_grow(buf, len + 1);
  buf->data.len = len;
  return buf->data.data;
}

char* c4w_init_chain(uint64_t chain_id, char* trusted_block_hashes, data_request_t* requests) {
  buffer_t   buf    = {0};
  json_t     blocks = json_parse(trusted_block_hashes ? trusted_block_hashes : "[]");
  c4_state_t state  = {0};
  state.requests    = requests;

  c4_status_t status = c4_set_trusted_blocks(&state, blocks, chain_id);
  if (state.error) {
    bprintf(&buf, "{\"error\": \"%s\"}", state.error);
    c4_state_free(&state);
    return (char*) buf.data.data;
  }

  bprintf(&buf, "{\"req_ptr\": \"%lx\", \"requests\": [", (uint64_t) state.requests);
  requests = state.requests;
  while (requests) {
    if (!requests->error && !requests->response.data) {
      if (buf.data.data[buf.data.len - 1] != '[') bprintf(&buf, ",");
      add_data_request(&buf, requests);
    }
    requests = requests->next;
  }

  if (!c4_state_get_pending_request(&state)) c4_state_free(&state);
  return bprintf(&buf, "]}");
}

bytes_t proofer_get_proof(proofer_t* proofer) {
  proofer_ctx_t* ctx = (proofer_ctx_t*) proofer;
  return ctx->proof;
}
/*
bytes_t c4_proof(char* method, char* params, uint64_t chain_id, char** error) {

  buffer_t buf    = {0};
  bytes_t  result = {0};

  proofer_t* proofer = create_proofer_ctx(method, params, chain_id);
  while (true) {
    char*  state      = proofer_execute_json_status(proofer);
    json_t state_json = json_parse(state);
    json_t status     = json_get(state_json, "status");
    if (json_equal_string(status, "success")) {
      free(state);
      uint8_t* proof     = (uint8_t*) json_get_uint64(json, "result");
      uint32_t proof_len = json_get_uint32(json, "result_len");
      result             = bytes_dup(bytes(proof, proof_len));
      break;
    }
    else if (json_string_equal(status, "error")) {
      *error = json_new_string(json_get(state_json, "error"));
      free(state);
      break;
    }
    else { // there are pending requests we need to fetch first
      json_for_each_value(json_get(state_json, "requests"), req) {
        // this should be done async in kotlin
        bytes_t response = curl_fetch(
            json_get(req, "url"),
            json_get_bytes(req, "payload", &buf),
            json_get(req, "method"),
            json_get_uint32(req, "chain_id"));
        void* req_ptr = json_get_uint64(req, "req_ptr");
        if (response.len) {
          uint8_t* target = req_create_response(req_ptr, response.len, 0);
          memcpy(target, response.data, response.len);
        }
        else {
          req_set_error(req_ptr, "Failed to fetch data", 0);
        }
      }
      free(state);
    }
  }

  free_proofer_ctx(proofer);
  return result;
}




    suspend fun getProof(method: String, args: Array<Any>): ByteArray {
        return withContext(Dispatchers.IO) {
            var error: String? = null
            var requests: Array<Request>? = null
            var proof: ByteArray? = null

            long ctx =colibriJNI.create_proofer_ctx(method, args, chainId)

            while (true) {
                var state_string = colibriJNI.proofer_execute_json_status(ctx)
                var state = JSON.parse( state_string ) // How do I parse the json string to a json object?
                if (state.status == "success") {
                    proof = new ByteArray(state.result, state.result_len) // how do I access C-pointers in Kotlin
                }
                else if (state.status == "error") {
                    error = state.error
                }
                else {
                    // there are pending requests we need to fetch first
                    // maybe even in different threads to run them parallel?
                    state.requests.forEach { request ->
                        var response = await fetchRequest(request)
                        if (response.len > 0) {
                            var target = colibriJNI.req_create_response(request.req_ptr, response.len, 0)
                            memcpy(target, response.data, response.len)
                        }
                        else {
                            colibriJNI.req_set_error(request.req_ptr, "Failed to fetch data", 0)
                        }
                    }
                }
            }
            colibriJNI.free_proofer_ctx(ctx)
            proof ?: throw RuntimeException(error ?: "Failed to generate proof")
        }
    }
*/