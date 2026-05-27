/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 *
 * Historic-proof snapshot builder (server, master mode only).
 *
 * For each finalized checkpoint SSE event, the server pre-builds one
 * `zk_proof_checkpoint_${anchor_slot}.ssz` snapshot per period that has a
 * legacy `zk_proof.ssz`. The snapshot embeds an `historic_proof` checkpoint
 * variant (see `ETH_HISTORIC_BLOCK_PROOF`) which:
 *
 *   - anchors against a RECENT epoch-boundary header (currently `finalized`)
 *     that checkpointz instances still have in their ~6h cache window, and
 *   - cryptographically links the period's attested block root to the
 *     `state_root` of that recent header via `historical_summaries`.
 *
 * The prover selects the latest snapshot whose anchor is <= the current
 * finalized slot via `snapshots.idx`. Snapshots older than
 * `finalized_slot - 1800` (~6h) are unlinked.
 *
 * Slave-mode handling lives in `period_store.c` (`snapshots.idx` is invalidated
 * on every checkpoint event so slaves re-fetch from the master).
 */

#include "beacon_types.h"
#include "bytes.h"
#include "eth_clients.h"
#include "eth_conf.h"
#include "historic_proof.h"
#include "json.h"
#include "logger.h"
#include "period_store.h"
#include "server.h"
#include "ssz.h"
#include "uv_util.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SLOTS_PER_PERIOD         8192u
#define HEADER_SSZ_SIZE          112u
// Snapshots older than the checkpointz cache window become unusable as WSP
// anchors. Public checkpointz instances retain ~30 finalized snapshots which
// roughly corresponds to 6 hours, hence 1800 slots at 12s/slot.
#define CHECKPOINTZ_WINDOW_SLOTS 1800u

// SSZ definition for blocks.ssz (must match period_store_historical_roots.c).
static const ssz_def_t BLOCKS_DEF = SSZ_VECTOR("blocks", ssz_bytes32, SLOTS_PER_PERIOD);

typedef struct {
  uint64_t period;
  uint64_t anchor_slot;

  // Phase A: period files
  bytes_t sync_ssz;   // ${period}/sync.ssz
  bytes_t blocks_ssz; // ${period-1}/blocks.ssz

  // Phase B: external requests
  bytes_t header_response;    // eth/v1/beacon/headers/${anchor_slot}
  bytes_t summaries_response; // eth/v1/lodestar/states/${anchor_slot}/historical_summaries
  int     requests_remaining;
  bool    any_failed;
} build_ctx_t;

// =============================================================================
// snapshots.idx helpers (synchronous I/O; idx files are < 1 KiB).
// Format: <uint32 count LE><uint64 slots[count] LE>
// Atomic writes via tmp file + rename.
// =============================================================================

static char* idx_path(uint64_t period) {
  return bprintf(NULL, "%s/%l/snapshots.idx", eth_config.period_store, period);
}

static char* idx_tmp_path(uint64_t period) {
  return bprintf(NULL, "%s/%l/snapshots.idx.tmp", eth_config.period_store, period);
}

// Loads the slot list from the idx file. Returns true on success; out_slots is
// heap-allocated (caller frees) or NULL when count == 0. A missing file is
// treated as success with count = 0 -- callers always get a usable view.
static bool snapshots_idx_load(uint64_t period, uint64_t** out_slots, uint32_t* out_count) {
  *out_slots = NULL;
  *out_count = 0;

  char* path = idx_path(period);
  FILE* f    = fopen(path, "rb");
  safe_free(path);
  if (!f) return true;

  uint8_t header[4] = {0};
  if (fread(header, 1, 4, f) != 4) {
    fclose(f);
    return false;
  }
  uint32_t count = uint32_from_le(header);
  if (count == 0) {
    fclose(f);
    return true;
  }

  uint64_t* slots = (uint64_t*) safe_calloc(count, sizeof(uint64_t));
  uint8_t   buf[8];
  for (uint32_t i = 0; i < count; i++) {
    if (fread(buf, 1, 8, f) != 8) {
      safe_free(slots);
      fclose(f);
      return false;
    }
    slots[i] = uint64_from_le(buf);
  }
  fclose(f);
  *out_slots = slots;
  *out_count = count;
  return true;
}

