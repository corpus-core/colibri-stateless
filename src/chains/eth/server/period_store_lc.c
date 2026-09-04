#include "beacon.h"
#include "beacon_types.h"
#include "eth_conf.h"
#include "lcu_gloas.h"
#include "logger.h"
#include "period_store.h"
#include "prover.h"
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
    log_warn("period_store: writing " C4_PS_LCU_SSZ " for period %l failed: %s", ctx->period, files[0].error);
  }
  else {
    log_info("period_store: wrote " C4_PS_LCU_SSZ " for period %l", ctx->period);
  }
  // free file meta (we didn't transfer data ownership)
  c4_file_data_array_free(files, 1, 0);
  c4_request_free(ctx->req);
  safe_free(ctx);
}

// `c4_ps_build_lcu` (self-build fallback) is defined later in this file and
// declared in `period_store.h`.

static void fetch_lcu_cb(client_t* client, void* data, data_request_t* r) {
  (void) client;
  uint64_t period            = data ? *((uint64_t*) data) : 0;
  bool     is_bootstrap_path = r->url && strstr(r->url, "bootstrap") != NULL;
  safe_free(data);
  if (!r->response.data && !r->error) r->error = strdup("unknown error!");
  if (r->error) {
    log_warn("period_store: LCU fetch for period %l failed: %s", period, r->error);
    c4_request_free(r);
    // Only the light_client/updates path can be self-built; bootstrap has a
    // separate precompute path via head_update.c and is served from cache.
    if (!is_bootstrap_path) c4_ps_build_lcu(period);
    return;
  }
  // Beacon-node returned a body that is too short to contain even the wire
  // prefix -> treat as missing and trigger the self-build fallback for LCUs.
  if (!is_bootstrap_path && r->response.len < UPDATE_PREFIX_SIZE) {
    log_warn("period_store: LCU fetch for period %l returned short response (%d bytes)", period, r->response.len);
    c4_request_free(r);
    c4_ps_build_lcu(period);
    return;
  }
  // prepare async write of lcb.ssz or lcu.ssz
  char* dir  = c4_ps_ensure_period_dir(period);
  char* path = bprintf(NULL, strstr(r->url, "bootstrap") ? "%s/" C4_PS_LCB_SSZ : "%s/" C4_PS_LCU_SSZ, dir);
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
  char* path = bprintf(NULL, "%s/" C4_PS_LCU_SSZ, dir);
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
        log_debug("period_store: " C4_PS_LCU_SSZ " missing for period %l (%s)", ctx->start_period + i, files[i].error);
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
    files[i].path   = bprintf(NULL, "%s/" C4_PS_LCU_SSZ, dir);
    files[i].offset = 0;
    files[i].limit  = 0;
    safe_free(dir);
  }
  int rc = c4_read_files_uv(ctx, lcu_assemble_read_cb, files, (int) count);
  if (rc < 0) {
    // Scheduling failed: clean up and fail fast to avoid leaks
    c4_file_data_array_free(files, (int) count, 0);
    char* err = strdup("failed to schedule " C4_PS_LCU_SSZ " reads");
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

  // now fetch the light client bootstrap
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

// ---------------------------------------------------------------------------
// Self-build fallback: build a Gloas LightClientUpdate locally when the
// beacon node cannot serve one for `period`, wrap it in the Beacon-API
// wire format (12B prefix + LCU SSZ) and persist it to {period}/lcu.ssz so
// the existing consumers (handle_lcu, historic_proof fetch_updates_data,
// period_store_zk_prover) can read it back unchanged.
// ---------------------------------------------------------------------------

typedef struct {
  uint64_t period;
} ps_build_lcu_ctx_t;

static void ps_build_lcu_write_done_cb(void* user_data, file_data_t* files, int num_files) {
  (void) num_files;
  ps_build_lcu_ctx_t* wctx = (ps_build_lcu_ctx_t*) user_data;
  uint64_t            period = wctx ? wctx->period : 0;
  if (files && files[0].error) {
    log_warn("period_store: writing self-built " C4_PS_LCU_SSZ " for period %l failed: %s", period, files[0].error);
  }
  else {
    log_info("period_store: wrote self-built " C4_PS_LCU_SSZ " for period %l", period);
  }
  // The file body is owned by the caller (a heap buffer transferred into the
  // write); free it here now that the write has completed.
  if (files && files[0].data.data) safe_free(files[0].data.data);
  c4_file_data_array_free(files, 1, 0);
  safe_free(wctx);
}

// Async callback that drives `c4_create_gloas_lcu` to completion. `ctx->proof`
// carries the target period as an 8-byte little-endian uint64. Best-effort:
// any error is logged and swallowed (the client can still fall back to a
// live beacon call via `c4_get_light_client_updates`).
static void ps_build_lcu_cb(request_t* req) {
  if (c4_check_retry_request(req)) return;
  prover_ctx_t* ctx    = (prover_ctx_t*) req->ctx;
  uint64_t      period = 0;
  if (ctx->proof.data && ctx->proof.len == sizeof(uint64_t))
    period = uint64_from_le(ctx->proof.data);

  bytes_t     lcu_ssz = NULL_BYTES;
  c4_status_t status  = c4_create_gloas_lcu(ctx, period, &lcu_ssz);

  switch (status) {
    case C4_SUCCESS: {
      // Wrap into the Beacon-API `light_client/updates` wire format so the
      // existing consumers can parse it without any special-case.
      const chain_spec_t* chain = c4_eth_get_chain_spec(ctx->chain_id);
      if (!chain || !chain->fork_version_func) {
        // Defense-in-depth: every registered chain sets `fork_version_func`,
        // but a future entry might forget. Fail loudly instead of NULL-deref.
        log_warn("period_store: LCU self-build for period %l: chain spec missing fork_version_func", period);
        safe_free(lcu_ssz.data);
        c4_prover_free(ctx);
        safe_free(req);
        return;
      }
      uint8_t fork_version[4] = {0};
      chain->fork_version_func(ctx->chain_id, C4_FORK_GLOAS, fork_version);
      bytes_t wire = c4_gloas_lcu_wrap_beacon_response(lcu_ssz, fork_version);
      safe_free(lcu_ssz.data);
      if (!wire.data) {
        log_warn("period_store: LCU self-build wrapping failed for period %l", period);
        c4_prover_free(ctx);
        safe_free(req);
        return;
      }

      char* dir  = c4_ps_ensure_period_dir(period);
      char* path = bprintf(NULL, "%s/" C4_PS_LCU_SSZ, dir);
      safe_free(dir);

      file_data_t files[1] = {0};
      files[0].path        = path;
      files[0].offset      = 0;
      files[0].limit       = wire.len;
      files[0].data        = wire; // ownership transferred to write callback

      ps_build_lcu_ctx_t* wctx = (ps_build_lcu_ctx_t*) safe_calloc(1, sizeof(ps_build_lcu_ctx_t));
      wctx->period             = period;
      int rc                   = c4_write_files_uv(wctx, ps_build_lcu_write_done_cb, files, 1, O_WRONLY | O_CREAT | O_TRUNC, 0666);
      if (rc < 0) {
        log_warn("period_store: scheduling self-built LCU write failed for period %l", period);
        safe_free(wire.data);
        c4_file_data_array_free(files, 1, 0);
        safe_free(wctx);
      }
      c4_prover_free(ctx);
      safe_free(req);
      return;
    }
    case C4_ERROR:
      // Post fork/period gate, any error here is a real Lodestar/beacon
      // issue. Swallowed: client falls back to a live beacon fetch via the
      // regular `c4_get_light_client_updates` path.
      log_warn("period_store: LCU self-build for period %l failed: %s",
               period, ctx->state.error ? ctx->state.error : "(unknown)");
      c4_prover_free(ctx);
      safe_free(req);
      return;
    case C4_PENDING:
      if (c4_state_get_pending_request(&ctx->state)) {
        c4_start_curl_requests(req, &ctx->state);
        return;
      }
      log_warn("period_store: LCU self-build for period %l stalled without pending requests: %s",
               period, ctx->state.error ? ctx->state.error : "(unknown)");
      c4_prover_free(ctx);
      safe_free(req);
      return;
  }
}

void c4_ps_build_lcu(uint64_t period) {
  if (graceful_shutdown_in_progress) return;
  if (!eth_config.period_store) return;
  // The self-build path uses Lodestar's unofficial CompactMultiProof
  // endpoint (see `c4_create_state_proof` / `c4_create_gloas_lcu`). Do NOT
  // attempt it when the operator did not opt into Lodestar compatibility --
  // it would just churn round-trips against a beacon node that will 404.
  if (!(http_server.prover_flags & C4_PROVER_FLAG_LODESTAR)) return;
  // Refuse to double-build if the file already exists (e.g. a previous
  // self-build succeeded and a new fetch fails on a network flake).
  if (c4_ps_file_exists(period, C4_PS_LCU_SSZ)) return;

  // Beacon API servers are required for the state proofs behind the
  // self-build path. Without them the whole exercise is pointless.
  server_list_t* sl = c4_get_server_list(C4_DATA_TYPE_BEACON_API);
  if (!sl || sl->count == 0) return;

  // Fork-gate up front. The chain-spec fork lookup is a cheap array probe;
  // failing here avoids allocating an async request that will die later in
  // the orchestrator anyway.
  const chain_spec_t* chain = c4_eth_get_chain_spec(http_server.chain_id);
  if (!chain) return;
  uint64_t  slot_in_period = slot_for_period(period, chain);
  uint64_t  epoch          = epoch_for_slot(slot_in_period, chain);
  fork_id_t fork           = c4_chain_fork_id(http_server.chain_id, epoch);
  if (fork != C4_FORK_GLOAS) return;

  request_t*    req = (request_t*) safe_calloc(1, sizeof(request_t));
  prover_ctx_t* ctx = (prover_ctx_t*) safe_calloc(1, sizeof(prover_ctx_t));
  ctx->chain_id     = http_server.chain_id;
  ctx->client_type  = BEACON_CLIENT_EVENT_SERVER;
  ctx->flags        = http_server.prover_flags;

  // Stash the target period in `ctx->proof` (proof-as-scratchpad, same
  // pattern as `c4_precompute_finalized_bootstrap_cb`). `c4_prover_free`
  // reclaims this buffer as part of the ctx tear-down. Encoding is LE so
  // it round-trips through `uint64_from_le` in the callback.
  uint8_t* period_buf = (uint8_t*) safe_calloc(1, sizeof(uint64_t));
  uint64_to_le(period_buf, period);
  ctx->proof = bytes(period_buf, sizeof(uint64_t));

  req->client = NULL;
  req->ctx    = ctx;
  req->cb     = ps_build_lcu_cb;
  req->cb(req);
}
