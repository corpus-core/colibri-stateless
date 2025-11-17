
#include "period_store.h"
#include "bytes.h"
#include "json.h"
#include "logger.h"
#include "server.h"
#include "ssz.h"
#include "state.h"
#include "uv_util.h"
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <uv.h>

// Macro to log libuv errors, perform cleanup and return with a custom statement
#define UVX_CHECK(op, expr, cleanup, retstmt)                                                  \
  do {                                                                                         \
    int _rc = (expr);                                                                          \
    if (_rc < 0) {                                                                             \
      log_error("period_store: %s failed: %s (%s)", (op), uv_strerror(_rc), uv_err_name(_rc)); \
      cleanup;                                                                                 \
      retstmt;                                                                                 \
    }                                                                                          \
  } while (0)

#define HEADER_SCHEMA    "{data:{root:bytes32,header:{message:{slot:suint,proposer_index:suint,parent_root:bytes32,state_root:bytes32,body_root:bytes32}}}}"
#define SLOTS_PER_PERIOD 8192u
#define HEADER_SIZE      112

typedef struct {
  uint64_t  slot;
  bytes32_t root;
  uint8_t   header[HEADER_SIZE];
  bytes32_t parent_root;
} block_t;
typedef struct {
  uint64_t period;  // the period for blocks and headers
  uint8_t* blocks;  // 8192 * 32 bytes block roots
  uint8_t* headers; // 8192 * 112 bytes headers
} period_data_t;

typedef struct {
  block_t current; // current block, which is moving backwards while backfilling
  block_t parent;  // this is only set, when the parent is missing and was fetched.

  period_data_t current_period;
  period_data_t previous_period;

  uint64_t started_ts;
  uint64_t end_slot;
  uint64_t start_slot;
  bool     done;

} backfill_ctx_t;

typedef struct write_task_s {
  block_t              block;
  bool                 run_backfill;
  struct write_task_s* next;
} write_task_t;

typedef struct {
  char*         base_dir;
  uint64_t      last_checked_period;
  write_task_t* head;
  write_task_t* tail;
} write_queue_ctx_t;

typedef struct {
  // lifecycle context across async fs ops
  write_task_t* task;
  char*         blocks_path;
  char*         headers_path;
  uv_file       blocks_fd;
  uv_file       headers_fd;
  uint64_t      blocks_offset;
  uint64_t      headers_offset;
  buffer_t      tmp;
} fs_ctx_t;

// forward declaration
static void     ps_finish_write(fs_ctx_t* ctx, bool ok);
static void     backfill();
static void     run_write_block_queue();
static void     enqueue_backfill(void);
static uint64_t latest_head_slot = 0;
static void     fetch_header_cb(client_t* client, void* data, data_request_t* r);
// LCU forward declarations
static void schedule_fetch_lcu(uint64_t period);
static void fetch_lcu_cb(client_t* client, void* data, data_request_t* r);
static void lcu_write_done_cb(void* user_data, file_data_t* files, int num_files);

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
  ps_finish_write(ctx, ok);
}
static backfill_ctx_t    bf_ctx = {0};
static write_queue_ctx_t queue  = {0};

// Delayed header fetch (rate-limit friendly)
typedef struct {
  uint8_t  root[32];
  block_t* target;
  bool     use_head;
} fetch_ctx_t;
static uv_timer_t fetch_timer;
static bool       fetch_timer_initialized = false;
static void       fetch_timer_cb(uv_timer_t* h) {
  fetch_ctx_t*    fc        = (fetch_ctx_t*) h->data;
  static client_t bf_client = {0};
  bf_client.being_closed    = false;
  data_request_t* req       = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
  req->url                  = fc->use_head ? strdup("eth/v1/beacon/headers/head")
                                                 : bprintf(NULL, "eth/v1/beacon/headers/0x%x", bytes(fc->root, 32));
  req->method               = C4_DATA_METHOD_GET;
  req->chain_id             = http_server.chain_id;
  req->type                 = C4_DATA_TYPE_BEACON_API;
  req->encoding             = C4_DATA_ENCODING_JSON;
  c4_add_request(&bf_client, req, fc->target, fetch_header_cb);
  uv_timer_stop(h);
  safe_free(fc);
}