// Writes the slot list atomically (tmp + rename).
static bool snapshots_idx_save(uint64_t period, uint64_t* slots, uint32_t count) {
  char* tmp_path = idx_tmp_path(period);
  FILE* f        = fopen(tmp_path, "wb");
  if (!f) {
    log_warn("period_store: cannot open %s for write: %s", tmp_path, strerror(errno));
    safe_free(tmp_path);
    return false;
  }

  uint8_t header[4];
  uint32_to_le(header, count);
  if (fwrite(header, 1, 4, f) != 4) {
    fclose(f);
    unlink(tmp_path);
    safe_free(tmp_path);
    return false;
  }
  for (uint32_t i = 0; i < count; i++) {
    uint8_t buf[8];
    uint64_to_le(buf, slots[i]);
    if (fwrite(buf, 1, 8, f) != 8) {
      fclose(f);
      unlink(tmp_path);
      safe_free(tmp_path);
      return false;
    }
  }
  fclose(f);

  char* final_path = idx_path(period);
  if (rename(tmp_path, final_path) != 0) {
    log_warn("period_store: rename(%s -> %s) failed: %s", tmp_path, final_path, strerror(errno));
    unlink(tmp_path);
    safe_free(tmp_path);
    safe_free(final_path);
    return false;
  }
  safe_free(tmp_path);
  safe_free(final_path);
  return true;
}

// Removes snapshot files whose anchor is older than `finalized_slot -
// CHECKPOINTZ_WINDOW_SLOTS` and rewrites snapshots.idx accordingly. The idx
// is updated BEFORE unlinking files so the index is always a subset of the
// physically present snapshots (never references a missing file).
static void snapshots_cleanup(uint64_t period, uint64_t finalized_slot) {
  uint64_t* slots = NULL;
  uint32_t  count = 0;
  if (!snapshots_idx_load(period, &slots, &count) || count == 0) {
    safe_free(slots);
    return;
  }

  uint64_t threshold = finalized_slot > CHECKPOINTZ_WINDOW_SLOTS ? finalized_slot - CHECKPOINTZ_WINDOW_SLOTS : 0;

  uint64_t* keep      = (uint64_t*) safe_calloc(count, sizeof(uint64_t));
  uint64_t* remove    = (uint64_t*) safe_calloc(count, sizeof(uint64_t));
  uint32_t  keep_n    = 0;
  uint32_t  remove_n  = 0;
  for (uint32_t i = 0; i < count; i++) {
    if (slots[i] >= threshold)
      keep[keep_n++] = slots[i];
    else
      remove[remove_n++] = slots[i];
  }

  if (remove_n > 0) {
    snapshots_idx_save(period, keep, keep_n);
    for (uint32_t i = 0; i < remove_n; i++) {
      char* fpath = bprintf(NULL, "%s/%l/zk_proof_checkpoint_%l.ssz", eth_config.period_store, period, remove[i]);
      if (unlink(fpath) != 0 && errno != ENOENT)
        log_warn("period_store: unlink %s failed: %s", fpath, strerror(errno));
      safe_free(fpath);
    }
    log_debug("period_store: cleaned %d old historic_proof snapshots for period %l (kept %d)", remove_n, period, keep_n);
  }

  safe_free(slots);
  safe_free(keep);
  safe_free(remove);
}

// Idempotently appends `new_slot` to the period's snapshots.idx. The list is
// kept sorted ascending for deterministic prover lookups (largest <= finalized).
static void snapshots_idx_add(uint64_t period, uint64_t new_slot) {
  uint64_t* slots = NULL;
  uint32_t  count = 0;
  if (!snapshots_idx_load(period, &slots, &count)) {
    safe_free(slots);
    return;
  }

  for (uint32_t i = 0; i < count; i++) {
    if (slots[i] == new_slot) {
      safe_free(slots);
      return;
    }
  }

  uint64_t* merged = (uint64_t*) safe_calloc(count + 1, sizeof(uint64_t));
  uint32_t  pos    = 0;
  while (pos < count && slots[pos] < new_slot) {
    merged[pos] = slots[pos];
    pos++;
  }
  merged[pos] = new_slot;
  for (uint32_t i = pos; i < count; i++)
    merged[i + 1] = slots[i];

  snapshots_idx_save(period, merged, count + 1);
  safe_free(slots);
  safe_free(merged);
}

// =============================================================================
// Async pipeline state machine.
// =============================================================================

static void build_ctx_free(build_ctx_t* bctx) {
  if (!bctx) return;
  safe_free(bctx->sync_ssz.data);
  safe_free(bctx->blocks_ssz.data);
  safe_free(bctx->header_response.data);
  safe_free(bctx->summaries_response.data);
  safe_free(bctx);
}

