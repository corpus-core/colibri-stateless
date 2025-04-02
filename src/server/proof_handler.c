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
}

static void c4_proofer_execute_after(uv_work_t* req, int status) {
  proof_work_t* work = (proof_work_t*) req->data;
  c4_proofer_handle_request(work->req_obj);
  free(req);
}

static void proofer_request_free(request_t* req) {
  c4_proofer_free((proofer_ctx_t*) req->ctx);
  free(req);
}

void c4_proofer_handle_request(request_t* req) {
  if (c4_check_retry_request(req)) return; // if there are data_request in the req, we either clean it up or retry in case of an error (if possible.)
  proofer_ctx_t* ctx = (proofer_ctx_t*) req->ctx;
  if (ctx->flags & C4_PROOFER_FLAG_UV_WORKER_REQUIRED && c4_proofer_status(ctx) == C4_PENDING && c4_state_get_pending_request(&ctx->state) == NULL) {
    // no data are required and no pending request, so we can execute the proofer in the worker thread
    proof_work_t* work = (proof_work_t*) calloc(1, sizeof(proof_work_t));
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
  json_t method = json_get(rpc_req, "method");
  json_t params = json_get(rpc_req, "params");
  if (method.type != JSON_TYPE_STRING || params.type != JSON_TYPE_ARRAY) {
    c4_http_respond(client, 400, "application/json", bytes("{\"error\":\"Invalid request\"}", 27));
    return true;
  }

  char*      method_str = bprintf(NULL, "%j", method);
  char*      params_str = bprintf(NULL, "%J", params);
  request_t* req        = (request_t*) calloc(1, sizeof(request_t));
  req->client           = client;
  req->cb               = c4_proofer_handle_request;
  req->ctx              = c4_proofer_create(method_str, params_str, (chain_id_t) http_server.chain_id, C4_PROOFER_FLAG_UV_SERVER_CTX);
  free(method_str);
  free(params_str);
  c4_proofer_handle_request(req);
  return true;
}

bool c4_handle_status(client_t* client) {
  char* text = "<html><body><h1>Status</h1><p>Proofer is running</p></body></html>";
  c4_http_respond(client, 200, "text/html", bytes(text, strlen(text)));
  return true;
}

static void c4_proxy_callback(client_t* client, void* data, data_request_t* req) {
  // Check if client is still valid before responding
  if (!client || client->being_closed) {
    fprintf(stderr, "WARNING: Client is no longer valid or is being closed - discarding proxy response\n");
    // Clean up resources
    if (req) {
      free(req->url);
      free(req->response.data);
      free(req->error);
      free(req);
    }
    return;
  }

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
  data_request_t* req = (data_request_t*) calloc(1, sizeof(data_request_t));
  req->url            = bprintf(NULL, "/eth/v1/beacon/%s", client->request.path + 8);
  req->method         = C4_DATA_METHOD_GET;
  req->chain_id       = C4_CHAIN_MAINNET;
  req->type           = C4_DATA_TYPE_BEACON_API;
  req->encoding       = C4_DATA_ENCODING_JSON;
  c4_add_request(client, req, NULL, c4_proxy_callback);
  return true;
}

static void handle_new_head_cb(request_t* req) {
  proofer_ctx_t* ctx = (proofer_ctx_t*) req->ctx;
  beacon_head_t* b   = (beacon_head_t*) ctx->proof.data;
  ssz_ob_t       sig_block, data_block;

  switch (c4_eth_get_signblock_and_parent(ctx, b->root, NULL, &sig_block, &data_block)) {
    case C4_SUCCESS: {
      bytes32_t cache_key = {0};
      sprintf((char*) cache_key, "Slatest");
      c4_proofer_cache_invalidate(cache_key);
      beacon_block_t* beacon_block = (beacon_block_t*) calloc(1, sizeof(beacon_block_t));
      ssz_ob_t        sig_body     = ssz_get(&sig_block, "body");
      beacon_block->slot           = ssz_get_uint64(&data_block, "slot");
      beacon_block->header         = data_block;
      beacon_block->body           = ssz_get(&data_block, "body");
      beacon_block->execution      = ssz_get(&beacon_block->body, "executionPayload");
      beacon_block->sync_aggregate = ssz_get(&sig_body, "syncAggregate");
      bytes_t  root_hash           = ssz_get(&sig_block, "parentRoot").bytes;
      ssz_ob_t execution           = ssz_get(&sig_body, "executionPayload");
      c4_beacon_cache_update_blockdata(ctx, beacon_block, ssz_get_uint64(&execution, "timestamp"), root_hash.data);
      proofer_request_free(req);
      return;
    }
    case C4_ERROR: {
      log_error("Error fetching sigblock and parent: %s", ctx->state.error);
      proofer_request_free(req);
      return;
    }
    case C4_PENDING:
      if (c4_state_get_pending_request(&ctx->state)) // there are pending requests, let's take care of them first
        c4_start_curl_requests(req);
      else {
        log_error("Error fetching sigblock and parent: %s", ctx->state.error);
        proofer_request_free(req);
      }

      return;
  }
}

void c4_handle_new_head(json_t head) {

  beacon_head_t* b      = (beacon_head_t*) calloc(1, sizeof(beacon_head_t));
  buffer_t       buffer = stack_buffer(b->root);
  b->slot               = json_get_uint64(head, "slot");
  bytes_t        root   = json_get_bytes(head, "block", &buffer);
  request_t*     req    = (request_t*) calloc(1, sizeof(request_t));
  proofer_ctx_t* ctx    = (proofer_ctx_t*) calloc(1, sizeof(proofer_ctx_t));
  req->client           = NULL;
  req->cb               = handle_new_head_cb;
  req->ctx              = ctx;
  ctx->proof            = bytes(b, sizeof(beacon_head_t));
  handle_new_head_cb(req);
}

static void c4_handle_finalized_checkpoint_cb(request_t* req) {
  proofer_ctx_t* ctx = (proofer_ctx_t*) req->ctx;

  switch (c4_eth_update_finality(ctx)) {
    case C4_SUCCESS: {
      proofer_request_free(req);
      return;
    }
    case C4_ERROR: {
      log_error("Error fetching sigblock and parent: %s", ctx->state.error);
      proofer_request_free(req);
      return;
    }
    case C4_PENDING:
      if (c4_state_get_pending_request(&ctx->state)) // there are pending requests, let's take care of them first
        c4_start_curl_requests(req);
      else {
        log_error("Error fetching sigblock and parent: %s", ctx->state.error);
        proofer_request_free(req);
      }
  }
}

void c4_handle_finalized_checkpoint(json_t checkpoint) {
  request_t* req = (request_t*) calloc(1, sizeof(request_t));
  req->cb        = c4_handle_finalized_checkpoint_cb;
  req->ctx       = calloc(1, sizeof(proofer_ctx_t));
  req->cb(req);
}