static uv_timer_t backfill_timer;
static bool       backfill_timer_initialized = false;

static void backfill_timer_cb(uv_timer_t* handle) {
  backfill();
}

static void enqueue_backfill(void) {
  if (!backfill_timer_initialized) {
    int rc = uv_timer_init(uv_default_loop(), &backfill_timer);
    if (rc < 0) {
      log_error("period_store: uv_timer_init failed: %s (%s)", uv_strerror(rc), uv_err_name(rc));
      return;
    }
    backfill_timer_initialized = true;
  }
  if (uv_is_active((uv_handle_t*) &backfill_timer)) return;
  int rc = uv_timer_start(&backfill_timer, backfill_timer_cb, 0, 0);
  if (rc < 0) {
    log_error("period_store: uv_timer_start failed: %s (%s)", uv_strerror(rc), uv_err_name(rc));
  }
}

static void backfill_done() {
  uint64_t duration_ms    = current_ms() - bf_ctx.started_ts;
  uint64_t duration_s     = duration_ms / 1000;
  uint64_t duration_min   = duration_s / 60;
  uint64_t duration_hours = duration_min / 60;
  if (duration_hours > 0)
    log_info(YELLOW("period_store:") " backfill done at slot %l in " GREEN("%l hours and %l min"), bf_ctx.current.slot, duration_hours, duration_min % 60);
  else if (duration_min > 0)
    log_info(YELLOW("period_store:") " backfill done at slot %l in " GREEN("%l min and %l s"), bf_ctx.current.slot, duration_min, duration_s % 60);
  else if (duration_s > 0)
    log_info(YELLOW("period_store:") " backfill done at slot %l in " GREEN("%l s and %l ms"), bf_ctx.current.slot, duration_s, duration_ms % 1000);
  else
    log_info(YELLOW("period_store:") " backfill done at slot %l in " GREEN("%l ms"), bf_ctx.current.slot, duration_ms);

  bf_ctx.done = true;
  safe_free(bf_ctx.current_period.blocks);
  safe_free(bf_ctx.current_period.headers);
  safe_free(bf_ctx.previous_period.blocks);
  safe_free(bf_ctx.previous_period.headers);
  memset(&bf_ctx.current_period, 0, sizeof(period_data_t));
  memset(&bf_ctx.previous_period, 0, sizeof(period_data_t));
  memset(&bf_ctx.current, 0, sizeof(block_t));
  memset(&bf_ctx.parent, 0, sizeof(block_t));
}

static void backfill_check(block_t* head) {
  if (bf_ctx.done) {
    // should we rerun the backfill?
    // after 100 slots
    if (head->slot > bf_ctx.start_slot && head->slot - bf_ctx.start_slot > 100) {
      log_info("period_store: backfill restart from %l to %l", head->slot, bf_ctx.start_slot);
      bf_ctx.started_ts = current_ms();
      bf_ctx.end_slot   = bf_ctx.start_slot;
      bf_ctx.start_slot = head->slot;
      bf_ctx.done       = false;
      bf_ctx.current    = *head;
      memcpy(bf_ctx.current.parent_root, head->header + 16, 32);
      http_server.stats.period_sync_retries_total++;
      backfill();
    }
  }
  else if (bf_ctx.start_slot == 0) {
    // we have not started anything yet.
    // let's start from the head down to the configured max periods.
    uint64_t max_periods = http_server.period_backfill_max_periods > 0 ? http_server.period_backfill_max_periods : 2;
    bf_ctx.started_ts    = current_ms();
    bf_ctx.start_slot    = head->slot;
    bf_ctx.end_slot      = head->slot - (head->slot % SLOTS_PER_PERIOD) - (SLOTS_PER_PERIOD * max_periods);
    bf_ctx.done          = false;
    bf_ctx.current       = *head;
    memcpy(bf_ctx.current.parent_root, head->header + 16, 32);
    http_server.stats.period_sync_retries_total++;
    log_info("period_store: backfill start [%l -> %l)", bf_ctx.start_slot, bf_ctx.end_slot);
    backfill();
  }
}

