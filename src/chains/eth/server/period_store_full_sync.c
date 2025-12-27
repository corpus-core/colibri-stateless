/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
#include "eth_conf.h"
#include "logger.h"
#include "period_store.h"
#include "server.h"
#include "uv_util.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <uv.h>

// Reuse the query parser from handle_lcu.c
uint64_t c4_get_query(char* query, char* param);

typedef struct {
  uint64_t period;
  char*    filename; // owned
  uint32_t length;
  bool     force_full; // force full download (no offset) and truncate
} sync_file_t;

typedef struct {
  bool         in_progress;
  bool         last_full_period_initialized;
  uint64_t     last_full_period;
  uint64_t     start_period;
  uint32_t     file_count;
  sync_file_t* files;
  uint32_t     current_index;
} full_sync_ctx_t;

static full_sync_ctx_t g_full_sync        = {0};
static client_t        g_full_sync_client = {0};

typedef struct {
  data_request_t*  req;
  full_sync_ctx_t* ctx;
  uint64_t         period;
} write_ctx_t;

static bool is_numeric_str(const char* s) {
  if (!s || !*s) return false;
  for (const char* p = s; *p; p++) {
    if (*p < '0' || *p > '9') return false;
  }
  return true;
}

static int cmp_u64(const void* a, const void* b) {
  uint64_t aa = *(const uint64_t*) a;
  uint64_t bb = *(const uint64_t*) b;
  if (aa < bb) return -1;
  if (aa > bb) return 1;
  return 0;
}

static void free_sync_files(sync_file_t* files, uint32_t count) {
  if (!files) return;
  for (uint32_t i = 0; i < count; i++) safe_free(files[i].filename);
  safe_free(files);
}

static bool c4_ps_period_is_complete(uint64_t period) {
  return c4_ps_file_exists(period, "blocks_root.bin") && c4_ps_file_exists(period, "zk_proof_g16.bin");
}

static void c4_ps_full_sync_unmark_complete_period(full_sync_ctx_t* ctx, uint64_t period) {
  if (!ctx) return;
  if (period < ctx->start_period) return;
  if (!c4_ps_period_is_complete(period)) return;

  char* blocks_root = bprintf(NULL, "%s/%l/blocks_root.bin", eth_config.period_store, period);
  // Pragmatic: if we had an error while syncing a period, remove marker files so the next sync retries cleanly.
  remove(blocks_root);
  safe_free(blocks_root);
}

static uint64_t determine_last_full_period(void) {
  // Find highest numeric period directory, then walk backwards until we find a fully completed period.
  uv_fs_t req = {0};
  int     rc  = uv_fs_scandir(uv_default_loop(), &req, eth_config.period_store, 0, NULL);
  if (rc < 0) {
    C4_UV_LOG_ERR_NEG("period_store full_sync: uv_fs_scandir", rc);
    uv_fs_req_cleanup(&req);
    return 0;
  }

  uint64_t*   periods     = (uint64_t*) safe_calloc((size_t) rc + 1, sizeof(uint64_t));
  size_t      periods_len = 0;
  uv_dirent_t ent;
  while (uv_fs_scandir_next(&req, &ent) != UV_EOF) {
    if (ent.type != UV_DIRENT_DIR) continue;
    if (!is_numeric_str(ent.name)) continue;
    periods[periods_len++] = (uint64_t) strtoull(ent.name, NULL, 10);
  }
  uv_fs_req_cleanup(&req);

  if (periods_len == 0) {
    safe_free(periods);
    return 0;
  }

  qsort(periods, periods_len, sizeof(uint64_t), cmp_u64);

  for (size_t i = periods_len; i > 0; i--) {
    uint64_t p = periods[i - 1];
    if (c4_ps_period_is_complete(p)) {
      safe_free(periods);
      return p;
    }
  }

  safe_free(periods);
  return 0;
}

static uint64_t local_file_size(const char* path) {
  struct stat st;
  if (stat(path, &st) != 0) return 0;
  if (st.st_size < 0) return 0;
  return (uint64_t) st.st_size;
}

static void full_sync_download_next(void);

static void full_sync_write_done_cb(void* user_data, file_data_t* files, int num_files) {
  (void) num_files;
  write_ctx_t*    wctx = (write_ctx_t*) user_data;
  data_request_t* req  = wctx ? wctx->req : NULL;
  if (files && files[0].error) {
    log_warn("period_store: full_sync write failed: %s", files[0].error);
    if (wctx) c4_ps_full_sync_unmark_complete_period(wctx->ctx, wctx->period);
  }
  c4_file_data_array_free(files, 1, 0);

  // free request/response
  if (req) c4_request_free(req);

  safe_free(wctx);

  full_sync_download_next();
}

