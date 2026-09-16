/*
 * Asynchronous file read/write helpers built on libuv
 */
#ifndef uv_util_h__
#define uv_util_h__

#include "bytes.h"
#include "logger.h"
#include <stddef.h>
#include <uv.h>

/**
 * Logs a libuv failure when `r` is negative (e.g. synchronous `uv_fs_*` return value).
 *
 * @param op operation name for the log line
 * @param r libuv status code
 */
#define C4_UV_LOG_ERR_NEG(op, r)                                                         \
  do {                                                                                   \
    int _r = (int) (r);                                                                  \
    if (_r < 0) log_error("%s failed: %s (%s)", (op), uv_strerror(_r), uv_err_name(_r)); \
  } while (0)

/**
 * Logs a libuv failure when `r` is non-zero (e.g. bind/listen/init calls).
 *
 * @param op operation name for the log line
 * @param r libuv status code
 */
#define C4_UV_LOG_ERR_NZ(op, r)                                                           \
  do {                                                                                    \
    int _r = (int) (r);                                                                   \
    if (_r != 0) log_error("%s failed: %s (%s)", (op), uv_strerror(_r), uv_err_name(_r)); \
  } while (0)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct file_data_s {
  char*   path;   /** file path (owned by caller; not freed by the util) */
  size_t  offset; /** start offset in file */
  size_t  limit;  /** max bytes to read/write (`0` = entire file) */
  char*   error;  /** allocated error string on failure (caller frees) */
  bytes_t data;   /** result buffer for reads / input buffer for writes */
} file_data_t;

/** Called when a batch read completes (all files finished). */
typedef void (*c4_read_files_cb)(void* user_data, file_data_t* files, int num_files);
/** Called when a batch write completes (all files finished). */
typedef void (*c4_write_files_cb)(void* user_data, file_data_t* files, int num_files);

/**
 * Schedules asynchronous reads for multiple files on the default libuv loop.
 *
 * Invokes `cb` once when all files finish. On success, `files[i].data` holds bytes (caller frees `data.data`).
 * On error, `files[i].error` is set.
 *
 * @param user_data opaque pointer passed to `cb`
 * @param cb completion callback
 * @param files array of length `num_files`
 * @param num_files number of entries in `files`
 * @return `0` if scheduled, negative libuv error if scheduling failed (`cb` not called)
 */
int c4_read_files_uv(void* user_data, c4_read_files_cb cb, file_data_t* files, int num_files);

/**
 * Schedules asynchronous writes for multiple files on the default libuv loop.
 *
 * @param user_data opaque pointer passed to `cb`
 * @param cb completion callback
 * @param files array of length `num_files` (each `data` buffer is written)
 * @param num_files number of entries
 * @param flags open flags passed to `uv_fs_open` (e.g. `O_WRONLY|O_CREAT`)
 * @param mode file mode passed to `uv_fs_open`
 * @return `0` if scheduled, negative libuv error if scheduling failed (`cb` not called)
 */
int c4_write_files_uv(void* user_data, c4_write_files_cb cb, file_data_t* files, int num_files, int flags, int mode);

/**
 * Frees `error` and optionally `data.data` for each entry in a batch.
 *
 * @param files array to clean up
 * @param num_files length of `files`
 * @param free_data non-zero to also `safe_free` successful read buffers
 */
void c4_file_data_array_free(file_data_t* files, int num_files, int free_data);

#ifdef __cplusplus
}
#endif

#endif
