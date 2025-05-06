#include "beacon.h"
#include "logger.h"
#include "server.h"
typedef struct {
  uv_work_t      req;
  request_t*     req_obj;
  proofer_ctx_t* ctx;
} proof_work_t;

// --- executed in worker-thread ---
static void c4_proofer_execute_worker(uv_work_t* req) {
  proof_work_t* work = (proof_work_t*) req->data;
  c4_proofer_execute(work->ctx);
  work->ctx->flags &= ~C4_PROOFER_FLAG_UV_WORKER_REQUIRED;
}

static void c4_proofer_execute_after(uv_work_t* req, int status) {
  proof_work_t* work = (proof_work_t*) req->data;
  c4_proofer_handle_request(work->req_obj);
  safe_free(req);
}

static void proofer_request_free(request_t* req) {
  c4_proofer_free((proofer_ctx_t*) req->ctx);
  safe_free(req);
}

void c4_proofer_handle_request(request_t* req) {
  if (c4_check_retry_request(req)) return; // if there are data_request in the req, we either clean it up or retry in case of an error (if possible.)
  proofer_ctx_t* ctx = (proofer_ctx_t*) req->ctx;
  if (ctx->flags & C4_PROOFER_FLAG_UV_WORKER_REQUIRED && c4_proofer_status(ctx) == C4_PENDING && c4_state_get_pending_request(&ctx->state) == NULL) {
    // no data are required and no pending request, so we can execute the proofer in the worker thread
    proof_work_t* work = (proof_work_t*) safe_calloc(1, sizeof(proof_work_t));
    work->req_obj      = req;
    work->ctx          = ctx;
    work->req.data     = work;

    uv_queue_work(uv_default_loop(), &work->req,
                  c4_proofer_execute_worker,
                  c4_proofer_execute_after);
    return;
  }

  switch (c4_proofer_execute(ctx)) {
    case C4_SUCCESS:
      c4_http_respond(req->client, 200, "application/octet-stream", ctx->proof);
      proofer_request_free(req);
      return;
    case C4_ERROR: {
      buffer_t buf = {0};
      bprintf(&buf, "{\"error\":\"%s\"}", ctx->state.error);
      c4_http_respond(req->client, 500, "application/json", buf.data);
      buffer_free(&buf);
      proofer_request_free(req);
      return;
    }
    case C4_PENDING:
      if (c4_state_get_pending_request(&ctx->state)) // there are pending requests, let's take care of them first
        c4_start_curl_requests(req);
      else if (ctx->flags & C4_PROOFER_FLAG_UV_WORKER_REQUIRED) // worker is required, retry and handle it in the beginning of the next loop
        c4_proofer_handle_request(req);
      else {
        // stop here, we don't have anything to do
        char* error = "{\"error\":\"Internal proofer error: no proofer available\"}";
        c4_http_respond(req->client, 500, "application/json", bytes((uint8_t*) error, strlen(error)));
        proofer_request_free(req);
      }

      return;
  }
}
bool c4_handle_proof_request(client_t* client) {
  if (client->request.method != C4_DATA_METHOD_POST /*|| strncmp(client->request.path, "/proof/", 7) != 0*/)
    return false;

  json_t rpc_req = json_parse((char*) client->request.payload);
  if (rpc_req.type != JSON_TYPE_OBJECT) {
    c4_http_respond(client, 400, "application/json", bytes("{\"error\":\"Invalid request\"}", 27));
    return true;
  }
  json_t method       = json_get(rpc_req, "method");
  json_t params       = json_get(rpc_req, "params");
  json_t client_state = json_get(rpc_req, "c4");
  if (method.type != JSON_TYPE_STRING || params.type != JSON_TYPE_ARRAY) {
    c4_http_respond(client, 400, "application/json", bytes("{\"error\":\"Invalid request\"}", 27));
    return true;
  }

  buffer_t   client_state_buf = {0};
  char*      method_str       = bprintf(NULL, "%j", method);
  char*      params_str       = bprintf(NULL, "%J", params);
  request_t* req              = (request_t*) safe_calloc(1, sizeof(request_t));
  req->client                 = client;
  req->cb                     = c4_proofer_handle_request;
  req->ctx                    = c4_proofer_create(method_str, params_str, (chain_id_t) http_server.chain_id, C4_PROOFER_FLAG_UV_SERVER_CTX);
  if (client_state.type == JSON_TYPE_STRING) ((proofer_ctx_t*) req->ctx)->client_state = json_as_bytes(client_state, &client_state_buf);

  safe_free(method_str);
  safe_free(params_str);
  c4_proofer_handle_request(req);
  return true;
}