static void full_sync_download_cb(client_t* client, void* user_data, data_request_t* r) {
  (void) client;
  full_sync_ctx_t* ctx = (full_sync_ctx_t*) user_data;
  if (!ctx || ctx != &g_full_sync) {
    c4_request_free(r);
    g_full_sync.in_progress = false;
    return;
  }

  if (!r->response.data && !r->error) r->error = strdup("unknown error");
  if (r->error) {
    log_warn("period_store: full_sync download failed: %s", r->error);
    if (ctx->current_index > 0) {
      uint32_t idx = ctx->current_index - 1;
      if (idx < ctx->file_count) c4_ps_full_sync_unmark_complete_period(ctx, ctx->files[idx].period);
    }
    c4_request_free(r);
    full_sync_download_next();
    return;
  }

  // Use the current file task (already advanced index before request)
  uint32_t idx = ctx->current_index - 1;
  if (idx >= ctx->file_count) {
    c4_request_free(r);
    full_sync_download_next();
    return;
  }

  sync_file_t* f        = &ctx->files[idx];
  char*        dir      = c4_ps_ensure_period_dir(f->period);
  char*        out_path = bprintf(NULL, "%s/%s", dir, f->filename);
  safe_free(dir);

  // Determine if we append or truncate based on requested offset
  // We encode offset in the URL query; extract from URL to avoid storing extra state.
  // If offset was used, we opened without truncation and write at that offset.
  uint64_t offset = 0;
  char*    q      = strchr(r->url, '?');
  if (q && strstr(q + 1, "offset=")) offset = c4_get_query(q + 1, "offset");

  file_data_t files[1] = {0};
  files[0].path        = out_path;
  files[0].offset      = (size_t) offset;
  files[0].limit       = r->response.len;
  files[0].data        = r->response;

  int flags = O_WRONLY | O_CREAT;
  if (offset == 0) flags |= O_TRUNC;

  // Keep request alive until write finishes (pass write_ctx_t as user_data)
  write_ctx_t* wctx = (write_ctx_t*) safe_calloc(1, sizeof(write_ctx_t));
  wctx->req         = r;
  wctx->ctx         = ctx;
  wctx->period      = f->period;

  int wrc = c4_write_files_uv(wctx, full_sync_write_done_cb, files, 1, flags, 0666);
  if (wrc < 0) {
    log_warn("period_store: full_sync scheduling write failed");
    c4_ps_full_sync_unmark_complete_period(ctx, f->period);
    c4_file_data_array_free(files, 1, 0);
    safe_free(wctx);
    c4_request_free(r);
    full_sync_download_next();
    return;
  }
}

static void full_sync_download_next(void) {
  full_sync_ctx_t* ctx = &g_full_sync;

  while (ctx->current_index < ctx->file_count) {
    sync_file_t* f = &ctx->files[ctx->current_index++];

    // Decide local action
    char* dir        = c4_ps_ensure_period_dir(f->period);
    char* local_path = bprintf(NULL, "%s/%s", dir, f->filename);
    safe_free(dir);

    uint64_t local_sz = local_file_size(local_path);
    safe_free(local_path);

    // Force full fetch for blocks.ssz and headers.ssz when blocks_root.bin exists for that period.
    bool force_full = f->force_full;

    if (!force_full && local_sz == f->length)
      continue; // up to date

    uint64_t offset = 0;
    if (!force_full && local_sz < f->length)
      offset = local_sz;

    // Build master URL: <master>/period_store/<period>/<filename>[?offset=...]
    data_request_t* req = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
    req->method         = C4_DATA_METHOD_GET;
    req->chain_id       = http_server.chain_id;
    req->type           = C4_DATA_TYPE_REST_API;
    req->encoding       = C4_DATA_ENCODING_SSZ;

    const char* base  = eth_config.period_master_url;
    bool        slash = base[strlen(base) - 1] == '/';
    if (offset > 0)
      req->url = bprintf(NULL, "%s%speriod_store/%l/%s?offset=%l",
                         base,
                         slash ? "" : "/",
                         f->period,
                         f->filename,
                         offset);
    else
      req->url = bprintf(NULL, "%s%speriod_store/%l/%s",
                         base,
                         slash ? "" : "/",
                         f->period,
                         f->filename);

    g_full_sync_client.being_closed = false;
    c4_add_request(&g_full_sync_client, req, ctx, full_sync_download_cb);
    return; // async in flight
  }

  // Done. Advance last_full_period forward while fully complete.
  while (c4_ps_period_is_complete(ctx->last_full_period + 1))
    ctx->last_full_period++;

  log_info("period_store: full_sync completed (last_full_period=%l, files=%d)", ctx->last_full_period, ctx->file_count);
  free_sync_files(ctx->files, ctx->file_count);
  ctx->files         = NULL;
  ctx->file_count    = 0;
  ctx->current_index = 0;
  ctx->in_progress   = false;
}