static void snapshot_write_cb(void* user_data, file_data_t* files, int num_files) {
  build_ctx_t* bctx = (build_ctx_t*) user_data;
  if (files[0].error) {
    log_warn("period_store: write zk_proof_checkpoint_%l.ssz for period %l failed: %s",
             bctx->anchor_slot, bctx->period, files[0].error);
  }
  else {
    log_info("period_store: wrote zk_proof_checkpoint_%l.ssz for period %l (%d bytes)",
             bctx->anchor_slot, bctx->period, files[0].data.len);
    snapshots_idx_add(bctx->period, bctx->anchor_slot);
    snapshots_cleanup(bctx->period, bctx->anchor_slot);
  }
  c4_file_data_array_free(files, num_files, 1);
  build_ctx_free(bctx);
}

// Builds a 112-byte BeaconBlockHeader from the JSON response of
// `eth/v1/beacon/headers/${slot}` (message section). Writes into `out` (must
// be at least HEADER_SSZ_SIZE bytes).
static bool encode_beacon_block_header_from_json(json_t header_message, uint8_t* out) {
  if (header_message.type != JSON_TYPE_OBJECT) return false;

  uint64_to_le(out + 0, json_get_uint64(header_message, "slot"));
  uint64_to_le(out + 8, json_get_uint64(header_message, "proposer_index"));

  buffer_t parent_buf = {.data = {.data = out + 16, .len = 32}, .allocated = -32};
  buffer_t state_buf  = {.data = {.data = out + 48, .len = 32}, .allocated = -32};
  buffer_t body_buf   = {.data = {.data = out + 80, .len = 32}, .allocated = -32};

  bytes_t parent = json_get_bytes(header_message, "parent_root", &parent_buf);
  bytes_t state  = json_get_bytes(header_message, "state_root", &state_buf);
  bytes_t body   = json_get_bytes(header_message, "body_root", &body_buf);
  return parent.len == 32 && state.len == 32 && body.len == 32;
}

