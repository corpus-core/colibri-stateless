#include "beacon_types.h"
#include "eth_clients.h"
#include "eth_conf.h"
#include "logger.h"
#include "period_store.h"
#include "server.h"
#include "ssz.h"
#include "sync_committee.h"
#include "uv_util.h"
#define SLOTS_PER_PERIOD 8192u
// SSZ definition for blocks.ssz: 8192 block roots (bytes32 each)
static const ssz_def_t BLOCKS             = SSZ_VECTOR("blocks", ssz_bytes32, SLOTS_PER_PERIOD);
static uint64_t        latest_hist_period = UINT64_MAX;

// Latest verified blocks_root marker (blocks_root.bin) for monitoring.
static uint64_t g_blocks_root_last_verified_period = 0;
static uint64_t g_blocks_root_last_verified_ts_s   = 0;

typedef struct {
  uint64_t        period;
  data_request_t* req; // kept alive until write finishes
} historical_root_write_ctx_t;

typedef struct {
  uint64_t hist_period;
  uint64_t current_period;
  uint64_t last_period;
  uint32_t offset_period;
  uint64_t periods_verified;
  uint64_t periods_skipped_verified;
  uint64_t periods_failed;
  json_t   json_doc;
  json_t   data;
  buffer_t json_buf;
} verify_blocks_ctx_t;

static void verify_blocks_schedule_next(verify_blocks_ctx_t* ctx);

uint64_t c4_ps_blocks_root_last_verified_period(void) {
  return g_blocks_root_last_verified_period;
}

uint64_t c4_ps_blocks_root_last_verified_timestamp_seconds(void) {
  return g_blocks_root_last_verified_ts_s;
}

void c4_ps_blocks_root_init_from_store(void) {
  if (eth_config.period_master_url) return;
  if (!eth_config.period_store) return;

  uint64_t first = 0, last = 0;
  if (!c4_ps_period_index_get_contiguous_from(0, &first, &last)) return;

  for (uint64_t p = last;; p--) {
    if (c4_ps_file_exists(p, "blocks_root.bin")) {
      char*       path = bprintf(NULL, "%s/%l/blocks_root.bin", eth_config.period_store, p);
      struct stat st;
      if (path && stat(path, &st) == 0) {
        g_blocks_root_last_verified_period = p;
        g_blocks_root_last_verified_ts_s   = (uint64_t) st.st_mtime;
        safe_free(path);
        return;
      }
      safe_free(path);
    }
    if (p == first) break;
  }
}

static void verify_hist_json_read_cb(void* user_data, file_data_t* files, int num_files) {
  verify_blocks_ctx_t* ctx = (verify_blocks_ctx_t*) user_data;
  uint64_t             hp  = ctx ? ctx->hist_period : 0;

  if (!files || num_files < 1) {
    log_warn("period_store: verify blocks_root: failed to read historical_root.json for hist_period %l (invalid file count %d)", hp, num_files);
    if (files) c4_file_data_array_free(files, num_files, 1);
    buffer_free(&ctx->json_buf);
    safe_free(ctx);
    return;
  }

  if (files[0].error || !files[0].data.data || files[0].data.len == 0) {
    log_warn("period_store: verify blocks_root: cannot read historical_root.json for hist_period %l (%s)", hp, files[0].error ? files[0].error : "empty file");
    c4_file_data_array_free(files, num_files, 1);
    buffer_free(&ctx->json_buf);
    safe_free(ctx);
    return;
  }

  buffer_append(&ctx->json_buf, files[0].data);
  buffer_add_chars(&ctx->json_buf, ""); // ensure NULL-termination without changing length
  ctx->json_doc = json_parse((char*) ctx->json_buf.data.data);

  ctx->data = json_get(ctx->json_doc, "data");
  if (ctx->data.type != JSON_TYPE_OBJECT) {
    log_warn("period_store: verify blocks_root: historical_root.json for hist_period %l has no 'data' object", hp);
    buffer_free(&ctx->json_buf);
    c4_file_data_array_free(files, num_files, 1);
    safe_free(ctx);
    return;
  }

  c4_file_data_array_free(files, num_files, 1);

  // Start verifying periods one by one using the parsed JSON.
  verify_blocks_schedule_next(ctx);
}

