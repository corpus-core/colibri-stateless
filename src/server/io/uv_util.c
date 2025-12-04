#include "uv_util.h"
#include "bytes.h"
#include "logger.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#else
#include <direct.h>
#endif

#ifndef _WIN32
#define C4_MKDIR(path, mode) mkdir((path), (mode_t) (mode))
#define C4_DIR_MODE 0777
#else
#define C4_MKDIR(path, mode) (_mkdir(path))
#define C4_DIR_MODE 0
#endif

typedef struct read_one_ctx_s  read_one_ctx_t;
typedef struct write_one_ctx_s write_one_ctx_t;

typedef struct {
  void*             user_data;
  c4_read_files_cb  rcb;
  c4_write_files_cb wcb;
  file_data_t*      files;
  int               num_files;
  int               pending;
} files_op_ctx_t;

struct read_one_ctx_s {
  files_op_ctx_t* parent;
  file_data_t*    f;
  uv_file         fd;
  uv_fs_t         open_req;
  uv_fs_t         fstat_req;
  uv_fs_t         read_req;
  uv_fs_t         close_req;
  size_t          to_read;
  size_t          done;
};

static int c4_is_dot_path(const char* path) {
  if (!path) return 0;
  return (strcmp(path, ".") == 0 || strcmp(path, "..") == 0);
}

static int c4_is_root_path(const char* path) {
  if (!path) return 1;
  size_t len = strlen(path);
  if (len == 0) return 1;
#ifndef _WIN32
  return (len == 1 && path[0] == '/');
#else
  if (len == 1 && (path[0] == '/' || path[0] == '\\')) return 1;
  if (len == 2 && path[1] == ':') return 1;
  if (len == 3 && path[1] == ':' && (path[2] == '/' || path[2] == '\\')) return 1;
  return 0;
#endif
}

static int c4_mkpath(const char* path, int mode) {
  if (!path || *path == '\0') return 0;
  size_t len   = strlen(path);
  char*  tmp   = (char*) safe_calloc(len + 1, 1);
  char*  start = tmp;
  memcpy(tmp, path, len);
#ifdef _WIN32
  if (len >= 2 && tmp[1] == ':') start = tmp + 2;
#endif
  for (char* cursor = start; *cursor; ++cursor) {
    if (*cursor != '/' && *cursor != '\\') continue;
    char saved = *cursor;
    *cursor    = '\0';
    if (!c4_is_root_path(tmp) && !c4_is_dot_path(tmp)) {
      if (C4_MKDIR(tmp, mode) != 0 && errno != EEXIST) {
        int err = errno;
        safe_free(tmp);
        return -err;
      }
    }
    *cursor = saved;
    while (*(cursor + 1) == '/' || *(cursor + 1) == '\\') cursor++;
  }
  if (!c4_is_root_path(tmp) && !c4_is_dot_path(tmp)) {
    if (C4_MKDIR(tmp, mode) != 0 && errno != EEXIST) {
      int err = errno;
      safe_free(tmp);
      return -err;
    }
  }
  safe_free(tmp);
  return 0;
}

static int c4_ensure_parent_directory(const char* filepath, int mode) {
  if (!filepath) return -EINVAL;
  const char* slash = strrchr(filepath, '/');
#ifdef _WIN32
  const char* backslash = strrchr(filepath, '\\');
  if (!slash || (backslash && backslash > slash)) slash = backslash;
#endif
  if (!slash) return 0;
  size_t len = (size_t) (slash - filepath);
  if (len == 0) return 0;
  char* dir = (char*) safe_calloc(len + 1, 1);
  memcpy(dir, filepath, len);
  dir[len] = '\0';
  int rc   = c4_mkpath(dir, mode);
  safe_free(dir);
  return rc;
}

static void read_on_close(uv_fs_t* req) {
  read_one_ctx_t* ctx = (read_one_ctx_t*) req->data;
  uv_fs_req_cleanup(req);
  ctx->fd                = -1;
  files_op_ctx_t* parent = ctx->parent;
  // one file finished
  if (--parent->pending == 0) {
    if (parent->rcb) parent->rcb(parent->user_data, parent->files, parent->num_files);
    // no allocations for parent except owned by caller in file_data
    free(parent);
  }
  free(ctx);
}

