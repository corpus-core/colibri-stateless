// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "beacon.h"
#include "beacon_types.h"
#include "logger.h"
#include "server.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <uv.h>

static void proofer_request_free(request_t* req) {
  c4_proofer_free((proofer_ctx_t*) req->ctx);
  safe_free(req);
}

typedef struct {
  uint32_t  last_slot;
  uint32_t  last_lcu;
  bytes32_t last_block_root;
} head_update_t;
static head_update_t head_update = {0};

static void fill_last_update(uint64_t current_slot) {
  if (!http_server.period_store)
    return;

  char                tmp[1000] = {0};
  const chain_spec_t* spec      = c4_eth_get_chain_spec((chain_id_t) http_server.chain_id);
  buffer_t            buf       = stack_buffer(tmp);
  uint32_t            period    = current_slot >> (spec->slots_per_epoch_bits + spec->epochs_per_period_bits);
  while (period && (!head_update.last_slot || !head_update.last_lcu)) {

    if (!head_update.last_slot) {
      buffer_reset(&buf);
      char* blocks_path = bprintf(&buf, "%s/%l/%d/blocks.ssz", http_server.period_store, http_server.chain_id, period);
      FILE* f           = fopen(blocks_path, "rb");
      if (!f) {
        period--;
        continue;
      }

      struct stat stats;
      if (stat(blocks_path, &stats) != 0 || stats.st_size == 0) {
        fclose(f);
        period--;
        continue;
      }

      head_update.last_slot = (((uint64_t) period) << 13) + (stats.st_size / 32) - 1;
      log_info("found last slot: %l in period %d with size %l", head_update.last_slot, period, (uint64_t) stats.st_size);
      if (stats.st_size > 0) {
        size_t bytes_to_read = (stats.st_size < sizeof(head_update.last_block_root)) ? (size_t) stats.st_size : sizeof(head_update.last_block_root);
        long   offset        = -(long) bytes_to_read;

        if (fseek(f, offset, SEEK_END) == 0) {
          size_t bytes_actually_read = fread(head_update.last_block_root, 1, bytes_to_read, f);
          if (bytes_actually_read != bytes_to_read)
            log_warn("Konnte nicht die erwartete Anzahl an Bytes aus %s lesen.", blocks_path);
          if (bytes_actually_read < sizeof(head_update.last_block_root))
            memset(head_update.last_block_root + bytes_actually_read, 0, sizeof(head_update.last_block_root) - bytes_actually_read);
        }
        else
          log_warn("Konnte nicht ans Ende der Datei %s springen.", blocks_path);
      }
      fclose(f);
    }

    if (!head_update.last_lcu) {
      buffer_reset(&buf);
      char*       lcu_path = bprintf(&buf, "%s/%l/%d/lcu.ssz", http_server.period_store, http_server.chain_id, period);
      struct stat stats;
      if (stat(lcu_path, &stats) != 0 || stats.st_size == 0) {
        period--;
        continue;
      }
      head_update.last_lcu = period;
    }
  }
}

typedef struct {
  uv_fs_t  fs_req;
  uv_buf_t uv_buf;
  char*    path;
  bytes_t  data_to_write;
  int      file_descriptor;
} append_file_req_t;

static void on_file_closed_after_append(uv_fs_t* req) {
  append_file_req_t* append_req = (append_file_req_t*) req->data;
  //  log_info("added to file %s %d bytes", append_req->path, append_req->data_to_write.len);
  if (req->result < 0) {
    log_warn("Fehler beim Schließen der Datei %s nach dem Anhängen: %s", append_req->path, uv_strerror((int) req->result));
  }
  uv_fs_req_cleanup(req);
  safe_free(append_req->path);
  safe_free(append_req->data_to_write.data);
  safe_free(append_req);
}

static void on_file_written_for_append(uv_fs_t* req) {
  append_file_req_t* append_req = (append_file_req_t*) req->data;
  if (req->result < 0) {
    log_warn("Fehler beim Schreiben in die Datei %s: %s", append_req->path, uv_strerror((int) req->result));
  }
  else if ((size_t) req->result != append_req->data_to_write.len) {
    log_warn("Nicht alle Bytes wurden in %s geschrieben. Erwartet: %zu, Geschrieben: %ld", append_req->path, append_req->data_to_write.len, req->result);
  }

  uv_fs_req_cleanup(req);
  uv_fs_close(uv_default_loop(), &append_req->fs_req, append_req->file_descriptor, on_file_closed_after_append);
}

