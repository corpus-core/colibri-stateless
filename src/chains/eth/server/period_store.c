
#include "period_prover.h"
#include "period_store_check.h"
#include "period_store_internal.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <uv.h>

bool c4_ps_file_exists(uint64_t period, const char* filename) {
  char*       path = bprintf(NULL, "%s/%l/%s", eth_config.period_store, period, filename);
  struct stat buffer;
  bool        exists = stat(path, &buffer) == 0;
  safe_free(path);
  return exists;
}
static inline bool is_file_not_found(char* error) {
  return error && strstr(error, "such file or directory") != NULL;
}
// Backfill state is defined here and shared via period_store_internal.h
backfill_ctx_t    bf_ctx             = {0};
write_queue_ctx_t queue              = {0};
uint64_t          latest_head_slot   = 0;
uint64_t          latest_hist_period = UINT64_MAX;

// SSZ definition for blocks.ssz: 8192 block roots (bytes32 each)
const ssz_def_t BLOCKS = SSZ_VECTOR("blocks", ssz_bytes32, SLOTS_PER_PERIOD);

char* c4_ps_ensure_period_dir(uint64_t period) {
  uv_fs_t req = {};
  if (!queue.base_dir) {
    int rc = uv_fs_mkdir(uv_default_loop(), &req, eth_config.period_store, 0777, NULL);
    if (rc < 0 && req.result != UV_EEXIST)
      log_warn("period_store: mkdir failed for %s: %s", eth_config.period_store, uv_strerror((int) req.result));
    else
      queue.base_dir = eth_config.period_store;
    uv_fs_req_cleanup(&req);
  }

  char* dir = bprintf(NULL, "%s/%l", queue.base_dir, period);
  if (queue.last_checked_period != period) {
    req    = (uv_fs_t) {0};
    int rc = uv_fs_mkdir(uv_default_loop(), &req, dir, 0777, NULL);
    if (rc < 0 && req.result != UV_EEXIST)
      log_warn("period_store: mkdir failed for %s: %s", dir, uv_strerror((int) req.result));
    queue.last_checked_period = period;
    uv_fs_req_cleanup(&req);
  }

  return dir;
}

// write completion callback for c4_write_files_uv
static void ps_write_done_cb(void* user_data, file_data_t* files, int num_files) {
  fs_ctx_t* ctx = (fs_ctx_t*) user_data;
  bool      ok  = true;
  for (int i = 0; i < num_files; i++) {
    if (files[i].error) {
      log_error("period_store: write %s failed: %s", files[i].path ? files[i].path : "(null)", files[i].error);
      ok = false;
    }
  }
  // free only meta (paths/errors) - don't free files[i].data.data (points into task memory)
  c4_file_data_array_free(files, num_files, 0);
  c4_ps_finish_write(ctx, ok);
}
// After both files written, close FDs and finish
void c4_ps_finish_write(fs_ctx_t* ctx, bool ok) {
  // close if open
  if (ctx->blocks_fd >= 0) {
    uv_fs_t c = {0};
    uv_fs_close(uv_default_loop(), &c, ctx->blocks_fd, NULL);
    uv_fs_req_cleanup(&c);
    ctx->blocks_fd = -1;
  }
  if (ctx->headers_fd >= 0) {
    uv_fs_t c = {0};
    uv_fs_close(uv_default_loop(), &c, ctx->headers_fd, NULL);
    uv_fs_req_cleanup(&c);
    ctx->headers_fd = -1;
  }

  write_task_t* task = ctx->task;
  // capture values we need after freeing the task/context
  uint64_t slot          = task->block.slot;
  bool     was_backfill  = task->run_backfill;
  bool     call_backfill = was_backfill;
  bool     call_next     = task->next != NULL;

  // remove from queue
  if (task == queue.head) {
    queue.head = task->next;
    if (!queue.head) queue.tail = NULL;
  }
  if (http_server.stats.period_sync_queue_depth > 0) http_server.stats.period_sync_queue_depth--;

  // !call_back means this came from the beacon_events
  // and means we need to check, if we should start backfilling.
  if (!call_backfill)
    c4_ps_backfill_check(&task->block);
  else {
    // check if this is the last task from backfill.
    for (write_task_t* t = task->next; t; t = t->next) {
      if (t->run_backfill) {
        // there are more tasks from backfill,
        // so we don't need to call backfill yet. Only the last one should.
        call_backfill = false;
        break;
      }
    }
  }

  // metrics and logging
  if (ok) {
    http_server.stats.period_sync_last_slot    = slot;
    http_server.stats.period_sync_last_slot_ts = current_unix_ms();
    if (was_backfill)
      http_server.stats.period_sync_backfilled_slots_total++;
    else
      http_server.stats.period_sync_written_slots_total++;
    if (latest_head_slot >= http_server.stats.period_sync_last_slot)
      http_server.stats.period_sync_lag_slots = latest_head_slot - http_server.stats.period_sync_last_slot;
    else
      http_server.stats.period_sync_lag_slots = 0;
    log_debug("period_store: wrote slot %l (%s)", slot, was_backfill ? "backfill" : "head");
  }
  else {
    http_server.stats.period_sync_errors_total++;
    log_warn("period_store: write failed");
  }

  // free task and context LAST, after all uses of task data
  safe_free(ctx->task);
  safe_free(ctx->blocks_path);
  safe_free(ctx->headers_path);
  buffer_free(&ctx->tmp);
  safe_free(ctx);

  if (call_backfill) {
    log_debug("period_store: continue backfill after write");
    c4_ps_backfill();
  }
  if (call_next) c4_ps_run_write_block_queue();
}

