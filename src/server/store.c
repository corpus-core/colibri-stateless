#include "server.h"
#include <fcntl.h>  // für O_RDONLY
#include <stdlib.h> // für malloc, free, realloc
#include <string.h> // für memcpy

// Vorwärtsdeklarationen der Callbacks
static void on_store_open(uv_fs_t* req);
static void on_store_read(uv_fs_t* req);
static void on_store_close(uv_fs_t* req);

// Struktur zur Kapselung des Zustands für eine Leseoperation
typedef struct {
  uv_fs_t               open_req;
  uv_fs_t               read_req;
  uv_fs_t               close_req;
  uv_buf_t              iov;            // Aktueller Lesepuffer für uv_fs_read
  char                  read_buf[1024]; // Fester Puffer für uv_fs_read
  buffer_t              data;
  int                   fd;       // Dateideskriptor
  void*                 user_ptr; // Benutzerdefinierter Zeiger
  handle_stored_data_cb user_cb;  // Benutzerdefinierter Callback
  char*                 file_path;
  uint64_t              period;
} store_read_context_t;

static void free_store_read_context(store_read_context_t* ctx) {
  buffer_free(&ctx->data);
  free(ctx->file_path);
  free(ctx);
}

// --- Implementierung der Callbacks ---

// Callback nach dem Schließen der Datei
static void on_store_close(uv_fs_t* req) {
  store_read_context_t* ctx = req->data;
  if (req->result < 0)
    fprintf(stderr, "Fehler beim Schließen der Datei '%s': %s\n", ctx->file_path, uv_strerror((int) req->result));
  uv_fs_req_cleanup(req);
  free_store_read_context(ctx);
}

// Callback nach dem Lesen von Daten
static void on_store_read(uv_fs_t* req) {
  store_read_context_t* ctx = req->data;

  if (req->result < 0) {
    ctx->user_cb(ctx->user_ptr, ctx->period, NULL_BYTES, uv_strerror((int) req->result));
    uv_fs_close(req->loop, &ctx->close_req, ctx->fd, on_store_close);
    uv_fs_req_cleanup(req);
  }
  else if (req->result == 0) {
    ctx->user_cb(ctx->user_ptr, ctx->period, ctx->data.data, NULL);
    uv_fs_close(req->loop, &ctx->close_req, ctx->fd, on_store_close);
    uv_fs_req_cleanup(req);
  }
  else {
    buffer_append(&ctx->data, bytes(ctx->read_buf, (size_t) req->result));
    ctx->iov = uv_buf_init(ctx->read_buf, sizeof(ctx->read_buf));
    uv_fs_read(req->loop, &ctx->read_req, ctx->fd, &ctx->iov, 1, -1, on_store_read);
  }
}

// Callback nach dem Öffnen der Datei
static void on_store_open(uv_fs_t* req) {
  store_read_context_t* ctx = req->data;

  if (req->result >= 0) {
    ctx->fd            = req->result;
    ctx->read_req.data = ctx;
    ctx->iov           = uv_buf_init(ctx->read_buf, sizeof(ctx->read_buf));
    int r              = uv_fs_read(req->loop, &ctx->read_req, ctx->fd, &ctx->iov, 1, -1, on_store_read);
    if (r < 0) {
      ctx->user_cb(ctx->user_ptr, ctx->period, NULL_BYTES, uv_strerror(r)); // if we call the cb here, is there a chance it gets called again in the  on_close_store?
      uv_fs_close(req->loop, &ctx->close_req, ctx->fd, on_store_close);
    }
  }
  else {
    char* error = bprintf(NULL, "Error opening %s : %s", ctx->file_path, uv_strerror((int) req->result));
    ctx->user_cb(ctx->user_ptr, ctx->period, NULL_BYTES, error);
    safe_free(error);
    free_store_read_context(ctx);
  }
  uv_fs_req_cleanup(req);
}

bool c4_get_from_store_by_type(chain_id_t chain_id, uint64_t period, store_type_t type, uint32_t slot, void* uptr, handle_stored_data_cb cb) {
  if (!http_server.period_store) {
    cb(uptr, period, NULL_BYTES, "period_store not configured!");
    return false;
  }

  char* fname = NULL;

  switch (type) {
    case STORE_TYPE_BLOCK_HEADER:
      fname = "headers.ssz";
      break;
    case STORE_TYPE_BLOCK_ROOT:
    case STORE_TYPE_BLOCK_ROOTS:
      fname = "blocks.ssz";
      break;
    case STORE_TYPE_LCU:
      fname = "lcu.ssz";
      break;
    default:
      cb(uptr, period, NULL_BYTES, "invalid store_type!");
      return false;
  }

  char     tmp[200] = {0};
  buffer_t buf      = stack_buffer(tmp);
  return c4_get_from_store(bprintf(&buf, "%l/%d/%s", (uint64_t) chain_id, (uint32_t) period, fname), uptr, cb);
}

static uint32_t get_period_from_path(char* path) {
  char  tmp[50]        = {0};
  char* period_end_str = strrchr(path, '/');
  if (period_end_str == NULL || period_end_str == path)
    return 0;
  char* period_start_str = period_end_str - 1;
  while (period_start_str >= path && *period_start_str != '/')
    period_start_str--;

  if (period_start_str > path && period_start_str < period_end_str && period_start_str[0] == '/' && period_end_str - period_start_str < sizeof(tmp)) {
    memcpy(tmp, period_start_str + 1, period_end_str - period_start_str - 1);
    return atoi(tmp);
  }
  return 0;
}
bool c4_get_from_store(char* path, void* uptr, handle_stored_data_cb cb) {
  store_read_context_t* ctx = safe_calloc(1, sizeof(store_read_context_t));
  ctx->file_path            = bprintf(NULL, "%s/%s", http_server.period_store, path);
  ctx->fd                   = -1;
  ctx->period               = get_period_from_path(ctx->file_path);
  ctx->user_ptr             = uptr;
  ctx->user_cb              = cb;
  ctx->open_req.data        = ctx;
  ctx->read_req.data        = ctx;
  ctx->close_req.data       = ctx;

  int r = uv_fs_open(uv_default_loop(), &ctx->open_req, ctx->file_path, O_RDONLY, 0, on_store_open);
  if (r < 0) {
    cb(uptr, ctx->period, NULL_BYTES, uv_strerror(r));
    free_store_read_context(ctx);
    return false;
  }

  return true;
}