static void read_on_read(uv_fs_t* req) {
  read_one_ctx_t* ctx = (read_one_ctx_t*) req->data;
  if (req->result < 0) {
    // error
    ctx->f->error = strdup(uv_strerror((int) req->result));
    if (ctx->f->data.data) free(ctx->f->data.data);
    ctx->f->data = NULL_BYTES;
    uv_fs_req_cleanup(req);
    // close and finish
    ctx->close_req.data = ctx;
    uv_fs_close(uv_default_loop(), &ctx->close_req, ctx->fd, read_on_close);
    return;
  }
  if (req->result == 0) {
    // EOF
    uv_fs_req_cleanup(req);
    ctx->close_req.data = ctx;
    uv_fs_close(uv_default_loop(), &ctx->close_req, ctx->fd, read_on_close);
    return;
  }
  ctx->done += (size_t) req->result;
  uv_fs_req_cleanup(req);
  if (ctx->done >= ctx->to_read) {
    // finish
    ctx->close_req.data = ctx;
    uv_fs_close(uv_default_loop(), &ctx->close_req, ctx->fd, read_on_close);
    return;
  }
  // continue reading
  size_t   remain    = ctx->to_read - ctx->done;
  uv_buf_t buf       = uv_buf_init((char*) (ctx->f->data.data + ctx->done), (unsigned int) remain);
  ctx->read_req.data = ctx;
  uv_fs_read(uv_default_loop(), &ctx->read_req, ctx->fd, &buf, 1, (int64_t) (ctx->f->offset + ctx->done), read_on_read);
}

static void read_on_fstat(uv_fs_t* req) {
  read_one_ctx_t* ctx = (read_one_ctx_t*) req->data;
  if (req->result < 0) {
    ctx->f->error = strdup(uv_strerror((int) req->result));
    uv_fs_req_cleanup(req);
    // close and finish
    ctx->close_req.data = ctx;
    uv_fs_close(uv_default_loop(), &ctx->close_req, ctx->fd, read_on_close);
    return;
  }
  // compute to_read
  size_t file_size = (size_t) req->statbuf.st_size;
  size_t avail     = file_size > ctx->f->offset ? (file_size - ctx->f->offset) : 0;
  ctx->to_read     = (ctx->f->limit == 0 || ctx->f->limit > avail) ? avail : ctx->f->limit;
  // allocate buffer
  if (ctx->to_read > 0) {
    ctx->f->data = bytes((uint8_t*) safe_calloc(1, ctx->to_read), (uint32_t) ctx->to_read);
    // start first read
    uv_buf_t buf       = uv_buf_init((char*) ctx->f->data.data, (unsigned int) ctx->to_read);
    ctx->read_req.data = ctx;
    uv_fs_read(uv_default_loop(), &ctx->read_req, ctx->fd, &buf, 1, (int64_t) ctx->f->offset, read_on_read);
  }
  else {
    // nothing to read
    ctx->f->data = NULL_BYTES;
    uv_fs_req_cleanup(req);
    ctx->close_req.data = ctx;
    uv_fs_close(uv_default_loop(), &ctx->close_req, ctx->fd, read_on_close);
  }
}

static void read_on_open(uv_fs_t* req) {
  read_one_ctx_t* ctx = (read_one_ctx_t*) req->data;
  if (req->result < 0) {
    ctx->f->error = strdup(uv_strerror((int) req->result));
    uv_fs_req_cleanup(req);
    // complete without fstat/read
    files_op_ctx_t* parent = ctx->parent;
    if (--parent->pending == 0) {
      if (parent->rcb) parent->rcb(parent->user_data, parent->files, parent->num_files);
      free(parent);
    }
    free(ctx);
    return;
  }
  ctx->fd             = (uv_file) req->result;
  ctx->fstat_req.data = ctx;
  uv_fs_req_cleanup(req);
  uv_fs_fstat(uv_default_loop(), &ctx->fstat_req, ctx->fd, read_on_fstat);
}

int c4_read_files_uv(void* user_data, c4_read_files_cb cb, file_data_t* files, int num_files) {
  if (num_files <= 0) return -1;
  files_op_ctx_t* parent = (files_op_ctx_t*) safe_calloc(1, sizeof(files_op_ctx_t));
  parent->user_data      = user_data;
  parent->rcb            = cb;
  // Make a heap copy of the files array to keep it valid until callback
  parent->files = (file_data_t*) safe_calloc((size_t) num_files, sizeof(file_data_t));
  memcpy(parent->files, files, (size_t) num_files * sizeof(file_data_t));
  parent->num_files = num_files;
  parent->pending   = num_files;
  for (int i = 0; i < num_files; i++) {
    read_one_ctx_t* ctx = (read_one_ctx_t*) safe_calloc(1, sizeof(read_one_ctx_t));
    ctx->parent         = parent;
    ctx->f              = &parent->files[i];
    ctx->fd             = -1;
    ctx->open_req.data  = ctx;
    uv_fs_open(uv_default_loop(), &ctx->open_req, ctx->f->path, O_RDONLY, 0, read_on_open);
  }
  return 0;
}

struct write_one_ctx_s {
  files_op_ctx_t* parent;
  file_data_t*    f;
  uv_file         fd;
  uv_fs_t         open_req;
  uv_fs_t         write_req;
  uv_fs_t         close_req;
  size_t          to_write;
  size_t          done;
  int             flags;
  int             mode;
};

static void write_ctx_finish(write_one_ctx_t* ctx) {
  files_op_ctx_t* parent = ctx->parent;
  if (--parent->pending == 0) {
    if (parent->wcb) parent->wcb(parent->user_data, parent->files, parent->num_files);
    free(parent);
  }
  free(ctx);
}