static void on_write_headers_done(uv_fs_t* req) {
  fs_ctx_t* ctx = (fs_ctx_t*) req->data;
  bool      ok  = req->result >= 0;
  uv_fs_req_cleanup(req);
  safe_free(req);
  c4_ps_finish_write(ctx, ok);
}

static void on_open_headers(uv_fs_t* req) {
  fs_ctx_t* ctx = (fs_ctx_t*) req->data;
  if (req->result < 0) {
    log_error("period_store: open headers failed: %s", uv_strerror((int) req->result));
    uv_fs_req_cleanup(req);
    safe_free(req);
    c4_ps_finish_write(ctx, false);
    return;
  }
  ctx->headers_fd = (uv_file) req->result;
  uv_fs_req_cleanup(req);
  safe_free(req);

  // write headers at offset
  uv_fs_t* w   = (uv_fs_t*) safe_calloc(1, sizeof(uv_fs_t));
  w->data      = ctx;
  uv_buf_t buf = uv_buf_init((char*) ctx->task->block.header, HEADER_SIZE);
  UVX_CHECK("uv_fs_write(headers)",
            uv_fs_write(uv_default_loop(), w, ctx->headers_fd, &buf, 1, (int64_t) ctx->headers_offset, on_write_headers_done),
            uv_fs_req_cleanup(w);
            safe_free(w),
            c4_ps_finish_write(ctx, false);
            return);
}

static void on_write_blocks_done(uv_fs_t* req) {
  fs_ctx_t* ctx = (fs_ctx_t*) req->data;
  if (req->result < 0) {
    log_error("period_store: write blocks failed: %s", uv_strerror((int) req->result));
    uv_fs_req_cleanup(req);
    safe_free(req);
    c4_ps_finish_write(ctx, false);
    return;
  }
  uv_fs_req_cleanup(req);
  safe_free(req);

  // open headers next
  uv_fs_t* o = (uv_fs_t*) safe_calloc(1, sizeof(uv_fs_t));
  o->data    = ctx;
  UVX_CHECK("uv_fs_open(headers)",
            uv_fs_open(uv_default_loop(), o, ctx->headers_path, O_RDWR | O_CREAT, 0666, on_open_headers),
            uv_fs_req_cleanup(o);
            safe_free(o),
            c4_ps_finish_write(ctx, false);
            return);
}

static void on_open_blocks(uv_fs_t* req) {
  fs_ctx_t* ctx = (fs_ctx_t*) req->data;
  if (req->result < 0) {
    log_error("period_store: open blocks failed: %s", uv_strerror((int) req->result));
    uv_fs_req_cleanup(req);
    safe_free(req);
    c4_ps_finish_write(ctx, false);
    return;
  }
  ctx->blocks_fd = (uv_file) req->result;
  uv_fs_req_cleanup(req);
  safe_free(req);

  uv_fs_t* w   = (uv_fs_t*) safe_calloc(1, sizeof(uv_fs_t));
  w->data      = ctx;
  uv_buf_t buf = uv_buf_init((char*) ctx->task->block.root, 32);
  UVX_CHECK("uv_fs_write(blocks)",
            uv_fs_write(uv_default_loop(), w, ctx->blocks_fd, &buf, 1, (int64_t) ctx->blocks_offset, on_write_blocks_done),
            uv_fs_req_cleanup(w);
            safe_free(w),
            c4_ps_finish_write(ctx, false);
            return);
}