/** converts the response to a header_t and cleans up the request object */
static char* response_to_header(data_request_t* r, block_t* header) {
  char* err = NULL;
  if (!r->response.data && !r->error) r->error = strdup("unknown error!");
  if (r->error) {
    err = r->error;
    safe_free(r->url);
    safe_free(r->response.data);
    safe_free(r);
    return err;
  }
  if (!r->response.data) return strdup("empty response!");

  json_t js = json_parse((char*) r->response.data);
  err       = (char*) json_validate(js, HEADER_SCHEMA, "validating beacon header");
  if (err) {
    safe_free(r->url);
    safe_free(r->response.data);
    safe_free(r);
    return err;
  }
  json_t dataj = json_get(js, "data");
  json_t msg   = json_get(json_get(dataj, "header"), "message");
  // slot, proposer, parent,state,body roots
  header->slot = json_as_uint64(json_get(msg, "slot"));
  uint64_to_le(header->header + 0, header->slot);
  uint64_to_le(header->header + 8, json_as_uint64(json_get(msg, "proposer_index")));
  json_to_bytes(json_get(msg, "parent_root"), bytes(header->header + 16, 32));
  json_to_bytes(json_get(msg, "state_root"), bytes(header->header + 48, 32));
  json_to_bytes(json_get(msg, "body_root"), bytes(header->header + 80, 32));
  json_to_bytes(json_get(dataj, "root"), bytes(header->root, 32));
  memcpy(header->parent_root, header->header + 16, 32);
  safe_free(r->url);
  safe_free(r->response.data);
  safe_free(r);
  return NULL; // no error
}

static char* ensure_period_dir(uint64_t period) {
  uv_fs_t req = {};
  if (!queue.base_dir) {
    int rc = uv_fs_mkdir(uv_default_loop(), &req, http_server.period_store, 0777, NULL);
    if (rc < 0 && req.result != UV_EEXIST)
      log_warn("period_store: mkdir failed for %s: %s", http_server.period_store, uv_strerror((int) req.result));
    else
      queue.base_dir = http_server.period_store;
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

// After both files written, close FDs and finish
static void ps_finish_write(fs_ctx_t* ctx, bool ok) {
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

  write_task_t* task          = ctx->task;
  bool          call_backfill = task->run_backfill;
  bool          call_next     = task->next != NULL;

  // remove from queue
  if (task == queue.head) {
    queue.head = task->next;
    if (!queue.head) queue.tail = NULL;
  }
  if (http_server.stats.period_sync_queue_depth > 0) http_server.stats.period_sync_queue_depth--;

  // !call_back means this came from the beacon_events
  // and means we need to check, if we should start backfilling.
  if (!call_backfill)
    backfill_check(&task->block);
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

  // free task and context
  safe_free(ctx->task);
  safe_free(ctx->blocks_path);
  safe_free(ctx->headers_path);
  buffer_free(&ctx->tmp);
  safe_free(ctx);

  // metrics and logging
  if (ok) {
    http_server.stats.period_sync_last_slot    = task->block.slot;
    http_server.stats.period_sync_last_slot_ts = current_unix_ms();
    if (task->run_backfill)
      http_server.stats.period_sync_backfilled_slots_total++;
    else
      http_server.stats.period_sync_written_slots_total++;
    if (latest_head_slot >= http_server.stats.period_sync_last_slot)
      http_server.stats.period_sync_lag_slots = latest_head_slot - http_server.stats.period_sync_last_slot;
    else
      http_server.stats.period_sync_lag_slots = 0;
    log_debug("period_store: wrote slot %l (%s)", task->block.slot, task->run_backfill ? "backfill" : "head");
  }
  else {
    http_server.stats.period_sync_errors_total++;
    log_warn("period_store: write failed");
  }

  if (call_backfill) {
    log_debug("period_store: continue backfill after write");
    backfill();
  }
  if (call_next) run_write_block_queue();
}

static void on_write_headers_done(uv_fs_t* req) {
  fs_ctx_t* ctx = (fs_ctx_t*) req->data;
  bool      ok  = req->result >= 0;
  uv_fs_req_cleanup(req);
  safe_free(req);
  ps_finish_write(ctx, ok);
}

static void on_open_headers(uv_fs_t* req) {
  fs_ctx_t* ctx = (fs_ctx_t*) req->data;
  if (req->result < 0) {
    log_error("period_store: open headers failed: %s", uv_strerror((int) req->result));
    uv_fs_req_cleanup(req);
    safe_free(req);
    ps_finish_write(ctx, false);
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
            ps_finish_write(ctx, false);
            return);
}

static void on_write_blocks_done(uv_fs_t* req) {
  fs_ctx_t* ctx = (fs_ctx_t*) req->data;
  if (req->result < 0) {
    log_error("period_store: write blocks failed: %s", uv_strerror((int) req->result));
    uv_fs_req_cleanup(req);
    safe_free(req);
    ps_finish_write(ctx, false);
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
            ps_finish_write(ctx, false);
            return);
}

static void on_open_blocks(uv_fs_t* req) {
  fs_ctx_t* ctx = (fs_ctx_t*) req->data;
  if (req->result < 0) {
    log_error("period_store: open blocks failed: %s", uv_strerror((int) req->result));
    uv_fs_req_cleanup(req);
    safe_free(req);
    ps_finish_write(ctx, false);
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
            ps_finish_write(ctx, false);
            return);
}

// execute the head and if successfull, we schedule the next one.
static void run_write_block_queue() {
  if (!queue.head) return;
  write_task_t* task     = queue.head;
  uint64_t      period   = task->block.slot / SLOTS_PER_PERIOD;
  uint64_t      idx      = task->block.slot % SLOTS_PER_PERIOD;
  char*         dir_path = ensure_period_dir(period);

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

  // Schedule write; ps_finish_write will free ctx and proceed
  int rc = c4_write_files_uv(ctx, ps_write_done_cb, files, 2, O_RDWR | O_CREAT, 0666);
  if (rc < 0) {
    log_error("period_store: c4_write_files_uv scheduling failed");
    ps_finish_write(ctx, false);
    // free allocated paths and the files array on failure (callback won't run)
    c4_file_data_array_free(files, 2, 0);
  }
}

static void set_block(block_t* block, bool run_backfill) {
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
        schedule_fetch_lcu(period - 1);
      }
      last_head_period = period;
    }
  }

  if (queue.head == queue.tail) // this means we have only one task in the queue
    run_write_block_queue();    // if there are more it will be handled after the first is finished.
}

