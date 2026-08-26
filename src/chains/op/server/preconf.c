#include "handler.h"
#include "logger.h"
#include "op_conf.h"
#include "uv_util.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

// ---- Remote fallback: fetch preconf from master kona-bridge via blocking curl ----

typedef struct {
  uv_work_t         work;
  single_request_t* req;
  char*             master_url; // Full URL to fetch
  char*             cache_path; // Local path to cache the result (NULL = no caching)
  uint8_t*          data;
  uint32_t          data_len;
} preconf_remote_fetch_t;

static size_t preconf_curl_write(void* ptr, size_t size, size_t nmemb, void* ud) {
  preconf_remote_fetch_t* ctx  = (preconf_remote_fetch_t*) ud;
  uint32_t                real = (uint32_t) (size * nmemb);
  uint8_t*                buf  = realloc(ctx->data, ctx->data_len + real);
  if (!buf) return 0;
  memcpy(buf + ctx->data_len, ptr, real);
  ctx->data = buf;
  ctx->data_len += real;
  return real;
}

// Runs in libuv thread pool — blocking curl call is safe here
static void preconf_remote_worker(uv_work_t* work) {
  preconf_remote_fetch_t* ctx = (preconf_remote_fetch_t*) work->data;

  CURL* curl = curl_easy_init();
  if (!curl) return;

  curl_easy_setopt(curl, CURLOPT_URL, ctx->master_url);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, preconf_curl_write);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, ctx);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

  long     http_code = 0;
  CURLcode res       = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK || http_code != 200 || ctx->data_len == 0) {
    free(ctx->data);
    ctx->data     = NULL;
    ctx->data_len = 0;
    return;
  }

  // Cache to local filesystem for block-specific requests (not latest/pre_latest symlinks)
  if (ctx->cache_path) {
    FILE* f = fopen(ctx->cache_path, "wb");
    if (f) {
      fwrite(ctx->data, 1, ctx->data_len, f);
      fclose(f);
    }
  }
}

// Runs in main event loop after worker completes
static void preconf_remote_complete(uv_work_t* work, int status) {
  preconf_remote_fetch_t* ctx = (preconf_remote_fetch_t*) work->data;

  if (status == 0 && ctx->data && ctx->data_len > 0) {
    ctx->req->req->response = (bytes_t) {.len = ctx->data_len, .data = ctx->data};
    ctx->data               = NULL; // ownership transferred
  }
  else {
    free(ctx->data);
    ctx->req->req->error = strdup("Preconf not available locally or on master bridge");
  }

  c4_internal_call_finish(ctx->req);
  free(ctx->master_url);
  free(ctx->cache_path);
  free(ctx);
}

// ---- Main preconf handler ----

static void c4_handle_preconf_cb(void* user_data, file_data_t* files, int num_files) {
  single_request_t* r = (single_request_t*) user_data;

  if (files[0].error) {
    // Try master bridge fallback when file is missing and MASTER_KONA_BRIDGE_URL is set
    if (op_config.master_kona_bridge_url) {
      const char* preconf_path = r->req->url; // e.g. "preconf/latest"
      char*       master_url   = bprintf(NULL, "%s/%s", op_config.master_kona_bridge_url, preconf_path);

      // Only cache when requesting a specific block number (not latest/pre_latest symlinks)
      char* cache_path = NULL;
      if (files[0].path &&
          strncmp(preconf_path, "preconf/latest", 14) != 0 &&
          strncmp(preconf_path, "preconf/pre_latest", 18) != 0) {
        cache_path = strdup(files[0].path);
      }

      c4_file_data_array_free(files, num_files, 0);

      preconf_remote_fetch_t* ctx = calloc(1, sizeof(*ctx));
      ctx->work.data              = ctx;
      ctx->req                    = r;
      ctx->master_url             = master_url;
      ctx->cache_path             = cache_path;

      uv_queue_work(uv_default_loop(), &ctx->work,
                    preconf_remote_worker, preconf_remote_complete);
      return;
    }

    r->req->error = strdup(files[0].error);
  }
  else {
    r->req->response   = files[0].data;
    files[0].data.data = NULL;
  }

  c4_file_data_array_free(files, num_files, 0);
  c4_internal_call_finish(r);
}

bool c4_handle_preconf(single_request_t* r) {
  const char* path = "preconf/";
  if (strncmp(r->req->url, path, strlen(path))) return false;
  if (!op_config.preconf_storage_dir) {
    r->req->error = strdup("preconf_storage_dir not configured!");
    c4_internal_call_finish(r);
    return true;
  }

  char* block_identifier = r->req->url + strlen(path);
  char* file_name        = NULL;
  if (strcmp(block_identifier, "latest") == 0 || strcmp(block_identifier, "pre_latest") == 0)
    file_name = bprintf(NULL, "%s/%s.raw", op_config.preconf_storage_dir, block_identifier);
  else if (strncmp(block_identifier, "0x", 2) == 0 || strncmp(block_identifier, "0X", 2) == 0)
    file_name = bprintf(NULL, "%s/block_%l_%l.raw", op_config.preconf_storage_dir, r->req->chain_id, strtoull(block_identifier + 2, NULL, 16));
  else {
    r->req->error = bprintf(NULL, "Invalid block identifier: %s", block_identifier);
    c4_internal_call_finish(r);
    return true;
  }

  file_data_t f = {.path = file_name};
  c4_read_files_uv(r, c4_handle_preconf_cb, &f, 1);
  return true;
}
