#include "beacon_types.h"
#include "eth_conf.h"
#include "lcu_gloas.h"
#include "logger.h"
#include "period_store.h"
#include "prover.h"
#include "server.h"
#include "sync_committee.h"
#include "uv_util.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ---- Assemble multiple LCU from cache (fetch missing) ----
typedef struct {
  void*           user_data;
  light_client_cb cb;
  uint64_t        start_period;
  uint32_t        count;
  bytes_t*        parts;           // one owned body per period, NULL until accepted
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
static void lcu_persist_response(uint64_t period, data_request_t* r);

/**
 * Checks that `branch` is a single-leaf Merkle proof of `leaf` under `state_root`.
 *
 * @param branch     Sibling hashes, 32 bytes each, leaf-to-root order.
 * @param leaf       Object whose hash_tree_root is the leaf.
 * @param gindex     Generalized index of that leaf in the beacon state.
 * @param state_root `attestedHeader.beacon.stateRoot` (32 bytes).
 * @return `true` when the proof length matches `gindex` and folds to `state_root`.
 */
static bool lcu_branch_matches_state(bytes_t branch, ssz_ob_t leaf, gindex_t gindex, bytes_t state_root) {
  if (gindex == 0 || ssz_is_error(leaf) || !state_root.data || state_root.len != 32) return false;
  if (!branch.data || branch.len == 0 || (branch.len % 32) != 0 || bytes_all_zero(branch)) return false;
  bytes32_t leaf_root = {0};
  bytes32_t got       = {0};
  ssz_hash_tree_root(leaf, leaf_root);
  if (!ssz_verify_multi_merkle_proof(branch, bytes(leaf_root, 32), &gindex, got)) return false;
  return memcmp(got, state_root.data, 32) == 0;
}

bool c4_ps_lcu_wire_is_cacheable(chain_id_t chain_id, bytes_t wire, uint64_t period) {
  if (!wire.data || wire.len < UPDATE_PREFIX_SIZE) return false;
  uint64_t payload_len = uint64_from_le(wire.data);
  if (payload_len < 4 || payload_len > UINT32_MAX) return false;
  if ((uint64_t) wire.len != 8ull + payload_len) return false;

  const chain_spec_t* spec = c4_eth_get_chain_spec(chain_id);
  if (!spec) return false;

  fork_id_t        fork = c4_eth_fork_from_digest(chain_id, wire.data + 8);
  const ssz_def_t* def  = eth_get_light_client_update(fork);
  if (!def) return false;

  ssz_ob_t update = {.bytes = bytes(wire.data + 12, (uint32_t) payload_len - 4), .def = def};
  if (!ssz_is_valid(update, true, NULL)) return false;

  ssz_ob_t attested        = ssz_get(&update, "attestedHeader");
  ssz_ob_t attested_beacon = ssz_get(&attested, "beacon");
  if (ssz_is_error(attested_beacon)) return false;
  ssz_ob_t state_root    = ssz_get(&attested_beacon, "stateRoot");
  uint64_t attested_slot = ssz_get_uint64(&attested_beacon, "slot");
  if (ssz_is_error(state_root) || state_root.bytes.len != 32) return false;
  if (period_for_slot(attested_slot, spec) != period) return false;

  ssz_ob_t finalized        = ssz_get(&update, "finalizedHeader");
  ssz_ob_t finalized_beacon = ssz_get(&finalized, "beacon");
  if (ssz_is_error(finalized_beacon)) return false;
  // Beacon nodes zero this header until the finalized block is in the same
  // sync-committee period as the attested block. That body is not cacheable.
  if (ssz_get_uint64(&finalized_beacon, "slot") == 0) return false;

  ssz_ob_t finality_branch = ssz_get(&update, "finalityBranch");
  ssz_ob_t next_branch     = ssz_get(&update, "nextSyncCommitteeBranch");
  ssz_ob_t next_committee  = ssz_get(&update, "nextSyncCommittee");
  if (ssz_is_error(finality_branch) || ssz_is_error(next_branch) || ssz_is_error(next_committee)) return false;

  if (!lcu_branch_matches_state(finality_branch.bytes, finalized_beacon, c4_finalized_root_gindex(chain_id, attested_slot), state_root.bytes))
    return false;
  if (!lcu_branch_matches_state(next_branch.bytes, next_committee, c4_next_sync_committee_gindex(chain_id, attested_slot), state_root.bytes))
    return false;
  return true;
}

/**
 * Deletes a period's `lcu.ssz` after it was read and rejected.
 *
 * @param period Sync-committee period of the file.
 */
static void lcu_cache_unlink(uint64_t period) {
  if (!eth_config.period_store) return;
  char* path = bprintf(NULL, "%s/%l/" C4_PS_LCU_SSZ, eth_config.period_store, period);
  if (remove(path) == 0)
    log_warn("period_store: removed " C4_PS_LCU_SSZ " for period %l (not cacheable)", period);
  safe_free(path);
}

/**
 * Frees per-period bodies held by an in-flight assemble.
 *
 * @param ctx Assemble context. `parts` may be NULL.
 */
static void lcu_assemble_free_parts(lcu_assemble_ctx_t* ctx) {
  if (!ctx || !ctx->parts) return;
  for (uint32_t i = 0; i < ctx->count; i++) safe_free(ctx->parts[i].data);
  safe_free(ctx->parts);
  ctx->parts = NULL;
}

/**
 * Delivers `err` and frees the assemble context.
 *
 * @param ctx Assemble context. Freed by this function.
 * @param err Error string. Ownership passes to the callback.
 */
static void lcu_assemble_fail(lcu_assemble_ctx_t* ctx, char* err) {
  ctx->cb(ctx->user_data, NULL_BYTES, err);
  lcu_assemble_free_parts(ctx);
  safe_free(ctx->missing_indices);
  safe_free(ctx);
}

/**
 * Concatenates accepted bodies in period order and delivers them.
 *
 * @param ctx Assemble context. Freed by this function.
 */
static void lcu_assemble_finish(lcu_assemble_ctx_t* ctx) {
  buffer_t out = {0};
  for (uint32_t i = 0; i < ctx->count; i++) {
    if (!ctx->parts || !ctx->parts[i].data) {
      buffer_free(&out);
      char* err = bprintf(NULL, "missing light client update for period %l", ctx->start_period + i);
      lcu_assemble_fail(ctx, err);
      return;
    }
    buffer_append(&out, ctx->parts[i]);
  }
  bytes_t result = out.data;
  ctx->cb(ctx->user_data, result, NULL);
  lcu_assemble_free_parts(ctx);
  safe_free(ctx->missing_indices);
  safe_free(ctx);
}

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
  uint64_t period = data ? *((uint64_t*) data) : 0;
  safe_free(data);
  if (!r->response.data && !r->error) r->error = strdup("unknown error!");
  if (r->error) {
    log_warn("period_store: LCU fetch for period %l failed: %s", period, r->error);
    c4_request_free(r);
    c4_ps_build_lcu(period);
    return;
  }
  // Beacon-node returned a body that is too short to contain even the wire
  // prefix -> treat as missing and trigger the self-build fallback.
  if (r->response.len < UPDATE_PREFIX_SIZE) {
    log_warn("period_store: LCU fetch for period %l returned short response (%d bytes)", period, r->response.len);
    c4_request_free(r);
    c4_ps_build_lcu(period);
    return;
  }
  if (!c4_ps_lcu_wire_is_cacheable(http_server.chain_id, r->response, period)) {
    // A body can be a well-formed sync-committee update whose finalized header
    // is still zero. Do not persist it and do not self-build over it: the next
    // checkpoint retries while the file is absent.
    log_warn("period_store: LCU for period %l is not cacheable (no finality proof); not writing", period);
    c4_request_free(r);
    return;
  }
  lcu_persist_response(period, r);
}

