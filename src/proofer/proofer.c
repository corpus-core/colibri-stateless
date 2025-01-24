#include "proofer.h"
#include <stdlib.h>
#include <string.h>

proofer_ctx_t* c4_proofer_create(char* method, char* params) {
  json_t params_json = json_parse(params);
  if (params_json.type != JSON_TYPE_ARRAY) return NULL;
  char* params_str = malloc(params_json.len + 1);
  memcpy(params_str, params_json.start, params_json.len);
  params_str[params_json.len] = 0;
  params_json.start           = params_str;
  proofer_ctx_t* ctx          = calloc(1, sizeof(proofer_ctx_t));
  ctx->method                 = strdup(method);
  ctx->params                 = params_json;
  return ctx;
}

void c4_proofer_free(proofer_ctx_t* ctx) {
  data_request_t* data_request = ctx->data_requests;
  while (data_request) {
    data_request_t* next = data_request->next;
    if (data_request->url) free(data_request->url);
    if (data_request->error) free(data_request->error);
    if (data_request->payload.data) free(data_request->payload.data);
    if (data_request->response.data) free(data_request->response.data);
    free(data_request);
    data_request = next;
  }

  if (ctx->method) free(ctx->method);
  if (ctx->params.start) free(ctx->params.start);
  if (ctx->error) free(ctx->error);
  if (ctx->proof.data) free(ctx->proof.data);
  free(ctx);
}

void c4_proofer_execute(proofer_ctx_t* ctx) {
}

data_request_t* c4_proofer_get_pending_data_request(proofer_ctx_t* ctx) {
  data_request_t* data_request = ctx->data_requests;
  while (data_request) {
    if (data_request->response.data == NULL && data_request->error == NULL) return data_request;
    data_request = data_request->next;
  }
  return NULL;
}