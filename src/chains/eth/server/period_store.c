
#include "period_store.h"
#include "bytes.h"
#include "json.h"
#include "logger.h"
#include "server.h"
#include "ssz.h"
#include "state.h"
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

typedef void (*header_cb)(client_t*, void* data, data_request_t*);

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

static backfill_ctx_t    bf_ctx = {0};
static write_queue_ctx_t queue  = {0};

static void backfill();
static void run_write_block_queue();

static void backfill_done() {
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
      bf_ctx.end_slot   = bf_ctx.start_slot;
      bf_ctx.start_slot = head->slot;
      bf_ctx.done       = false;
      bf_ctx.current    = *head;
      memcpy(bf_ctx.current.parent_root, head->header + 16, 32);
      backfill();
    }
  }
  else if (bf_ctx.start_slot == 0) {
    // we have not started anything yet.
    // let's start from the head down to the configured max periods.
    uint64_t max_periods = http_server.period_backfill_max_periods > 0 ? http_server.period_backfill_max_periods : 2;
    bf_ctx.start_slot    = head->slot;
    bf_ctx.end_slot      = head->slot - (head->slot % SLOTS_PER_PERIOD) - (SLOTS_PER_PERIOD * max_periods);
    bf_ctx.done          = false;
    bf_ctx.current       = *head;
    memcpy(bf_ctx.current.parent_root, head->header + 16, 32);
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
  return NULL;
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

  if (call_backfill) backfill();
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

  // Open/create blocks first
  uv_fs_t* o = (uv_fs_t*) safe_calloc(1, sizeof(uv_fs_t));
  o->data    = ctx;
  UVX_CHECK("uv_fs_open(blocks)",
            uv_fs_open(uv_default_loop(), o, ctx->blocks_path, O_RDWR | O_CREAT, 0666, on_open_blocks),
            uv_fs_req_cleanup(o);
            safe_free(o),
            ps_finish_write(ctx, false);
            return);
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
  static client_t bf_client = {0};
  bf_client.being_closed    = false;

  data_request_t* req = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
  req->url            = root ? bprintf(NULL, "eth/v1/beacon/headers/%s", root) : strdup("eth/v1/beacon/headers/head");
  req->method         = C4_DATA_METHOD_GET;
  req->chain_id       = http_server.chain_id;
  req->type           = C4_DATA_TYPE_BEACON_API;
  req->encoding       = C4_DATA_ENCODING_JSON;
  c4_add_request(&bf_client, req, target, fetch_header_cb);
}

static void read_period(uint64_t period, period_data_t* period_data) {
  char* period_path  = ensure_period_dir(period);
  char* headers_path = bprintf(NULL, "%s/headers.ssz", period_path);
  char* blocks_path  = bprintf(NULL, "%s/blocks.ssz", period_path);

  bytes_t blocks = bytes_read(blocks_path);
  if (blocks.data == NULL || blocks.len != 32 * SLOTS_PER_PERIOD) { // doesn't exist
    period_data->blocks = safe_calloc(1, 32 * SLOTS_PER_PERIOD);
    if (blocks.data) {
      memcpy(period_data->blocks, blocks.data, blocks.len);
      safe_free(blocks.data);
    }
  }
  else
    period_data->blocks = blocks.data;

  bytes_t headers = bytes_read(headers_path);
  if (headers.data == NULL || headers.len != HEADER_SIZE * SLOTS_PER_PERIOD) { // doesn't exist
    period_data->headers = safe_calloc(HEADER_SIZE, SLOTS_PER_PERIOD);
    if (headers.data) {
      memcpy(period_data->headers, headers.data, headers.len);
      safe_free(headers.data);
    }
  }
  else
    period_data->headers = headers.data;

  // TODO instead of reading the file with fopen, we should use libuv
  // and once we have read it we go back and call backfill() again.
  backfill();
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
  block_t block = {0};
  for (uint64_t i = current->slot - 1; i >= 0; i++) {
    if (!read_block(i, &block)) return false;           // this means, we need to read the period first.
    if (memcmp(block.root, current->parent_root, 32)) { // this means there is no block at this slot.

      // this is not the block root we expected!
      if (!bytes_all_zero(bytes(block.root, 32)))
        log_warn("period_store: block root mismatch at slot %l: expected %x, got %x. Will fix it!", i, bytes(current->parent_root, 32), bytes(block.root, 32));

      // TODO use http_server.period_backfill_delay_ms to make sure
      //  we are not running into rate-limit issues

      // let's fetch the header for the parent block
      fetch_header(current->parent_root, result);
      return false;
    }
    else if (bytes_all_zero(bytes(block.header, HEADER_SIZE))) {
      // this is ok, it means for this slot there is no block.
      // continue to the next slot.
      continue;
    }
    else {
      *result = block;
      return true;
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
  block_t block = {.slot = slot};
  memcpy(block.root, block_root, 32);
  memcpy(block.header, header112, 112);
  memcpy(block.parent_root, header112 + 16, 32);
  set_block(&bf_ctx.current, false);
}