static void verify_blocks_for_period_read_cb(void* user_data, file_data_t* files, int num_files) {
  verify_blocks_ctx_t* ctx = (verify_blocks_ctx_t*) user_data;
  if (!ctx) {
    if (files) c4_file_data_array_free(files, num_files, 1);
    return;
  }

  uint64_t p = ctx->current_period - 1; // period for which we scheduled this read

  if (!files || num_files < 1) {
    log_warn("period_store: verify blocks_root for period %l failed: invalid file count (%d)", p, num_files);
    if (files) c4_file_data_array_free(files, num_files, 1);
    ctx->periods_failed++;
    verify_blocks_schedule_next(ctx);
    return;
  }

  // blocks.ssz
  if (files[0].error || !files[0].data.data || files[0].data.len == 0) {
    log_warn("period_store: verify blocks_root for period %l skipped: cannot read blocks.ssz (%s)", p, files[0].error ? files[0].error : "empty file");
    c4_file_data_array_free(files, num_files, 1);
    verify_blocks_schedule_next(ctx);
    return;
  }

  // Build SSZ object for blocks.ssz (zero-pad if file is shorter)
  uint8_t blocks_buf[32 * SLOTS_PER_PERIOD] = {0};
  size_t  blocks_len                        = sizeof(blocks_buf);
  size_t  copy_len                          = files[0].data.len < blocks_len ? files[0].data.len : blocks_len;
  memcpy(blocks_buf, files[0].data.data, copy_len);
  ssz_ob_t blocks_ob = {.bytes = bytes(blocks_buf, (uint32_t) blocks_len), .def = &BLOCKS};
  if (!ssz_is_type(&blocks_ob, &BLOCKS)) {
    log_warn("period_store: verify blocks_root for period %l failed: blocks.ssz has unexpected SSZ type/length", p);
    c4_file_data_array_free(files, num_files, 1);
    ctx->periods_failed++;
    verify_blocks_schedule_next(ctx);
    return;
  }
  bytes32_t blocks_root = {0};
  ssz_hash_tree_root(blocks_ob, blocks_root);

  if (p < ctx->offset_period) {
    // Historical summaries not defined yet for this period
    c4_file_data_array_free(files, num_files, 1);
    verify_blocks_schedule_next(ctx);
    return;
  }

  uint32_t summary_idx = (uint32_t) (p - ctx->offset_period);
  json_t   summaries   = json_get(ctx->data, "historical_summaries");
  json_t   entry       = json_at(summaries, summary_idx);
  if (entry.type == JSON_TYPE_NOT_FOUND) {
    log_warn("period_store: verify blocks_root for period %l failed: missing historical_summaries[%d]", p, summary_idx);
    c4_file_data_array_free(files, num_files, 1);
    ctx->periods_failed++;
    verify_blocks_schedule_next(ctx);
    return;
  }

  bytes32_t block_summary_root = {0};
  json_to_bytes(json_get(entry, "block_summary_root"), bytes(block_summary_root, 32));

  if (memcmp(block_summary_root, blocks_root, 32) != 0) {
    log_warn("period_store: blocks_root mismatch for period %l", p);
    c4_file_data_array_free(files, num_files, 1);
    ctx->periods_failed++;
    verify_blocks_schedule_next(ctx);
    return;
  }

  // Write marker file with blocks_root so we do not verify again
  char* dir  = c4_ps_ensure_period_dir(p);
  char* path = bprintf(NULL, "%s/blocks_root.bin", dir);
  safe_free(dir);

  FILE* f = fopen(path, "wb");
  if (!f) {
    log_warn("period_store: could not write blocks_root.bin for period %l: %s", p, strerror(errno));
  }
  else {
    size_t written = fwrite(blocks_root, 1, sizeof(blocks_root), f);
    if (written != sizeof(blocks_root))
      log_warn("period_store: short write for blocks_root.bin for period %l (wrote %d bytes)", p, (int) written);
    fclose(f);
    log_info("period_store: verified blocks_root for period %l", p);
    ctx->periods_verified++;

    // Update monitoring state (mtime in seconds).
    struct stat st;
    if (stat(path, &st) == 0) {
      g_blocks_root_last_verified_period = p;
      g_blocks_root_last_verified_ts_s   = (uint64_t) st.st_mtime;
    }
  }
  safe_free(path);

  c4_file_data_array_free(files, num_files, 1);

  verify_blocks_schedule_next(ctx);
}