/**
 * Writes one accepted Beacon response to `{period}/lcu.ssz`.
 *
 * @param period Sync-committee period.
 * @param r      Request whose `response` is the wire body. Freed when the write finishes,
 *               or immediately when the period store is disabled or scheduling fails.
 */
static void lcu_persist_response(uint64_t period, data_request_t* r) {
  if (!eth_config.period_store) {
    c4_request_free(r);
    return;
  }
  char* dir  = c4_ps_ensure_period_dir(period);
  char* path = bprintf(NULL, "%s/" C4_PS_LCU_SSZ, dir);
  safe_free(dir);
  file_data_t files[1] = {0};
  files[0].path        = path;
  files[0].offset      = 0;
  files[0].limit       = r->response.len;
  files[0].data        = r->response;
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
    lcu_assemble_fail(a, err);
    return;
  }
  uint32_t rel = p >= a->start_period ? (uint32_t) (p - a->start_period) : a->count;
  if (rel >= a->count || !c4_ps_lcu_wire_is_cacheable(http_server.chain_id, r->response, p)) {
    log_warn("period_store: LCU for period %l is not cacheable (no finality proof)", p);
    c4_request_free(r);
    char* err = bprintf(NULL, "LCU for period %l has no finality", p);
    lcu_assemble_fail(a, err);
    return;
  }
  a->parts[rel] = bytes_dup(r->response);
  lcu_persist_response(p, r);
  lcu_fetch_next(a);
}

