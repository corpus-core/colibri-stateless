#include "server.h"

void c4_process_next_step(request_t* req) {
  if (c4_check_retry_request(req)) return;
  proofer_ctx_t* ctx = (proofer_ctx_t*) req->ctx;

  switch (c4_proofer_execute(ctx)) {
    case C4_SUCCESS:
      c4_http_respond(req->client, 200, "application/octet-stream", ctx->proof);
      c4_proofer_free(ctx);
      free(req);
      return;
    case C4_ERROR: {
      buffer_t buf = {0};
      bprintf(NULL, "{\"error\":\"%s\"}", ctx->state.error);
      c4_http_respond(req->client, 500, "application/json", buf.data);
      buffer_free(&buf);
      c4_proofer_free(ctx);
      free(req);
      return;
    }
    case C4_PENDING:
      c4_start_curl_requests(req);
      return;
  }
}
bool c4_handle_proof_request(client_t* client) {
  if (client->request.method != C4_DATA_METHOD_POST || strncmp(client->request.path, "/proof/", 7) != 0)
    return false;

  json_t rpc_req = json_parse((char*) client->request.payload);
  if (rpc_req.type != JSON_TYPE_OBJECT) {
    c4_http_respond(client, 400, "application/json", bytes("{\"error\":\"Invalid request\"}", 27));
    return true;
  }
  json_t method = json_get(rpc_req, "method");
  json_t params = json_get(rpc_req, "params");
  if (method.type != JSON_TYPE_STRING || params.type != JSON_TYPE_ARRAY) {
    c4_http_respond(client, 400, "application/json", bytes("{\"error\":\"Invalid request\"}", 27));
    return true;
  }

  char*      method_str = json_new_string(method);
  char*      params_str = bprintf(NULL, "%J", params);
  request_t* req        = calloc(1, sizeof(request_t));
  req->client           = client;
  req->cb               = c4_process_next_step;
  req->ctx              = c4_proofer_create(method_str, params_str, C4_CHAIN_MAINNET, 0);
  free(method_str);
  free(params_str);
  c4_process_next_step(req);
  return true;
}

bool c4_handle_status(client_t* client) {
  char* text = "<html><body><h1>Status</h1><p>Proofer is running</p></body></html>";
  c4_http_respond(client, 200, "text/html", bytes(text, strlen(text)));
  return true;
}

static void c4_proxy_callback(client_t* client, void* data, data_request_t* req) {
  if (req->response.data)
    c4_http_respond(client, 200, "application/json", req->response);
  else {
    buffer_t buf = {0};
    bprintf(&buf, "{\"error\":\"%s\"}", req->error);
    c4_http_respond(client, 500, "application/json", buf.data);
    buffer_free(&buf);
  }
  free(req->url);
  free(req->response.data);
  free(req->error);
  free(req);
}

bool c4_proxy(client_t* client) {
  if (strncmp(client->request.path, "/beacon/", 8) != 0) return false;
  data_request_t* req = calloc(1, sizeof(data_request_t));
  req->url            = bprintf(NULL, "/eth/v1/beacon/%s", client->request.path + 8);
  req->method         = C4_DATA_METHOD_GET;
  req->chain_id       = C4_CHAIN_MAINNET;
  req->type           = C4_DATA_TYPE_BEACON_API;
  req->encoding       = C4_DATA_ENCODING_JSON;
  c4_add_request(client, req, NULL, c4_proxy_callback);
  return true;
}