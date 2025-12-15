#include "eth_conf.h"
#include "logger.h"
#include "period_store.h"
#include "server.h"
#include "ssz.h"
#include "sync_committee.h"
#include "uv_util.h"

#define THROW_PERIOD_ERROR(r, fmt, ...) \
  {                                     \
    log_warn(fmt, ##__VA_ARGS__);       \
    c4_request_free(r);                 \
    return;                             \
  }
// ---- Assemble multiple LCU from cache (fetch missing) ----
typedef struct {
  void*           user_data;
  light_client_cb cb;
  uint64_t        start_period;
  uint32_t        count;
  buffer_t        out;
  uint32_t        missing_count;
  uint32_t*       missing_indices; // indices into [0..count)
  uint32_t        missing_pos;     // next to fetch
} lcu_assemble_ctx_t;

// ---- LightClientUpdate (LCU) fetch/write ----
typedef struct {
  uint64_t        period;
  data_request_t* req; // kept alive until write finishes
} lcu_write_ctx_t;

typedef struct {
  lcu_assemble_ctx_t* agg;
  uint64_t            period;
} lcu_fetch_ctx_t;

static void lcu_fetch_next(lcu_assemble_ctx_t* ctx);

static void lcu_write_done_cb(void* user_data, file_data_t* files, int num_files) {
  (void) num_files;
  lcu_write_ctx_t* ctx = (lcu_write_ctx_t*) user_data;
  if (files && files[0].error) {
    log_warn("period_store: writing lcu.ssz for period %l failed: %s", ctx->period, files[0].error);
  }
  else {
    log_info("period_store: wrote lcu.ssz for period %l", ctx->period);
  }
  // free file meta (we didn't transfer data ownership)
  c4_file_data_array_free(files, 1, 0);
  c4_request_free(ctx->req);
  safe_free(ctx);
}

static void fetch_lcu_cb(client_t* client, void* data, data_request_t* r) {
  (void) client;
  uint64_t period = data ? *((uint64_t*) data) : 0;
  safe_free(data);
  if (!r->response.data && !r->error) r->error = strdup("unknown error!");
  if (r->error) {
    log_warn("period_store: LCU fetch for period %l failed: %s", period, r->error);
    c4_request_free(r);
    return;
  }
  // prepare async write of lcu.ssz
  char* dir  = c4_ps_ensure_period_dir(period);
  char* path = bprintf(NULL, strstr(r->url, "bootstrap") ? "%s/lcb.ssz" : "%s/lcu.ssz", dir);
  safe_free(dir);
  file_data_t files[1] = {0};
  files[0].path        = path;
  files[0].offset      = 0;
  files[0].limit       = r->response.len; // write all bytes
  files[0].data        = r->response;
  // keep request alive until write completes
  lcu_write_ctx_t* wctx = (lcu_write_ctx_t*) safe_calloc(1, sizeof(lcu_write_ctx_t));
  wctx->period          = period;
  wctx->req             = r;
  int rc                = c4_write_files_uv(wctx, lcu_write_done_cb, files, 1, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (rc < 0) {
    log_warn("period_store: scheduling LCU write failed for period %l", period);
    c4_file_data_array_free(files, 1, 0);
    c4_request_free(r);
    safe_free(wctx);
  }
}

void c4_ps_schedule_fetch_lcu(uint64_t period) {
  if (graceful_shutdown_in_progress) return;

  // Skip if no Beacon API servers configured
  server_list_t* sl = c4_get_server_list(C4_DATA_TYPE_BEACON_API);
  if (!sl || sl->count == 0) return;
  static client_t lcu_client = {0};
  lcu_client.being_closed    = false;
  data_request_t* req        = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
  req->url                   = bprintf(NULL, "eth/v1/beacon/light_client/updates?start_period=%l&count=1", period);
  req->method                = C4_DATA_METHOD_GET;
  req->chain_id              = http_server.chain_id;
  req->type                  = C4_DATA_TYPE_BEACON_API;
  req->encoding              = C4_DATA_ENCODING_SSZ;
  uint64_t* pdata            = (uint64_t*) safe_calloc(1, sizeof(uint64_t));
  *pdata                     = period;
  c4_add_request(&lcu_client, req, pdata, fetch_lcu_cb);
}

static void lcu_assemble_fetch_cb(client_t* client, void* data, data_request_t* r) {
  (void) client;
  lcu_fetch_ctx_t*    fctx = (lcu_fetch_ctx_t*) data;
  lcu_assemble_ctx_t* a    = fctx->agg;
  uint64_t            p    = fctx->period;
  safe_free(fctx);
  if (!r->response.data && !r->error) r->error = strdup("unknown error!");
  if (r->error) {
    char* err = bprintf(NULL, "LCU fetch failed for period %l: %s", p, r->error);
    c4_request_free(r);
    bytes_t result = NULL_BYTES;
    a->cb(a->user_data, result, err);
    buffer_free(&a->out);
    safe_free(a->missing_indices);
    safe_free(a);
    return;
  }
  // append to output
  buffer_append(&a->out, bytes((uint8_t*) r->response.data, (uint32_t) r->response.len));
  // persist to cache (reuse write helper; keeps r alive until write finishes)
  char* dir  = c4_ps_ensure_period_dir(p);
  char* path = bprintf(NULL, "%s/lcu.ssz", dir);
  safe_free(dir);
  file_data_t files[1]  = {0};
  files[0].path         = path;
  files[0].offset       = 0;
  files[0].limit        = r->response.len;
  files[0].data         = r->response;
  lcu_write_ctx_t* wctx = (lcu_write_ctx_t*) safe_calloc(1, sizeof(lcu_write_ctx_t));
  wctx->period          = p;
  wctx->req             = r; // will be freed in lcu_write_done_cb
  int rc                = c4_write_files_uv(wctx, lcu_write_done_cb, files, 1, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (rc < 0) {
    log_warn("period_store: scheduling LCU write failed for period %l", p);
    c4_file_data_array_free(files, 1, 0);
    c4_request_free(r);
    safe_free(wctx);
  }
  // next
  lcu_fetch_next(a);
}

static void lcu_fetch_next(lcu_assemble_ctx_t* ctx) {
  if (ctx->missing_pos >= ctx->missing_count) {
    // done: deliver result
    bytes_t result = ctx->out.data; // transfer ownership
    ctx->out       = (buffer_t) {0};
    ctx->cb(ctx->user_data, result, NULL);
    safe_free(ctx->missing_indices);
    safe_free(ctx);
    return;
  }
  uint32_t        rel_idx    = ctx->missing_indices[ctx->missing_pos++];
  uint64_t        period     = ctx->start_period + rel_idx;
  static client_t agg_client = {0};
  agg_client.being_closed    = false;
  data_request_t* req        = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
  req->url                   = bprintf(NULL, "eth/v1/beacon/light_client/updates?start_period=%l&count=1", period);
  req->method                = C4_DATA_METHOD_GET;
  req->chain_id              = http_server.chain_id;
  req->type                  = C4_DATA_TYPE_BEACON_API;
  req->encoding              = C4_DATA_ENCODING_SSZ;
  lcu_fetch_ctx_t* fctx      = (lcu_fetch_ctx_t*) safe_calloc(1, sizeof(lcu_fetch_ctx_t));
  fctx->agg                  = ctx;
  fctx->period               = period;
  c4_add_request(&agg_client, req, fctx, lcu_assemble_fetch_cb);
}

static void lcu_assemble_read_cb(void* user_data, file_data_t* files, int num_files) {
  lcu_assemble_ctx_t* ctx = (lcu_assemble_ctx_t*) user_data;
  // concatenate available; collect missing
  ctx->missing_indices = (uint32_t*) safe_calloc((size_t) ctx->count, sizeof(uint32_t));
  for (uint32_t i = 0; i < (uint32_t) num_files; i++) {
    if (files[i].error || files[i].data.len == 0) {
      ctx->missing_indices[ctx->missing_count++] = i;
      if (files[i].error)
        log_debug("period_store: lcu.ssz missing for period %l (%s)", ctx->start_period + i, files[i].error);
    }
    else {
      buffer_append(&ctx->out, files[i].data);
    }
  }
  // free read buffers; we've copied what we need
  c4_file_data_array_free(files, num_files, 1);
  if (ctx->missing_count == 0) {
    // deliver immediately
    bytes_t result = ctx->out.data; // transfer ownership to caller
    ctx->out       = (buffer_t) {0};
    ctx->cb(ctx->user_data, result, NULL);
    safe_free(ctx->missing_indices);
    safe_free(ctx);
    return;
  }
  // fetch missing sequentially to keep order
  lcu_fetch_next(ctx);
}

void c4_get_light_client_updates(void* user_data, uint64_t period, uint32_t count, light_client_cb cb) {
  lcu_assemble_ctx_t* ctx = (lcu_assemble_ctx_t*) safe_calloc(1, sizeof(lcu_assemble_ctx_t));
  ctx->user_data          = user_data;
  ctx->cb                 = cb;
  ctx->start_period       = period;
  ctx->count              = count;
  ctx->out                = (buffer_t) {0};
  if (!eth_config.period_store) {
    // Fallback: kein Cache → hole alle Perioden direkt und liefere concatenated Ergebnis (nicht persistieren)
    ctx->missing_count   = count;
    ctx->missing_pos     = 0;
    ctx->missing_indices = (uint32_t*) safe_calloc(count, sizeof(uint32_t));
    for (uint32_t i = 0; i < count; i++) ctx->missing_indices[i] = i;
    lcu_fetch_next(ctx);
    return;
  }
  // prepare reads
  file_data_t* files = (file_data_t*) safe_calloc(count, sizeof(file_data_t));
  for (uint32_t i = 0; i < count; i++) {
    char* dir       = c4_ps_ensure_period_dir(period + i);
    files[i].path   = bprintf(NULL, "%s/lcu.ssz", dir);
    files[i].offset = 0;
    files[i].limit  = 0;
    safe_free(dir);
  }
  int rc = c4_read_files_uv(ctx, lcu_assemble_read_cb, files, (int) count);
  if (rc < 0) {
    // Scheduling failed: clean up and fail fast to avoid leaks
    c4_file_data_array_free(files, (int) count, 0);
    char* err = strdup("failed to schedule lcu.ssz reads");
    cb(user_data, NULL_BYTES, err);
    buffer_free(&ctx->out);
    safe_free(ctx);
  }
  else {
    // c4_read_files_uv made its own heap copy of the array; free our temporary array container
    // IMPORTANT: do NOT free files[i].path here; ownership stays with the copy and will be freed in the callback
    safe_free(files);
  }
}

void c4_ps_fetch_lcb_for_checkpoint(bytes32_t checkpoint, uint64_t period) {

  // now fetch the lcb.ssz
  static client_t lcu_client = {0};
  data_request_t* req        = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
  req->url                   = bprintf(NULL, "eth/v1/beacon/light_client/bootstrap/0x%x", bytes(checkpoint, 32));
  req->method                = C4_DATA_METHOD_GET;
  req->chain_id              = http_server.chain_id;
  req->type                  = C4_DATA_TYPE_BEACON_API;
  req->encoding              = C4_DATA_ENCODING_SSZ;
  uint64_t* pdata            = (uint64_t*) safe_calloc(1, sizeof(uint64_t));
  *pdata                     = period;
  c4_add_request(&lcu_client, req, pdata, fetch_lcu_cb);
}
static void fetch_lcb_cb(client_t* client, void* data, data_request_t* r) {
  (void) client;
  uint64_t period = data ? *((uint64_t*) data) : 0;
  safe_free(data);

  if (!r->response.data && !r->error) r->error = strdup("unknown error!");
  if (r->error) THROW_PERIOD_ERROR(r, "period_store: LCU fetch for period %l failed: %s", period, r->error);
  if (r->response.len < UPDATE_PREFIX_SIZE) THROW_PERIOD_ERROR(r, "period_store: LCU fetch for period %l failed: response too short", period);

  ssz_ob_t update = {.bytes = bytes(r->response.data + UPDATE_PREFIX_SIZE, uint64_from_le(r->response.data) - SSZ_OFFSET_SIZE), .def = NULL};
  if (update.bytes.data + update.bytes.len > r->response.data + r->response.len) THROW_PERIOD_ERROR(r, "period_store: LCU fetch for period %l failed: response too short", period);
  update.def = eth_get_light_client_update(c4_eth_get_fork_for_lcu(http_server.chain_id, update.bytes));
  if (!update.def) THROW_PERIOD_ERROR(r, "period_store: LCU fetch for period %l failed: invalid update data len=%l", period, update.bytes.len);
  ssz_ob_t finalized         = ssz_get(&update, "finalizedHeader");
  ssz_ob_t header            = ssz_get(&finalized, "beacon");
  uint64_t checkpoint_period = ssz_get_uint64(&header, "slot") >> 13;
  if (checkpoint_period != period) THROW_PERIOD_ERROR(r, "period_store: LCU fetch for period %l failed: checkpoint period mismatch", period);
  bytes32_t checkpoint = {0};
  ssz_hash_tree_root(header, checkpoint);
  c4_request_free(r);
  c4_ps_fetch_lcb_for_checkpoint(checkpoint, period);
}

void c4_ps_schedule_fetch_lcb(uint64_t period) {
  if (graceful_shutdown_in_progress) return;

  // Skip if no Beacon API servers configured
  server_list_t* sl = c4_get_server_list(C4_DATA_TYPE_BEACON_API);
  if (!sl || sl->count == 0) return;

  static client_t lcu_client = {0};

  data_request_t* req = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
  req->url            = bprintf(NULL, "eth/v1/beacon/light_client/updates?start_period=%l&count=1", period);
  req->method         = C4_DATA_METHOD_GET;
  req->chain_id       = http_server.chain_id;
  req->type           = C4_DATA_TYPE_BEACON_API;
  req->encoding       = C4_DATA_ENCODING_SSZ;
  uint64_t* pdata     = (uint64_t*) safe_calloc(1, sizeof(uint64_t));
  *pdata              = period;
  c4_add_request(&lcu_client, req, pdata, fetch_lcb_cb);
}
