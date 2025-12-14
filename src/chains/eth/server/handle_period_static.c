/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */
#include "eth_conf.h"
#include "logger.h"
#include "period_store.h"
#include "server.h"
#include "util/bytes.h"
#include "uv_util.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <uv.h>

#define MAX_FILE_SIZE (1024 * 1024 * 5) // 5MB

const ssz_def_t C4_PERIOD_STORE_MANIFEST_ITEM_DEF[] = {
    SSZ_UINT64("period"),
    SSZ_STRING("filename", 32),
    SSZ_UINT32("length"),
};

const ssz_def_t C4_PERIOD_STORE_MANIFEST_ITEM_CONTAINER = SSZ_CONTAINER("files", C4_PERIOD_STORE_MANIFEST_ITEM_DEF);
const ssz_def_t C4_PERIOD_STORE_MANIFEST_LIST           = SSZ_LIST("files", C4_PERIOD_STORE_MANIFEST_ITEM_CONTAINER, 10000);

typedef struct {
  client_t* client;
  char*     path;
  char*     content_type;
} period_static_ctx_t;

// Reuse the query parser from handle_lcu.c
uint64_t c4_get_query(char* query, char* param);

// Small in-memory cache for the most common manifest request (typically start=current period).
// This avoids repeated directory scans when multiple slaves hit the master at the same checkpoint.
static bytes_t        g_manifest_cache_bytes   = {0};
static uint64_t       g_manifest_cache_start   = UINT64_MAX;
static uint64_t       g_manifest_cache_ts_ms   = 0;
static const int      g_manifest_cache_ttl_ms  = 30000;       // 30s
static const uint32_t g_manifest_cache_max_len = 1024 * 1024; // 1 MiB