static void try_complete_build(build_ctx_t* bctx) {
  if (bctx->any_failed) {
    log_debug("period_store: historic_proof snapshot build for period %l slot %l aborted (one or more fetches failed)",
              bctx->period, bctx->anchor_slot);
    build_ctx_free(bctx);
    return;
  }

  // Decode period attested header from sync.ssz
  const ssz_def_t* verify_request_def = eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST);
  ssz_ob_t         sync               = {.def = verify_request_def, .bytes = bctx->sync_ssz};
  ssz_ob_t         proof              = ssz_get(&sync, "proof");
  uint64_t         attested_slot      = ssz_get_uint64(&proof, "slot");
  if (attested_slot == 0) {
    log_warn("period_store: invalid sync.ssz for period %l (no slot)", bctx->period);
    build_ctx_free(bctx);
    return;
  }
  uint64_t block_period = attested_slot >> 13;
  uint64_t block_idx    = attested_slot & (SLOTS_PER_PERIOD - 1);

  // Parse JSON responses
  char* hdr_str = (char*) safe_calloc(1, bctx->header_response.len + 1);
  memcpy(hdr_str, bctx->header_response.data, bctx->header_response.len);
  json_t hdr_doc = json_parse(hdr_str);
  json_t hdr_msg = json_get(json_get(json_get(hdr_doc, "data"), "header"), "message");
  if (hdr_msg.type != JSON_TYPE_OBJECT) {
    log_warn("period_store: invalid header response for slot %l", bctx->anchor_slot);
    safe_free(hdr_str);
    build_ctx_free(bctx);
    return;
  }
  uint8_t anchor_header[HEADER_SSZ_SIZE] = {0};
  if (!encode_beacon_block_header_from_json(hdr_msg, anchor_header)) {
    log_warn("period_store: failed to encode anchor header for slot %l", bctx->anchor_slot);
    safe_free(hdr_str);
    build_ctx_free(bctx);
    return;
  }

  char* sum_str = (char*) safe_calloc(1, bctx->summaries_response.len + 1);
  memcpy(sum_str, bctx->summaries_response.data, bctx->summaries_response.len);
  json_t sum_doc = json_parse(sum_str);
  if (json_get(sum_doc, "data").type != JSON_TYPE_OBJECT) {
    log_warn("period_store: invalid historical_summaries response for slot %l", bctx->anchor_slot);
    safe_free(hdr_str);
    safe_free(sum_str);
    build_ctx_free(bctx);
    return;
  }

  // Build merkle proof via shared helper.
  bytes_t  merkle_proof = {0};
  gindex_t combined_gix = 0;
  if (c4_build_historic_merkle_proof(
          (chain_id_t) http_server.chain_id,
          NULL, // server has no state; errors get log_warn
          block_period,
          block_idx,
          bctx->blocks_ssz,
          sum_doc,
          bctx->anchor_slot,
          &merkle_proof,
          &combined_gix) != C4_SUCCESS) {
    safe_free(hdr_str);
    safe_free(sum_str);
    safe_free(merkle_proof.data);
    build_ctx_free(bctx);
    return;
  }

  // Build ZKSyncData SSZ with historic_proof variant (index 1 in checkpoint union).
  ssz_builder_t builder            = ssz_builder_for_def(C4_ETH_REQUEST_SYNCDATA_UNION + 2);
  ssz_builder_t checkpoint_builder = ssz_builder_for_def(ssz_get_def(builder.def, "checkpoint")->def.container.elements + 1);
  ssz_add_bytes(&checkpoint_builder, "proof", merkle_proof);
  ssz_add_bytes(&checkpoint_builder, "header", bytes(anchor_header, HEADER_SSZ_SIZE));
  ssz_add_uint64(&checkpoint_builder, (uint64_t) combined_gix);
  // historic_proof does not carry a BLS signature -- the anchor header is bound
  // via checkpointz, see verifier `update_from_zk_sync_data`.
  ssz_add_bytes(&checkpoint_builder, "sync_committee_bits", bytes(NULL, 64));
  ssz_add_bytes(&checkpoint_builder, "sync_committee_signature", bytes(NULL, 96));

  // Reuse vk_hash, proof, header, pubkeys from the legacy zk_proof.ssz. We
  // read it back from disk rather than re-deriving to avoid maintaining two
  // build paths (and because zk_proof.ssz is the source of truth for these
  // fields after `c4_build_zk_sync_proof_data`).
  char*       legacy_path  = bprintf(NULL, "%s/%l/zk_proof.ssz", eth_config.period_store, bctx->period);
  FILE*       lf           = fopen(legacy_path, "rb");
  bytes_t     legacy_bytes = NULL_BYTES;
  if (lf) {
    fseek(lf, 0, SEEK_END);
    long sz = ftell(lf);
    fseek(lf, 0, SEEK_SET);
    if (sz > 0) {
      legacy_bytes.data = safe_malloc((uint32_t) sz);
      legacy_bytes.len  = (uint32_t) fread(legacy_bytes.data, 1, (size_t) sz, lf);
    }
    fclose(lf);
  }
  safe_free(legacy_path);

  if (legacy_bytes.len == 0) {
    log_warn("period_store: cannot read zk_proof.ssz for period %l, skipping historic snapshot", bctx->period);
    safe_free(legacy_bytes.data);
    safe_free(hdr_str);
    safe_free(sum_str);
    safe_free(merkle_proof.data);
    buffer_free(&builder.fixed);
    buffer_free(&builder.dynamic);
    buffer_free(&checkpoint_builder.fixed);
    buffer_free(&checkpoint_builder.dynamic);
    build_ctx_free(bctx);
    return;
  }

  ssz_ob_t legacy = {.def = C4_ETH_REQUEST_SYNCDATA_UNION + 2, .bytes = legacy_bytes};
  ssz_add_bytes(&builder, "vk_hash", ssz_get(&legacy, "vk_hash").bytes);
  ssz_add_bytes(&builder, "proof", ssz_get(&legacy, "proof").bytes);
  ssz_add_bytes(&builder, "header", ssz_get(&legacy, "header").bytes);
  ssz_add_bytes(&builder, "pubkeys", ssz_get(&legacy, "pubkeys").bytes);
  ssz_add_builders(&builder, "checkpoint", checkpoint_builder);
  ssz_add_bytes(&builder, "signatures", NULL_BYTES);

  file_data_t out_file = {
      .data = ssz_builder_to_bytes(&builder).bytes,
      .path = bprintf(NULL, "%s/%l/zk_proof_checkpoint_%l.ssz", eth_config.period_store, bctx->period, bctx->anchor_slot)};

  safe_free(legacy_bytes.data);
  safe_free(hdr_str);
  safe_free(sum_str);
  safe_free(merkle_proof.data);

  int rc = c4_write_files_uv(bctx, snapshot_write_cb, &out_file, 1, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (rc < 0) {
    log_warn("period_store: scheduling snapshot write failed for period %l slot %l", bctx->period, bctx->anchor_slot);
    c4_file_data_array_free(&out_file, 1, 1);
    build_ctx_free(bctx);
  }
}