// execute the head and if successful, we schedule the next one.
void c4_ps_run_write_block_queue(void) {
  if (!queue.head) return;
  write_task_t* task     = queue.head;
  uint64_t      period   = task->block.slot / SLOTS_PER_PERIOD;
  uint64_t      idx      = task->block.slot % SLOTS_PER_PERIOD;
  char*         dir_path = c4_ps_ensure_period_dir(period);

  fs_ctx_t* ctx       = (fs_ctx_t*) safe_calloc(1, sizeof(fs_ctx_t));
  ctx->task           = task;
  ctx->blocks_fd      = -1;
  ctx->headers_fd     = -1;
  ctx->blocks_offset  = idx * 32;
  ctx->headers_offset = idx * HEADER_SIZE;
  ctx->blocks_path    = bprintf(NULL, "%s/blocks.ssz", dir_path);
  ctx->headers_path   = bprintf(NULL, "%s/headers.ssz", dir_path);
  safe_free(dir_path);

  // Use uv_util to write both files asynchronously at specific offsets
  file_data_t* files = (file_data_t*) safe_calloc(2, sizeof(file_data_t));
  files[0].path      = strdup(ctx->blocks_path);
  files[0].offset    = ctx->blocks_offset;
  files[0].limit     = 32;
  files[0].data      = bytes(ctx->task->block.root, 32);
  files[1].path      = strdup(ctx->headers_path);
  files[1].offset    = ctx->headers_offset;
  files[1].limit     = HEADER_SIZE;
  files[1].data      = bytes(ctx->task->block.header, HEADER_SIZE);

  // Schedule write; c4_ps_finish_write will free ctx and proceed
  int rc = c4_write_files_uv(ctx, ps_write_done_cb, files, 2, O_RDWR | O_CREAT, 0666);
  if (rc < 0) {
    log_error("period_store: c4_write_files_uv scheduling failed");
    c4_ps_finish_write(ctx, false);
    // free allocated paths and the files array on failure (callback won't run)
    c4_file_data_array_free(files, 2, 0);
  }
  else {
    // util made its own copy of the array; free only the container to avoid leaking
    safe_free(files);
  }
}

void c4_ps_set_block(block_t* block, bool run_backfill) {
  period_data_t* period_data = NULL;
  uint64_t       period      = block->slot / SLOTS_PER_PERIOD;
  uint64_t       idx         = block->slot % SLOTS_PER_PERIOD;

  write_task_t* task = (write_task_t*) safe_calloc(1, sizeof(write_task_t));
  task->block        = *block;
  task->run_backfill = run_backfill;
  task->next         = NULL;
  if (!queue.tail)
    queue.head = queue.tail = task;
  else {
    queue.tail->next = task;
    queue.tail       = task;
  }

  // update the period data in cache if available
  if (bf_ctx.current_period.period == period)
    period_data = &bf_ctx.current_period;
  else if (bf_ctx.previous_period.period == period)
    period_data = &bf_ctx.previous_period;
  if (period_data) {
    memcpy(period_data->blocks + idx * 32, task->block.root, 32);
    memcpy(period_data->headers + idx * HEADER_SIZE, task->block.header, HEADER_SIZE);
  }

  // update LCU for the previous period
  if (!run_backfill) {
    static uint64_t last_head_period = UINT64_MAX;
    if (last_head_period == UINT64_MAX) {
      last_head_period = period;
    }
    else if (period != last_head_period) {
      if (period > 0) {
        log_info("period_store: period changed (%l -> %l), refresh LCU for period %l", last_head_period, period, period - 1);
        c4_ps_schedule_fetch_lcu(period - 1);
      }
      last_head_period = period;
    }
  }

  if (queue.head == queue.tail)    // this means we have only one task in the queue
    c4_ps_run_write_block_queue(); // if there are more it will be handled after the first is finished.
}

typedef struct {
  period_data_t* out;
  uint64_t       period;
} period_read_done_ctx_t;