static void c4_handle_period_static_read_cb(void* user_data, file_data_t* files, int num_files) {
  period_static_ctx_t* ctx    = (period_static_ctx_t*) user_data;
  client_t*            client = ctx->client;

  if (files[0].error) {
    if (strstr(files[0].error, "no such file"))
      c4_write_error_response(client, 404, "Not Found");
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
}

static void c4_handle_period_static_manifest(client_t* client, uint64_t start_period) {
  // Serve cached manifest if available.
  uint64_t now_ms = current_ms();
  if (g_manifest_cache_bytes.data && g_manifest_cache_start == start_period && (now_ms - g_manifest_cache_ts_ms) < (uint64_t) g_manifest_cache_ttl_ms) {
    c4_http_respond(client, 200, "application/octet-stream", g_manifest_cache_bytes);
    return;
  }

  // Build SSZ list
  ssz_builder_t list_builder = ssz_builder_for_def(&C4_PERIOD_STORE_MANIFEST_LIST);
  ssz_builder_t file_builder = ssz_builder_for_def(&C4_PERIOD_STORE_MANIFEST_ITEM_CONTAINER);
  uint32_t      file_count   = 0;
  uint64_t      first        = 0;
  uint64_t      last         = 0;

  if (!c4_ps_period_index_get_contiguous_from(start_period, &first, &last)) {
    if (c4_ps_period_index_has_gaps()) {
      log_error("period_store: refusing manifest request because period directories contain gaps");
      c4_write_error_response(client, 500, "Period store integrity error");
    }
    else {
      log_error("period_store: refusing manifest request because period directories are not contiguous");
      c4_write_error_response(client, 500, "invalid period");
    }
    return;
  }

  for (uint64_t p = first; p <= last && file_count < 10000; p++) {
    char* dir_path = bprintf(NULL, "%s/%l", eth_config.period_store, p);

    uv_fs_t dir_req = {0};
    int     dir_rc  = uv_fs_scandir(uv_default_loop(), &dir_req, dir_path, 0, NULL);
    if (dir_rc < 0) {
      C4_UV_LOG_ERR_NEG("period_store: uv_fs_scandir", dir_rc);
      uv_fs_req_cleanup(&dir_req);
      ssz_builder_free(&list_builder);
      safe_free(dir_path);
      c4_write_error_response(client, 500, "Server error invalid period_store");
      return;
    }

    uv_dirent_t f_ent;
    while (file_count < 10000 && uv_fs_scandir_next(&dir_req, &f_ent) != UV_EOF) {
      if (f_ent.type != UV_DIRENT_FILE) continue;

      // stat to get size
      char*    fpath = bprintf(NULL, "%s/%s", dir_path, f_ent.name);
      uv_fs_t  streq = {0};
      uint32_t len   = 0;
      int      src   = uv_fs_stat(uv_default_loop(), &streq, fpath, NULL);
      if (src >= 0) {
        uint64_t sz = (uint64_t) streq.statbuf.st_size;
        if (sz > MAX_FILE_SIZE) {
          log_error("period_store: manifest file too large: %s (%" PRIu64 " bytes)", fpath, sz);
          uv_fs_req_cleanup(&streq);
          safe_free(fpath);
          uv_fs_req_cleanup(&dir_req);
          ssz_builder_free(&list_builder);
          safe_free(dir_path);
          c4_write_error_response(client, 500, "Period store file too large");
          return;
        }
        len = (uint32_t) sz;
      }
      uv_fs_req_cleanup(&streq);

      // encode item
      ssz_add_uint64(&file_builder, p);
      ssz_add_bytes(&file_builder, "filename", bytes((uint8_t*) f_ent.name, (uint32_t) strlen(f_ent.name) + 1)); // null-terminated filename only (period is separate field)
      ssz_add_uint32(&file_builder, len);

      // SSZ list offsets require the final element count. We patch offsets after the loop.
      ssz_add_dynamic_list_builders(&list_builder, 0, file_builder);
      file_count++;

      safe_free(fpath);
    }

    uv_fs_req_cleanup(&dir_req);
    safe_free(dir_path);

    if (p == UINT64_MAX) break;
  }

  // Patch SSZ offsets: list_builder.fixed contains uint32 offsets, measured from start of list body.
  // Since we added elements with num_elements=0, offsets are missing the fixed-size offset-table length (file_count * 4).
  for (uint32_t i = 0; i < file_count; i++) {
    uint32_t off = uint32_from_le(list_builder.fixed.data.data + i * 4);
    uint32_to_le(list_builder.fixed.data.data + i * 4, off + file_count * 4);
  }

  ssz_ob_t list_ob = ssz_builder_to_bytes(&list_builder);

  // Update cache (best-effort) for small responses only.
  if (list_ob.bytes.data && list_ob.bytes.len > 0 && list_ob.bytes.len <= g_manifest_cache_max_len) {
    safe_free(g_manifest_cache_bytes.data);
    g_manifest_cache_bytes = bytes_dup(list_ob.bytes);
    g_manifest_cache_start = start_period;
    g_manifest_cache_ts_ms = now_ms;
  }

  c4_http_respond(client, 200, "application/octet-stream", list_ob.bytes);
  safe_free(list_ob.bytes.data);
}

bool c4_handle_period_static(client_t* client) {
  if (strncmp(client->request.path, "/period_store", 13) != 0) return false;
  // Ensure the prefix match is exact: next char must be '/', '?' or end.
  char next = client->request.path[13];
  if (next && next != '/' && next != '?') return false;
  const char* after_prefix = client->request.path + 13;
  const char* query_mark   = strchr(after_prefix, '?');
  const char* path_end     = query_mark ? query_mark : client->request.path + strlen(client->request.path);
  uint64_t    offset       = 0;

  // Security check: prevent directory traversal
  if (strstr(client->request.path, "..")) {
    c4_write_error_response(client, 403, "Forbidden");
    return true;
  }

  if (!eth_config.period_store) {
    c4_write_error_response(client, 503, "Period store not configured");
    return true;
  }

  // Handle manifest request: /period_store?manifest=1&start=...
  if (query_mark) {
    char* query = (char*) (query_mark + 1);
    offset      = c4_get_query(query, "offset");
    if (c4_get_query(query, "manifest") == 1) {
      c4_handle_period_static_manifest(client, c4_get_query(query, "start"));
      return true;
    }
  }

  // File request: /period_store/<period>/<file>[?offset=...]
  const char* rel = (after_prefix < path_end && *after_prefix == '/') ? after_prefix + 1 : NULL;
  if (!rel || rel >= path_end) {
    c4_write_error_response(client, 400, "Missing period_store path");
    return true;
  }

  char* full_path    = bprintf(NULL, "%s/%r", eth_config.period_store, bytes(rel, (path_end - rel)));
  char* content_type = strstr(full_path, ".json") ? "application/json" : "application/octet-stream";
  // Prepare context
  period_static_ctx_t* ctx = (period_static_ctx_t*) safe_calloc(1, sizeof(period_static_ctx_t));
  ctx->client              = client;
  ctx->path                = full_path;
  ctx->content_type        = content_type;
  file_data_t files        = {
             .path   = strdup(full_path), // uv_util doesn't take ownership of path string itself usually, but we need to keep it alive or pass a copy.
             .offset = (size_t) offset,
             .limit  = 0};

  // Use c4_read_files_uv
  int rc = c4_read_files_uv(ctx, c4_handle_period_static_read_cb, &files, 1);
  if (rc < 0) {
    log_error("period_static: failed to schedule read");
    c4_write_error_response(client, 500, "Internal Server Error");
    safe_free(files.path);
    safe_free(ctx->path);
    safe_free(ctx);
    return true;
  }

  return true;
}