static void verify_blocks_schedule_next(verify_blocks_ctx_t* ctx) {
  while (ctx->current_period <= ctx->last_period) {
    uint64_t p = ctx->current_period++;

    if (c4_ps_file_exists(p, "blocks_root.bin")) {
      ctx->periods_skipped_verified++;
      continue;
    }
    if (!c4_ps_file_exists(p, "blocks.ssz")) continue;

    char*       dir  = c4_ps_ensure_period_dir(p);
    file_data_t f[1] = {0};
    f[0].path        = bprintf(NULL, "%s/blocks.ssz", dir);
    f[0].offset      = 0;
    f[0].limit       = 32 * SLOTS_PER_PERIOD;
    safe_free(dir);

    int rc = c4_read_files_uv(ctx, verify_blocks_for_period_read_cb, f, 1);
    if (rc < 0) {
      log_warn("period_store: scheduling verify blocks_root read failed for period %l (hist_period %l)", p, ctx->hist_period);
      c4_file_data_array_free(f, 1, 0);
      continue;
    }
    // One async read in flight; the callback will continue with the next period.
    return;
  }

  // Done with all periods
  buffer_free(&ctx->json_buf);
  log_info("period_store: blocks_root verification for hist_period %l finished (verified=%l, skipped=%l, failed=%l)",
           ctx->hist_period,
           ctx->periods_verified,
           ctx->periods_skipped_verified,
           ctx->periods_failed);
  safe_free(ctx);
}

void c4_ps_schedule_verify_all_blocks_for_historical() {
  if (graceful_shutdown_in_progress) return;
  if (!eth_config.period_store) return;

  // After backfill finished, try to verify blocks_root for all periods using the latest historical_summaries.
  // If we do not yet know which period provides historical_summaries, try to infer it from the current head period.
  if (eth_config.period_store && latest_hist_period == UINT64_MAX) {
    const chain_spec_t* chain = c4_eth_get_chain_spec((chain_id_t) http_server.chain_id);
    if (chain && chain->fork_epochs) {
      uint64_t head_period = period_for_slot(c4_ps_backfill_start_slot(), chain);
      if (c4_ps_file_exists(head_period, "historical_root.json")) {
        latest_hist_period = head_period;
      }
    }
  }
  if (latest_hist_period == UINT64_MAX) return;

  const chain_spec_t* chain = c4_eth_get_chain_spec((chain_id_t) http_server.chain_id);
  if (!chain || !chain->fork_epochs) return;

  uint64_t offset_period = (uint64_t) (chain->fork_epochs[C4_FORK_BELLATRIX] >> chain->epochs_per_period_bits);
  if (latest_hist_period <= offset_period) return;

  // Do not verify the current (head) period itself; historical_summaries only cover completed periods.
  uint64_t last_period = latest_hist_period - 1;
  if (last_period < offset_period) return;

  verify_blocks_ctx_t* ctx = (verify_blocks_ctx_t*) safe_calloc(1, sizeof(verify_blocks_ctx_t));
  ctx->hist_period         = latest_hist_period;
  ctx->current_period      = offset_period;
  ctx->last_period         = last_period;
  ctx->offset_period       = (uint32_t) offset_period;
  ctx->json_buf            = (buffer_t) {0};

  char*       dir  = c4_ps_ensure_period_dir(latest_hist_period);
  file_data_t f[1] = {0};
  f[0].path        = bprintf(NULL, "%s/historical_root.json", dir);
  f[0].offset      = 0;
  f[0].limit       = 0; // read all
  safe_free(dir);

  int rc = c4_read_files_uv(ctx, verify_hist_json_read_cb, f, 1);
  if (rc < 0) {
    log_warn("period_store: scheduling historical_root.json read failed for hist_period %l", latest_hist_period);
    c4_file_data_array_free(f, 1, 0);
    buffer_free(&ctx->json_buf);
    safe_free(ctx);
  }
}

