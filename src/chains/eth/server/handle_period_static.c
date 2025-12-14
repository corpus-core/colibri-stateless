/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */
#include "eth_conf.h"
#include "logger.h"
#include "server.h"
#include "util/bytes.h"
#include "uv_util.h"
#include <stdlib.h>
#include <string.h>
#include <uv.h>

typedef struct {
  client_t* client;
  char*     path;
  char*     content_type;
} period_static_ctx_t;

static void c4_handle_period_static_read_cb(void* user_data, file_data_t* files, int num_files) {
  period_static_ctx_t* ctx    = (period_static_ctx_t*) user_data;
  client_t*            client = ctx->client;

  if (files[0].error) {
    if (strstr(files[0].error, "no such file")) {
      c4_write_error_response(client, 404, "Not Found");
    }
    else {
      log_warn("period_static: read failed for %s: %s", files[0].path, files[0].error);
      c4_write_error_response(client, 500, "Internal Server Error");
    }
  }
  else {
    c4_http_respond(client, 200, ctx->content_type, files[0].data);
  }

  // Cleanup
  c4_file_data_array_free(files, num_files, 1); // Free data too
  safe_free(ctx->path);
  safe_free(ctx);
  // Note: we don't free client, server handles that after response
}

bool c4_handle_period_static(client_t* client) {
  if (strncmp(client->request.path, "/period_store", 13) != 0) return false;

  // Security check: prevent directory traversal
  if (strstr(client->request.path, "..")) {
    c4_write_error_response(client, 403, "Forbidden");
    return true;
  }

  if (!eth_config.period_store) {
    c4_write_error_response(client, 503, "Period store not configured");
    return true;
  }

  const char* subpath   = client->request.path + 14;
  char*       full_path = bprintf(NULL, "%s/%s", eth_config.period_store, subpath);

  // Check for directory traversal / valid path characters if needed
  // ...

  // Determine content type
  char* content_type = "application/octet-stream";
  if (strstr(full_path, ".json")) content_type = "application/json";

  // Prepare context
  period_static_ctx_t* ctx = (period_static_ctx_t*) safe_calloc(1, sizeof(period_static_ctx_t));
  ctx->client              = client;
  ctx->path                = full_path;
  ctx->content_type        = content_type;

  // Prepare file data request
  file_data_t* files = (file_data_t*) safe_calloc(1, sizeof(file_data_t));
  files[0].path      = strdup(full_path); // uv_util doesn't take ownership of path string itself usually, but we need to keep it alive or pass a copy.
                                          // Actually c4_read_files_uv copies the struct content but expects path to be valid.
                                          // Let's look at uv_util.c if we had it. Assuming it makes copies or we need to manage lifetime.
                                          // Based on `c4_file_data_array_free`, it frees the path. So we should strdup it.

  // Use c4_read_files_uv
  int rc = c4_read_files_uv(ctx, c4_handle_period_static_read_cb, files, 1);
  if (rc < 0) {
    log_error("period_static: failed to schedule read");
    c4_write_error_response(client, 500, "Internal Server Error");
    c4_file_data_array_free(files, 1, 0);
    safe_free(ctx->path);
    safe_free(ctx);
    return true;
  }

  // files array is copied/managed by uv_util usually?
  // Checking typical usage: usually the caller allocates it, passes it, and then frees it after the call returns (if the util copies it).
  // Or the util takes ownership.
  // Looking at period_store.c:
  // file_data_t* files = calloc...
  // c4_write_files_uv(...)
  // safe_free(files) -> util made its own copy.

  safe_free(files);

  return true;
}
