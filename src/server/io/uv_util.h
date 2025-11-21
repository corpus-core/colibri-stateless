/*
 * Asynchronous file read/write helpers built on libuv
 */
#ifndef uv_util_h__
#define uv_util_h__

#include "bytes.h"
#include <stddef.h>
#include <uv.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct file_data_s {
  char*   path;   // file path (owned by caller; not freed by the util)
  size_t  offset; // start offset in file
  size_t  limit;  // max bytes to read/write (0 => all)
  char*   error;  // allocated error string on failure (caller frees)
  bytes_t data;   // result buffer for reads / input buffer for writes
} file_data_t;

typedef void (*c4_read_files_cb)(void* user_data, file_data_t* files, int num_files);
typedef void (*c4_write_files_cb)(void* user_data, file_data_t* files, int num_files);

// Schedule asynchronous reads for multiple files at once.
// - Invokes cb once when ALL files finished (success or error).
// - On success: files[i].data contains the bytes read (caller must free data.data)
// - On error:   files[i].error is set (caller must free), data is NULL_BYTES
// Returns 0 if scheduled, <0 on immediate error (cb not called).
int c4_read_files_uv(void* user_data, c4_read_files_cb cb, file_data_t* files, int num_files);

// Schedule asynchronous writes for multiple files at once.
// - Invokes cb once when ALL files finished (success or error).
// - On success: files[i].error == NULL
// - On error:   files[i].error is set (caller must free)
// Returns 0 if scheduled, <0 on immediate error (cb not called).
int c4_write_files_uv(void* user_data, c4_write_files_cb cb, file_data_t* files, int num_files, int flags, int mode);

// Free an array of file_data_t helper: frees error and path always.
// If free_data is non-zero, also frees data.data when not NULL (useful for read results).
void c4_file_data_array_free(file_data_t* files, int num_files, int free_data);

#ifdef __cplusplus
}
#endif

#endif