static void read_period_done(void* user_data, file_data_t* files, int num_files) {
  period_read_done_ctx_t* done        = (period_read_done_ctx_t*) user_data;
  period_data_t*          pd          = done->out;
  uint64_t                p           = done->period;
  size_t                  blocks_len  = 32 * SLOTS_PER_PERIOD;
  size_t                  headers_len = HEADER_SIZE * SLOTS_PER_PERIOD;
  // blocks
  if (num_files > 0 && files[0].error == NULL && files[0].data.data && files[0].data.len > 0) {
    pd->blocks  = (uint8_t*) safe_calloc(1, blocks_len);
    size_t copy = files[0].data.len < blocks_len ? files[0].data.len : blocks_len;
    memcpy(pd->blocks, files[0].data.data, copy);
  }
  else {
    pd->blocks = (uint8_t*) safe_calloc(1, blocks_len);
    if (num_files > 0 && files[0].error)
      log_warn("period_store: could not read blocks: %s", files[0].error);
  }
  // headers
  if (num_files > 1 && files[1].error == NULL && files[1].data.data && files[1].data.len > 0) {
    if (files[1].data.len == headers_len) {
      // take ownership of the buffer to avoid copying ~1MB
      pd->headers        = files[1].data.data;
      files[1].data.data = NULL;
      files[1].data.len  = 0;
    }
    else {
      pd->headers = (uint8_t*) safe_calloc(1, headers_len);
      size_t copy = files[1].data.len < headers_len ? files[1].data.len : headers_len;
      memcpy(pd->headers, files[1].data.data, copy);
    }
  }
  else {
    pd->headers = (uint8_t*) safe_calloc(1, headers_len);
    if (num_files > 1 && files[1].error)
      log_warn("period_store: could not read headers: %s", files[1].error);
  }
  // If the LCU is missing, trigger an asynchronous, non-blocking fetch.
  if (num_files > 2) {

    // check lc update
    if (files[2].error || files[2].data.len == 0) {
      if (files[2].error)
        log_info("period_store: lcu.ssz missing for period %l (%s) -> will fetch", p, files[2].error);
      else
        log_info("period_store: lcu.ssz empty for period %l -> will fetch", p);
      if (!graceful_shutdown_in_progress) {
        c4_ps_schedule_fetch_lcu(p);
      }
    }
  }

  if (num_files > 3) {

    // Check light client bootstrap (LCB).
    if ((files[3].error || files[3].data.len == 0) && c4_ps_file_exists(p - 1, "zk_proof_g16.bin")) {
      if (files[3].error)
        log_info("period_store: lcb.ssz missing for period %l (%s) -> will fetch", p, files[3].error);
      else
        log_info("period_store: lcb.ssz empty for period %l -> will fetch", p);
      if (!graceful_shutdown_in_progress) {
        c4_ps_schedule_fetch_lcb(p);
      }
    }
  }

  pd->period = p;
  // free temp results (also frees remaining buffers if not ownership-transferred)
  c4_file_data_array_free(files, num_files, 1);
  safe_free(done);
  static uint64_t last_logged = 0;
  uint64_t        ts          = current_ms();
  if (last_logged == 0 || ts - last_logged > 1000) {
    log_info("backfilling period %l", p);
    last_logged = ts;
  }

  // continue backfill
  if (!graceful_shutdown_in_progress)
    c4_ps_backfill();
}

// backfill implementation moved to period_store_backfill.c

void c4_period_sync_on_head(uint64_t slot, const uint8_t block_root[32], const uint8_t header112[112]) {
  if (!eth_config.period_store) return;
  if (slot > latest_head_slot) latest_head_slot = slot;
  block_t block = {.slot = slot};
  memcpy(block.root, block_root, 32);
  memcpy(block.header, header112, 112);
  memcpy(block.parent_root, header112 + 16, 32);
  c4_ps_set_block(&block, false);
}

void c4_period_sync_on_checkpoint(bytes32_t checkpoint, uint64_t slot) {
  uint64_t period = slot >> 13;
  if (!c4_ps_file_exists(period, "lcb.ssz"))
    c4_ps_fetch_lcb_for_checkpoint(checkpoint, period);
  if (!c4_ps_file_exists(period, "lcu.ssz"))
    c4_ps_schedule_fetch_lcu(period);
  if (!c4_ps_file_exists(period, "historical_root.json"))
    c4_ps_schedule_fetch_historical_root(period);

  c4_period_prover_on_checkpoint(period);
}