static void historical_root_write_done_cb(void* user_data, file_data_t* files, int num_files) {
  (void) num_files;
  historical_root_write_ctx_t* ctx = (historical_root_write_ctx_t*) user_data;
  if (files && files[0].error) {
    log_warn("period_store: writing historical_root.json for period %l failed: %s", ctx->period, files[0].error);
  }
  else {
    log_info("period_store: wrote historical_root.json for period %l", ctx->period);
    // Remember latest period for which we have historical_summaries and trigger verification.
    latest_hist_period = ctx->period;
    if (c4_ps_backfill_done())
      c4_ps_schedule_verify_all_blocks_for_historical();
  }
  c4_file_data_array_free(files, num_files, 0);
  c4_request_free(ctx->req);
  safe_free(ctx);
}

static void fetch_historical_root_cb(client_t* client, void* data, data_request_t* r) {
  (void) client;
  uint64_t period = data ? *((uint64_t*) data) : 0;
  safe_free(data);
  if (!r->response.data && !r->error) r->error = strdup("unknown error!");
  if (r->error) {
    log_warn("period_store: historical summaries fetch for period %l failed: %s", period, r->error);
    c4_request_free(r);
    return;
  }
  char* dir  = c4_ps_ensure_period_dir(period);
  char* path = bprintf(NULL, "%s/historical_root.json", dir);
  safe_free(dir);
  file_data_t files[1]              = {0};
  files[0].path                     = path;
  files[0].offset                   = 0;
  files[0].limit                    = r->response.len;
  files[0].data                     = r->response;
  historical_root_write_ctx_t* wctx = (historical_root_write_ctx_t*) safe_calloc(1, sizeof(historical_root_write_ctx_t));
  wctx->period                      = period;
  wctx->req                         = r;
  int rc                            = c4_write_files_uv(wctx, historical_root_write_done_cb, files, 1, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (rc < 0) {
    log_warn("period_store: scheduling historical_root.json write failed for period %l", period);
    c4_file_data_array_free(files, 1, 0);
    c4_request_free(r);
    safe_free(wctx);
  }
}

void c4_ps_schedule_fetch_historical_root(uint64_t period) {
  if (graceful_shutdown_in_progress) return;
  server_list_t* sl = c4_get_server_list(C4_DATA_TYPE_BEACON_API);
  if (!sl || sl->count == 0) return;
  static client_t historical_client = {0};
  data_request_t* req               = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
  req->url                          = strdup("eth/v1/lodestar/states/head/historical_summaries");
  req->method                       = C4_DATA_METHOD_GET;
  req->chain_id                     = http_server.chain_id;
  req->type                         = C4_DATA_TYPE_BEACON_API;
  req->encoding                     = C4_DATA_ENCODING_JSON;
  req->preferred_client_type        = BEACON_CLIENT_LODESTAR;
  uint64_t* pdata                   = (uint64_t*) safe_calloc(1, sizeof(uint64_t));
  *pdata                            = period;
  c4_add_request(&historical_client, req, pdata, fetch_historical_root_cb);
}