static void write_on_close(uv_fs_t* req) {
  write_one_ctx_t* ctx = (write_one_ctx_t*) req->data;
  uv_fs_req_cleanup(req);
  ctx->fd                = -1;
  // one file finished
  write_ctx_finish(ctx);
}

static void write_on_write(uv_fs_t* req) {
  write_one_ctx_t* ctx = (write_one_ctx_t*) req->data;
  if (req->result < 0) {
    ctx->f->error = strdup(uv_strerror((int) req->result));
    uv_fs_req_cleanup(req);
    ctx->close_req.data = ctx;
    uv_fs_close(uv_default_loop(), &ctx->close_req, ctx->fd, write_on_close);
    return;
  }
  ctx->done += (size_t) req->result;
  uv_fs_req_cleanup(req);
  if (ctx->done >= ctx->to_write) {
    ctx->close_req.data = ctx;
    uv_fs_close(uv_default_loop(), &ctx->close_req, ctx->fd, write_on_close);
    return;
  }
  // continue writing
  size_t   remain     = ctx->to_write - ctx->done;
  uv_buf_t buf        = uv_buf_init((char*) (ctx->f->data.data + ctx->done), (unsigned int) remain);
  ctx->write_req.data = ctx;
  uv_fs_write(uv_default_loop(), &ctx->write_req, ctx->fd, &buf, 1, (int64_t) (ctx->f->offset + ctx->done), write_on_write);
}

static void write_on_open(uv_fs_t* req) {
  write_one_ctx_t* ctx = (write_one_ctx_t*) req->data;
  if (req->result < 0) {
    ctx->f->error = strdup(uv_strerror((int) req->result));
    uv_fs_req_cleanup(req);
    write_ctx_finish(ctx);
    return;
  }
  ctx->fd = (uv_file) req->result;
  uv_fs_req_cleanup(req);
  // start first write
  ctx->to_write = (ctx->f->limit == 0 || ctx->f->limit > ctx->f->data.len) ? ctx->f->data.len : ctx->f->limit;
  if (ctx->to_write == 0) {
    // nothing to write
    ctx->close_req.data = ctx;
    uv_fs_close(uv_default_loop(), &ctx->close_req, ctx->fd, write_on_close);
    return;
  }
  uv_buf_t buf        = uv_buf_init((char*) ctx->f->data.data, (unsigned int) ctx->to_write);
  ctx->write_req.data = ctx;
  uv_fs_write(uv_default_loop(), &ctx->write_req, ctx->fd, &buf, 1, (int64_t) ctx->f->offset, write_on_write);
}

int c4_write_files_uv(void* user_data, c4_write_files_cb cb, file_data_t* files, int num_files, int flags, int mode) {
  if (num_files <= 0) return -1;
  files_op_ctx_t* parent = (files_op_ctx_t*) safe_calloc(1, sizeof(files_op_ctx_t));
  parent->user_data      = user_data;
  parent->wcb            = cb;
  // Make a heap copy of the files array to keep it valid until callback
  parent->files = (file_data_t*) safe_calloc((size_t) num_files, sizeof(file_data_t));
  memcpy(parent->files, files, (size_t) num_files * sizeof(file_data_t));
  parent->num_files = num_files;
  parent->pending   = num_files;
  for (int i = 0; i < num_files; i++) {
    write_one_ctx_t* ctx = (write_one_ctx_t*) safe_calloc(1, sizeof(write_one_ctx_t));
    ctx->parent          = parent;
    ctx->f               = &parent->files[i];
    ctx->fd              = -1;
    ctx->flags           = flags;
    ctx->mode            = mode;
    int rc               = c4_ensure_parent_directory(ctx->f->path, C4_DIR_MODE);
    if (rc != 0) {
      int sys_err = -rc;
      int uv_err  = uv_translate_sys_error(sys_err);
      if (uv_err == 0) uv_err = UV_UNKNOWN;
      ctx->f->error = strdup(uv_strerror(uv_err));
      write_ctx_finish(ctx);
      continue;
    }
    ctx->open_req.data = ctx;
    uv_fs_open(uv_default_loop(), &ctx->open_req, ctx->f->path, ctx->flags, ctx->mode, write_on_open);
  }
  return 0;
}

void c4_file_data_array_free(file_data_t* files, int num_files, int free_data) {
  if (!files || num_files <= 0) return;
  for (int i = 0; i < num_files; i++) {
    if (files[i].error) safe_free(files[i].error);
    if (free_data && files[i].data.data) safe_free(files[i].data.data);
    if (files[i].path) safe_free(files[i].path);
    files[i].error = NULL;
    files[i].data  = NULL_BYTES;
    files[i].path  = NULL;
  }
  // also free the array itself (expects heap-allocated arrays)
  safe_free(files);
}