static void fetch_header_cb(client_t* client, void* data, data_request_t* r) {
  block_t block;
  char*   err = response_to_header(r, &block);
  if (err) {
    log_error("backfill failed: error while fetching header: %s", err);
    safe_free(err);
    backfill_done();
    return;
  }
  block_t* target = (block_t*) data;
  *target         = block;

  set_block(target, true);
}

static void fetch_header(uint8_t* root, block_t* target) {
  // Optional pacing to avoid rate-limits
  int delay_ms = http_server.period_backfill_delay_ms;
  if (delay_ms > 0) {
    if (!fetch_timer_initialized) {
      if (uv_timer_init(uv_default_loop(), &fetch_timer) != 0) {
        log_warn("period_store: uv_timer_init(fetch) failed; sending immediately");
        delay_ms = 0;
      }
      else {
        fetch_timer_initialized = true;
      }
    }
    if (delay_ms > 0) {
      fetch_ctx_t* fc = (fetch_ctx_t*) safe_calloc(1, sizeof(fetch_ctx_t));
      fc->target      = target;
      fc->use_head    = (root == NULL);
      if (root) memcpy(fc->root, root, 32);
      fetch_timer.data = fc;
      int rc           = uv_timer_start(&fetch_timer, fetch_timer_cb, (uint64_t) delay_ms, 0);
      if (rc < 0) {
        log_warn("period_store: uv_timer_start(fetch) failed; sending immediately: %s", uv_strerror(rc));
        delay_ms = 0;
      }
      if (delay_ms > 0) return;
    }
  }
  // Immediate send if no delay configured or timer failed
  static client_t bf_client = {0};
  bf_client.being_closed    = false;
  data_request_t* req       = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
  req->url                  = root ? bprintf(NULL, "eth/v1/beacon/headers/0x%x", bytes(root, 32)) : strdup("eth/v1/beacon/headers/head");
  req->method               = C4_DATA_METHOD_GET;
  req->chain_id             = http_server.chain_id;
  req->type                 = C4_DATA_TYPE_BEACON_API;
  req->encoding             = C4_DATA_ENCODING_JSON;
  c4_add_request(&bf_client, req, target, fetch_header_cb);
}