static void full_sync_manifest_cb(client_t* client, void* user_data, data_request_t* r) {
  (void) client;
  full_sync_ctx_t* ctx = (full_sync_ctx_t*) user_data;
  if (!ctx || ctx != &g_full_sync) return;

  if (!r->response.data && !r->error) r->error = strdup("unknown error");
  if (r->error) {
    log_warn("period_store: full_sync manifest fetch failed: %s", r->error);
    c4_request_free(r);
    ctx->in_progress = false;
    return;
  }

  ssz_ob_t   files_ob = ssz_ob(C4_PERIOD_STORE_MANIFEST_LIST, r->response);
  c4_state_t state    = {0};
  if (!ssz_is_valid(files_ob, true, &state)) {
    log_warn("period_store: full_sync manifest invalid: %s", state.error ? state.error : "unknown");
    safe_free(state.error);
    c4_request_free(r);
    ctx->in_progress = false;
    return;
  }

  uint32_t num = (uint32_t) ssz_len(files_ob);
  if (num > 10000) num = 10000;

  // First pass: collect periods that have blocks_root.bin
  uint64_t* root_periods = (uint64_t*) safe_calloc(num + 1, sizeof(uint64_t));
  uint32_t  root_n       = 0;
  for (uint32_t i = 0; i < num; i++) {
    ssz_ob_t file   = ssz_at(files_ob, (int) i);
    uint64_t period = ssz_get_uint64(&file, "period");
    ssz_ob_t nameob = ssz_get(&file, "filename");
    char*    name   = (char*) nameob.bytes.data;
    if (name && strcmp(name, "blocks_root.bin") == 0) root_periods[root_n++] = period;
  }
  if (root_n > 1) qsort(root_periods, root_n, sizeof(uint64_t), cmp_u64);

  ctx->files      = (sync_file_t*) safe_calloc(num, sizeof(sync_file_t));
  ctx->file_count = 0;

  for (uint32_t i = 0; i < num; i++) {
    ssz_ob_t file   = ssz_at(files_ob, (int) i);
    uint64_t period = ssz_get_uint64(&file, "period");
    ssz_ob_t nameob = ssz_get(&file, "filename");
    char*    name   = (char*) nameob.bytes.data;
    uint32_t len    = ssz_get_uint32(&file, "length");
    if (!name || !*name) continue;

    bool force_full = false;
    if ((strcmp(name, "blocks.ssz") == 0 || strcmp(name, "headers.ssz") == 0) && root_n > 0) {
      // If blocks_root.bin exists for this period, force full refresh of blocks/headers.
      uint64_t key = period;
      force_full   = bsearch(&key, root_periods, root_n, sizeof(uint64_t), cmp_u64) != NULL;
    }

    sync_file_t* out = &ctx->files[ctx->file_count++];
    out->period      = period;
    out->filename    = strdup(name);
    out->length      = len;
    out->force_full  = force_full;
  }

  safe_free(root_periods);

  // Cleanup manifest response
  ctx->current_index = 0;
  c4_request_free(r);
  full_sync_download_next();
}

void c4_ps_full_sync_on_checkpoint(uint64_t finalized_period) {
  (void) finalized_period;
  if (!eth_config.period_full_sync || !eth_config.period_master_url || !eth_config.period_store) return;
  if (g_full_sync.in_progress) return;

  if (!g_full_sync.last_full_period_initialized) {
    g_full_sync.last_full_period             = determine_last_full_period();
    g_full_sync.last_full_period_initialized = true;
    log_info("period_store: full_sync initialized last_full_period=%l", g_full_sync.last_full_period);
  }

  g_full_sync.in_progress         = true;
  g_full_sync.start_period        = g_full_sync.last_full_period + 1;
  g_full_sync_client.being_closed = false;

  const char* base  = eth_config.period_master_url;
  bool        slash = base[strlen(base) - 1] == '/';

  // Fetch manifest: /period_store?manifest=1&start=<start>
  data_request_t* req = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
  req->method         = C4_DATA_METHOD_GET;
  req->chain_id       = http_server.chain_id;
  req->type           = C4_DATA_TYPE_REST_API;
  req->encoding       = C4_DATA_ENCODING_SSZ;
  req->url            = bprintf(NULL, "%s%speriod_store?manifest=1&start=%l", base, slash ? "" : "/", g_full_sync.start_period);

  c4_add_request(&g_full_sync_client, req, &g_full_sync, full_sync_manifest_cb);
}