static void lcu_fetch_next(lcu_assemble_ctx_t* ctx) {
  // Missing periods are fetched one at a time. Each accepted body is stored by
  // index and the callback receives them concatenated in period order.
  if (ctx->missing_pos >= ctx->missing_count) {
    lcu_assemble_finish(ctx);
    return;
  }
  server_list_t* sl = c4_get_server_list(C4_DATA_TYPE_BEACON_API);
  if (!sl || sl->count == 0) {
    lcu_assemble_fail(ctx, strdup("no beacon API configured for light client updates"));
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
  lcu_assemble_ctx_t* ctx  = (lcu_assemble_ctx_t*) user_data;
  ctx->missing_indices     = (uint32_t*) safe_calloc((size_t) ctx->count, sizeof(uint32_t));
  for (uint32_t i = 0; i < (uint32_t) num_files; i++) {
    uint64_t period = ctx->start_period + i;
    bool     bad    = files[i].error || files[i].data.len == 0 ||
                   !c4_ps_lcu_wire_is_cacheable(http_server.chain_id, files[i].data, period);
    if (bad) {
      if (!files[i].error && files[i].data.len > 0) {
        log_warn("period_store: cached " C4_PS_LCU_SSZ " for period %l is not cacheable; refetching", period);
        lcu_cache_unlink(period);
      }
      else if (files[i].error)
        log_debug("period_store: " C4_PS_LCU_SSZ " missing for period %l (%s)", period, files[i].error);
      ctx->missing_indices[ctx->missing_count++] = i;
    }
    else {
      ctx->parts[i] = bytes_dup(files[i].data);
    }
  }
  c4_file_data_array_free(files, num_files, 1);
  if (ctx->missing_count == 0) {
    lcu_assemble_finish(ctx);
    return;
  }
  lcu_fetch_next(ctx);
}

void c4_get_light_client_updates(void* user_data, uint64_t period, uint32_t count, light_client_cb cb) {
  lcu_assemble_ctx_t* ctx = (lcu_assemble_ctx_t*) safe_calloc(1, sizeof(lcu_assemble_ctx_t));
  ctx->user_data          = user_data;
  ctx->cb                 = cb;
  ctx->start_period       = period;
  ctx->count              = count;
  ctx->parts              = (bytes_t*) safe_calloc(count ? count : 1, sizeof(bytes_t));
  if (!eth_config.period_store) {
    // No cache: fetch every period from the beacon API and deliver the
    // concatenated result. lcu_persist_response does not write in this case.
    ctx->missing_count   = count;
    ctx->missing_pos     = 0;
    ctx->missing_indices = (uint32_t*) safe_calloc(count ? count : 1, sizeof(uint32_t));
    for (uint32_t i = 0; i < count; i++) ctx->missing_indices[i] = i;
    lcu_fetch_next(ctx);
    return;
  }
  file_data_t* files = (file_data_t*) safe_calloc(count ? count : 1, sizeof(file_data_t));
  for (uint32_t i = 0; i < count; i++) {
    char* dir       = c4_ps_ensure_period_dir(period + i);
    files[i].path   = bprintf(NULL, "%s/" C4_PS_LCU_SSZ, dir);
    files[i].offset = 0;
    files[i].limit  = 0;
    safe_free(dir);
  }
  int rc = c4_read_files_uv(ctx, lcu_assemble_read_cb, files, (int) count);
  if (rc < 0) {
    // Scheduling failed: free the request list and fail before anything is read.
    c4_file_data_array_free(files, (int) count, 0);
    lcu_assemble_fail(ctx, strdup("failed to schedule " C4_PS_LCU_SSZ " reads"));
  }
  else {
    // c4_read_files_uv made its own heap copy of the array; free our temporary array container.
    // Do not free files[i].path here; the copy owns those strings.
    safe_free(files);
  }
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
  ps_build_lcu_ctx_t* wctx   = (ps_build_lcu_ctx_t*) user_data;
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
      uint8_t fork_digest[4] = {0};
      if (!c4_eth_compute_fork_digest(ctx->chain_id, C4_FORK_GLOAS, fork_digest)) {
        log_warn("period_store: LCU self-build for period %l: cannot compute Gloas fork digest", period);
        safe_free(lcu_ssz.data);
        c4_prover_free(ctx);
        safe_free(req);
        return;
      }
      bytes_t wire = c4_gloas_lcu_wrap_beacon_response(lcu_ssz, fork_digest);
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