// ---- LightClientUpdate (LCU) fetch/write ----
typedef struct {
  uint64_t        period;
  data_request_t* req; // kept alive until write finishes
} lcu_write_ctx_t;

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
  // free request and its buffer now that write is done
  if (ctx->req) {
    safe_free(ctx->req->url);
    safe_free(ctx->req->response.data);
    safe_free(ctx->req);
  }
  safe_free(ctx);
}

static void fetch_lcu_cb(client_t* client, void* data, data_request_t* r) {
  (void) client;
  uint64_t period = data ? *((uint64_t*) data) : 0;
  safe_free(data);
  if (!r->response.data && !r->error) r->error = strdup("unknown error!");
  if (r->error) {
    log_warn("period_store: LCU fetch for period %l failed: %s", period, r->error);
    safe_free(r->url);
    safe_free(r->response.data);
    safe_free(r);
    return;
  }
  // prepare async write of lcu.ssz
  char* dir  = ensure_period_dir(period);
  char* path = bprintf(NULL, "%s/lcu.ssz", dir);
  safe_free(dir);
  file_data_t* files = (file_data_t*) safe_calloc(1, sizeof(file_data_t));
  files[0].path      = path;
  files[0].offset    = 0;
  files[0].limit     = r->response.len; // write all bytes
  files[0].data      = r->response;
  // keep request alive until write completes
  lcu_write_ctx_t* wctx = (lcu_write_ctx_t*) safe_calloc(1, sizeof(lcu_write_ctx_t));
  wctx->period          = period;
  wctx->req             = r;
  int rc                = c4_write_files_uv(wctx, lcu_write_done_cb, files, 1, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (rc < 0) {
    log_warn("period_store: scheduling LCU write failed for period %l", period);
    c4_file_data_array_free(files, 1, 0);
    // free request as we won't receive callback
    safe_free(r->url);
    safe_free(r->response.data);
    safe_free(r);
    safe_free(wctx);
  }
}

static void schedule_fetch_lcu(uint64_t period) {
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
  // lcu: wenn nicht vorhanden, Fetch asynchron anstoßen (nicht blockierend)
  if (num_files > 2) {
    if (files[2].error || files[2].data.len == 0) {
      if (files[2].error)
        log_info("period_store: lcu.ssz missing for period %l (%s) -> will fetch", p, files[2].error);
      else
        log_info("period_store: lcu.ssz empty for period %l -> will fetch", p);
      schedule_fetch_lcu(p);
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
  backfill();
}

static void read_period(uint64_t period, period_data_t* period_data) {
  char*       dir  = ensure_period_dir(period);
  file_data_t f[3] = {0};
  f[0].path        = bprintf(NULL, "%s/blocks.ssz", dir);
  f[0].offset      = 0;
  f[0].limit       = 32 * SLOTS_PER_PERIOD;
  f[1].path        = bprintf(NULL, "%s/headers.ssz", dir);
  f[1].offset      = 0;
  f[1].limit       = HEADER_SIZE * SLOTS_PER_PERIOD;
  f[2].path        = bprintf(NULL, "%s/lcu.ssz", dir);
  f[2].offset      = 0;
  f[2].limit       = 0; // read all
  safe_free(dir);
  period_read_done_ctx_t* done = (period_read_done_ctx_t*) safe_calloc(1, sizeof(period_read_done_ctx_t));
  done->out                    = period_data;
  done->period                 = period;
  c4_read_files_uv(done, read_period_done, f, 3);
}

static inline bool read_block(uint64_t slot, block_t* result) {
  uint64_t       period      = slot / SLOTS_PER_PERIOD;
  uint64_t       idx         = slot % SLOTS_PER_PERIOD;
  uint64_t       off_b       = (uint64_t) idx * 32;
  uint64_t       off_h       = (uint64_t) idx * HEADER_SIZE;
  period_data_t* period_data = NULL;
  if (period == bf_ctx.current_period.period)
    period_data = &bf_ctx.current_period;
  else if (period == bf_ctx.previous_period.period)
    period_data = &bf_ctx.previous_period;
  else {
    // move the current period to the previous period and read the new period
    if (bf_ctx.previous_period.blocks) safe_free(bf_ctx.previous_period.blocks);
    if (bf_ctx.previous_period.headers) safe_free(bf_ctx.previous_period.headers);
    memcpy(&bf_ctx.previous_period, &bf_ctx.current_period, sizeof(period_data_t));
    memset(&bf_ctx.current_period, 0, sizeof(period_data_t));
    read_period(period, &bf_ctx.current_period);
    return false;
  }

  memcpy(result->root, period_data->blocks + off_b, 32);
  memcpy(result->header, period_data->headers + off_h, HEADER_SIZE);
  memcpy(result->parent_root, result->header + 16, 32);
  result->slot = slot;

  return true;
}
static inline bool read_parent_block(block_t* current, block_t* result) {
  block_t         block               = {0};
  static uint32_t scanned_since_yield = 0;
  // count down safely to avoid uint64 underflow and respect end_slot boundary
  for (uint64_t i = current->slot; i >= bf_ctx.end_slot; i--) {
    uint64_t s = i - 1;
    if (!read_block(s, &block)) return false;           // if false, this means, we need to read the period first.
    if (memcmp(block.root, current->parent_root, 32)) { // the root must always match the parent root

      // this means we skipped at least one slot,
      // because the header was empty.
      // if this does no match, it means that the header is missing even though
      // there was a block, so we need to fetch the header for the parent block.
      if (current->slot - s > 1)
        log_warn("period_store: block header missing at slot %l: will be fetched", i);

      // this is not the block root we expected!
      else if (!bytes_all_zero(bytes(block.root, 32)))
        log_warn("period_store: block root mismatch at slot %l: expected %x, got %x. Will fix it!", s, bytes(current->parent_root, 32), bytes(block.root, 32));

      // let's fetch the header for the parent block
      fetch_header(current->parent_root, &bf_ctx.parent);
      return false;
    }
    else if (bytes_all_zero(bytes(block.header, HEADER_SIZE))) {
      // no header means this slot was skipped, so we continue to the next slot to find our parent.
      continue;
    }
    else {
      *result = block;
      return true;
    }
    // Yield every 100 scanned slots to avoid blocking the event loop
    if ((++scanned_since_yield % 100) == 0) {
      enqueue_backfill(); // schedule continuation on the event loop
      return false;
    }
  }
  log_error("period_store: no parent block found for slot %l", current->slot);
  backfill_done();
  return false;
}

static void backfill() {
  block_t next_current = {0};

  if (bf_ctx.done) return;

  // make sure, we have a current block
  if (bf_ctx.current.slot == 0) {
    // we need to fecth the head first
    fetch_header(NULL, &bf_ctx.current);
    return;
  }

  if (bf_ctx.parent.slot) {
    // we have a parent block, so let's handle this first.
    // the parent slot itself is automaticly set from fetching,
    // but let us check, if there are skipped slots.
    bool wait_for_set_block = false;
    for (uint64_t i = bf_ctx.parent.slot + 1; i < bf_ctx.current.slot; i++) {
      // we set all the blocks between the parent and current with the same block root,
      // but empty header since at those slots there are no blocks.
      block_t block = {0};
      memcpy(block.parent_root, bf_ctx.parent.parent_root, 32);
      memcpy(block.root, bf_ctx.parent.root, 32);
      block.slot = i;
      set_block(&block, true);
      wait_for_set_block = true;
    }
    // remove the parent block, because we already checked it.

    bf_ctx.current = bf_ctx.parent;
    memset(&bf_ctx.parent, 0, sizeof(block_t));
    if (wait_for_set_block) return; // now we wait until those blocks have been written.
  }

  while (true) {
    if (!read_parent_block(&bf_ctx.current, &next_current))
      return; // false means we can't continue, because either a block is fetched or a period read from disk

    if (next_current.slot <= bf_ctx.end_slot) {
      // we are done!
      backfill_done();
      return;
    }

    // all is good so
    bf_ctx.current = next_current;
  }
}

void c4_period_sync_on_head(uint64_t slot, const uint8_t block_root[32], const uint8_t header112[112]) {
  if (!http_server.period_store) return;
  if (slot > latest_head_slot) latest_head_slot = slot;
  block_t block = {.slot = slot};
  memcpy(block.root, block_root, 32);
  memcpy(block.header, header112, 112);
  memcpy(block.parent_root, header112 + 16, 32);
  set_block(&block, false);
}
