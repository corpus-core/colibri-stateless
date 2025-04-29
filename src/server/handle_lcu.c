#include "logger.h"
#include "server.h"

typedef struct {
  bytes_t*  found;
  uint64_t  start_period;
  uint32_t  count;
  uint32_t  results;
  client_t* client;
  char*     error;
} lcu_ctx_t;

// extracts query parameters as uint64
static uint64_t get_query(char* query, char* param) {
  char* found = strstr(query, param);
  if (!found) return 0;
  found += strlen(param);
  if (*found == '=')
    found++;
  else
    return 0;
  char tmp[20] = {0};
  for (int i = 0; i < sizeof(tmp); i++) {
    if (!found[i] || found[i] == '&') break;
    tmp[i] = found[i];
  }
  return (uint64_t) atoll(tmp);
}

// callback function to handle lcus.
// this ctx is shared for multiple  updates.
static void handle_lcu_result(void* u_ptr, uint64_t period, bytes_t data, char* error) {
  lcu_ctx_t* ctx = u_ptr;
  uint32_t   i   = period - ctx->start_period; // this callback will be called for each period
  ctx->results++;                              // so we keep track to know when to finish
  if (period < ctx->start_period || period >= ctx->start_period + ctx->count) {
    if (!ctx->error) ctx->error = strdup("Invalid period!");
  }
  else if (error) {
    if (!ctx->error) ctx->error = bprintf(NULL, "Error fetching period %l: %s", period, error);
  }
  else
    ctx->found[i] = bytes_dup(data);

  if (ctx->results < ctx->count) return; // not finished yet, still pending updates

  // handle response
  if (ctx->error) {
    char* json = bprintf(NULL, "{\"error\":\"%s\"}", ctx->error);
    c4_http_respond(ctx->client, 500, "application/json", bytes((uint8_t*) json, strlen(json)));
    free(json);
  }
  else {
    buffer_t result = {0};
    for (i = 0; i < ctx->count; i++)
      buffer_append(&result, ctx->found[i]);
    c4_http_respond(ctx->client, 200, "application/octet-stream", result.data);
    buffer_free(&result);
  }

  // cleanup
  safe_free(ctx->error);
  for (i = 0; i < ctx->count; i++) safe_free(ctx->found[i].data);
  safe_free(ctx->found);
  safe_free(ctx);
}

bool c4_handle_lcu(client_t* client) {

  const char* path = "/eth/v1/beacon/light_client/updates?";
  if (strncmp(client->request.path, path, strlen(path)) != 0) return false;
  char*    query = client->request.path + strlen(path);
  uint64_t start = get_query(query, "start_period");
  uint64_t count = get_query(query, "count");

  if (!start || !count) {
    char* error = "{\"error\":\"Invalid arguments\"}";
    c4_http_respond(client, 500, "application/json", bytes((uint8_t*) error, strlen(error)));
    return true;
  }

  lcu_ctx_t* ctx    = safe_calloc(1, sizeof(lcu_ctx_t));
  ctx->client       = client;
  ctx->start_period = start;
  ctx->count        = (uint32_t) count;
  ctx->found        = safe_calloc(count, sizeof(bytes_t));

  for (int i = 0; i < count; i++)
    c4_get_from_store(http_server.chain_id, start + i, STORE_TYPE_LCU, 0, ctx, handle_lcu_result);

  return true;
}