static void on_file_opened_for_append(uv_fs_t* req) {
  append_file_req_t* append_req = (append_file_req_t*) req->data;
  if (req->result < 0) {
    log_warn("Fehler beim Öffnen/Erstellen der Datei %s zum Anhängen: %s", append_req->path, uv_strerror((int) req->result));
    uv_fs_req_cleanup(req);
    safe_free(append_req->path);
    safe_free(append_req->data_to_write.data);
    safe_free(append_req);
    return;
  }
  append_req->file_descriptor = (int) req->result;
  uv_fs_req_cleanup(req);

  append_req->uv_buf = uv_buf_init((char*) append_req->data_to_write.data, append_req->data_to_write.len);
  uv_fs_write(uv_default_loop(), &append_req->fs_req, append_req->file_descriptor, &append_req->uv_buf, 1, -1, on_file_written_for_append);
}

static void append_data(char* path, bytes_t data) {
  if (!path || !data.data || data.len == 0) {
    log_warn("Ungültiger Pfad oder Daten für append_data.");
    return;
  }

  append_file_req_t* append_req = (append_file_req_t*) safe_calloc(1, sizeof(append_file_req_t));
  if (!append_req) {
    log_error("Speicherallokierungsfehler für append_file_req_t");
    return;
  }

  append_req->path = strdup(path);
  if (!append_req->path) {
    log_error("Speicherallokierungsfehler für Pfadkopie in append_data");
    safe_free(append_req);
    return;
  }

  append_req->data_to_write.data = (uint8_t*) safe_malloc(data.len);
  if (!append_req->data_to_write.data) {
    log_error("Speicherallokierungsfehler für Datenkopie in append_data");
    safe_free(append_req->path);
    safe_free(append_req);
    return;
  }
  memcpy(append_req->data_to_write.data, data.data, data.len);
  append_req->data_to_write.len = data.len;

  append_req->fs_req.data = append_req;

  // O_APPEND: Daten ans Ende anhängen
  // O_CREAT: Datei erstellen, falls sie nicht existiert
  // O_WRONLY: Nur zum Schreiben öffnen
  // 0600: Les- und Schreibrechte für den Eigentümer
  int flags = O_APPEND | O_CREAT | O_WRONLY;
  int mode  = S_IRUSR | S_IWUSR; // Entspricht 0600

  //  log_info("opening file %s %d bytes", append_req->path, append_req->data_to_write.len);
  int r = uv_fs_open(uv_default_loop(), &append_req->fs_req, append_req->path, flags, mode, on_file_opened_for_append);
  if (r < 0) {
    log_error("Fehler beim Initiieren von uv_fs_open für %s: %s", append_req->path, uv_strerror(r));
    safe_free(append_req->path);
    safe_free(append_req->data_to_write.data);
    safe_free(append_req);
    // uv_fs_req_cleanup ist hier nicht nötig, da der Request nicht erfolgreich initialisiert wurde
  }
}

static c4_status_t handle_head(proofer_ctx_t* ctx, beacon_head_t* b, ssz_ob_t* sig_block, ssz_ob_t* data_block) {
  c4_status_t         status      = C4_SUCCESS;
  const chain_spec_t* spec        = c4_eth_get_chain_spec((chain_id_t) http_server.chain_id);
  uint32_t            period      = b->slot >> (spec->slots_per_epoch_bits + spec->epochs_per_period_bits);
  char                tmp[300]    = {0};
  char                tmp2[300]   = {0};
  buffer_t            buf1        = stack_buffer(tmp);
  buffer_t            buf2        = stack_buffer(tmp2);
  bytes_t             block_roots = {0};
  bytes_t             lcu         = {0};
  TRY_ASYNC(c4_eth_get_signblock_and_parent(ctx, b->root, NULL, sig_block, data_block, NULL));

  return C4_SUCCESS;

  //  TRY_ADD_ASYNC(status, c4_send_internal_request(ctx, bprintf(&buf1, "chain_store/%l/%d/blocks.ssz", http_server.chain_id, period), NULL, 0, &block_roots));
  //  TRY_ADD_ASYNC(status, c4_send_internal_request(ctx, bprintf(&buf2, "chain_store/%l/%d/lcu.ssz", http_server.chain_id, period), NULL, 0, &lcu));
}