static void header_fetch_cb(client_t* client, void* user_data, data_request_t* r) {
  (void) client;
  build_ctx_t* bctx = (build_ctx_t*) user_data;
  if (r->error || !r->response.data || r->response.len == 0) {
    log_debug("period_store: header fetch for slot %l failed: %s", bctx->anchor_slot, r->error ? r->error : "empty response");
    bctx->any_failed = true;
  }
  else {
    bctx->header_response = r->response;
    r->response           = NULL_BYTES;
  }
  c4_request_free(r);
  if (--bctx->requests_remaining == 0) try_complete_build(bctx);
}

static void summaries_fetch_cb(client_t* client, void* user_data, data_request_t* r) {
  (void) client;
  build_ctx_t* bctx = (build_ctx_t*) user_data;
  if (r->error || !r->response.data || r->response.len == 0) {
    log_debug("period_store: historical_summaries fetch for slot %l failed: %s", bctx->anchor_slot, r->error ? r->error : "empty response");
    bctx->any_failed = true;
  }
  else {
    bctx->summaries_response = r->response;
    r->response              = NULL_BYTES;
  }
  c4_request_free(r);
  if (--bctx->requests_remaining == 0) try_complete_build(bctx);
}

static void files_read_cb(void* user_data, file_data_t* files, int num_files) {
  build_ctx_t* bctx = (build_ctx_t*) user_data;
  if (files[0].error || files[1].error || files[0].data.len == 0 || files[1].data.len == 0) {
    log_debug("period_store: snapshot build for period %l aborted: required files missing (%s / %s)",
              bctx->period,
              files[0].error ? files[0].error : "ok",
              files[1].error ? files[1].error : "ok");
    c4_file_data_array_free(files, num_files, 1);
    build_ctx_free(bctx);
    return;
  }

  bctx->sync_ssz    = files[0].data;
  bctx->blocks_ssz  = files[1].data;
  files[0].data     = NULL_BYTES;
  files[1].data     = NULL_BYTES;
  c4_file_data_array_free(files, num_files, 1);

  server_list_t* sl = c4_get_server_list(C4_DATA_TYPE_BEACON_API);
  if (!sl || sl->count == 0) {
    log_debug("period_store: snapshot build for period %l aborted: no beacon API servers configured", bctx->period);
    build_ctx_free(bctx);
    return;
  }

  bctx->requests_remaining = 2;

  static client_t snapshot_client = {0};

  data_request_t* hreq = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
  hreq->url            = bprintf(NULL, "eth/v1/beacon/headers/%l", bctx->anchor_slot);
  hreq->method         = C4_DATA_METHOD_GET;
  hreq->chain_id       = http_server.chain_id;
  hreq->type           = C4_DATA_TYPE_BEACON_API;
  hreq->encoding       = C4_DATA_ENCODING_JSON;
  c4_add_request(&snapshot_client, hreq, bctx, header_fetch_cb);

  data_request_t* sreq       = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
  sreq->url                  = bprintf(NULL, "eth/v1/lodestar/states/%l/historical_summaries", bctx->anchor_slot);
  sreq->method               = C4_DATA_METHOD_GET;
  sreq->chain_id             = http_server.chain_id;
  sreq->type                 = C4_DATA_TYPE_BEACON_API;
  sreq->encoding             = C4_DATA_ENCODING_JSON;
  sreq->preferred_client_type = BEACON_CLIENT_LODESTAR;
  c4_add_request(&snapshot_client, sreq, bctx, summaries_fetch_cb);
}

void c4_ps_build_historic_proof_snapshot(uint64_t period, uint64_t finalized_slot) {
  if (eth_config.period_master_url) return; // slave mode: no local builds
  if (!eth_config.period_store) return;
  if (period == 0) return;

  // Idempotent: skip if we already have this exact snapshot.
  char* fname = bprintf(NULL, "zk_proof_checkpoint_%l.ssz", finalized_slot);
  bool  have  = c4_ps_file_exists(period, fname);
  safe_free(fname);
  if (have) {
    // Still run cleanup so the index can drop stale entries.
    snapshots_cleanup(period, finalized_slot);
    return;
  }

  build_ctx_t* bctx = (build_ctx_t*) safe_calloc(1, sizeof(build_ctx_t));
  bctx->period      = period;
  bctx->anchor_slot = finalized_slot;

  file_data_t files[2] = {0};
  files[0].path        = bprintf(NULL, "%s/%l/sync.ssz", eth_config.period_store, period);
  files[1].path        = bprintf(NULL, "%s/%l/blocks.ssz", eth_config.period_store, period - 1);

  int rc = c4_read_files_uv(bctx, files_read_cb, files, 2);
  if (rc < 0) {
    log_warn("period_store: scheduling snapshot prep read for period %l failed", period);
    c4_file_data_array_free(files, 2, 0);
    safe_free(bctx);
  }
}