static void handle_new_head_cb(request_t* req) {
  if (c4_check_retry_request(req)) return; // if there are data_request in the req, we either clean it up or retry in case of an error (if possible.)
  proofer_ctx_t* ctx = (proofer_ctx_t*) req->ctx;
  beacon_head_t* b   = (beacon_head_t*) ctx->proof.data;
  ssz_ob_t       sig_block, data_block;

  switch (handle_head(ctx, b, &sig_block, &data_block)) {
    case C4_SUCCESS: {
      bytes32_t cache_key = {0};
      sprintf((char*) cache_key, "Slatest");
      c4_proofer_cache_invalidate(cache_key);
      beacon_block_t* beacon_block = (beacon_block_t*) safe_calloc(1, sizeof(beacon_block_t));
      ssz_ob_t        sig_body     = ssz_get(&sig_block, "body");
      beacon_block->slot           = ssz_get_uint64(&data_block, "slot");
      beacon_block->header         = data_block;
      beacon_block->body           = ssz_get(&data_block, "body");
      beacon_block->execution      = ssz_get(&beacon_block->body, "executionPayload");
      beacon_block->sync_aggregate = ssz_get(&sig_body, "syncAggregate");
      bytes_t  root_hash           = ssz_get(&sig_block, "parentRoot").bytes;
      ssz_ob_t execution           = ssz_get(&sig_body, "executionPayload");
      c4_beacon_cache_update_blockdata(ctx, beacon_block, ssz_get_uint64(&execution, "timestamp"), root_hash.data);
      proofer_request_free(req);
      return;
    }
    case C4_ERROR: {
      log_error("Error fetching sigblock and parent: %s", ctx->state.error);
      proofer_request_free(req);
      return;
    }
    case C4_PENDING:
      if (c4_state_get_pending_request(&ctx->state)) // there are pending requests, let's take care of them first
        c4_start_curl_requests(req);
      else {
        log_error("Error fetching sigblock and parent: %s", ctx->state.error);
        proofer_request_free(req);
      }

      return;
  }
}

void c4_handle_new_head(json_t head) {

  beacon_head_t* b      = (beacon_head_t*) safe_calloc(1, sizeof(beacon_head_t));
  buffer_t       buffer = stack_buffer(b->root);
  b->slot               = json_get_uint64(head, "slot");
  bytes_t        root   = json_get_bytes(head, "block", &buffer);
  request_t*     req    = (request_t*) safe_calloc(1, sizeof(request_t));
  proofer_ctx_t* ctx    = (proofer_ctx_t*) safe_calloc(1, sizeof(proofer_ctx_t));
  req->client           = NULL;
  req->cb               = handle_new_head_cb;
  req->ctx              = ctx;
  ctx->proof            = bytes(b, sizeof(beacon_head_t));
  ctx->chain_id         = http_server.chain_id;
  ctx->client_type      = BEACON_CLIENT_EVENT_SERVER;
  handle_new_head_cb(req);
}

static void c4_handle_finalized_checkpoint_cb(request_t* req) {
  proofer_ctx_t* ctx = (proofer_ctx_t*) req->ctx;

  switch (c4_eth_update_finality(ctx)) {
    case C4_SUCCESS: {
      proofer_request_free(req);
      return;
    }
    case C4_ERROR: {
      log_error("Error fetching sigblock and parent: %s", ctx->state.error);
      proofer_request_free(req);
      return;
    }
    case C4_PENDING:
      if (c4_state_get_pending_request(&ctx->state)) // there are pending requests, let's take care of them first
        c4_start_curl_requests(req);
      else {
        log_error("Error fetching sigblock and parent: %s", ctx->state.error);
        proofer_request_free(req);
      }
  }
}

void c4_handle_finalized_checkpoint(json_t checkpoint) {
  request_t* req                           = (request_t*) safe_calloc(1, sizeof(request_t));
  req->cb                                  = c4_handle_finalized_checkpoint_cb;
  req->ctx                                 = safe_calloc(1, sizeof(proofer_ctx_t));
  ((proofer_ctx_t*) req->ctx)->client_type = BEACON_CLIENT_EVENT_SERVER;
  req->cb(req);
}
