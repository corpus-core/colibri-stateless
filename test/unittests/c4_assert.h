/*
 * Copyright (c) 2025 corpus.core
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include "../../bindings/colibri.h"
#include "../../bindings/colibri_common.h"
#include "beacon_types.h"
#include "bytes.h"
#include "chains.h"
#include "crypto.h"
#include "json.h"
#include "plugin.h"
#include "prover.h"
#include "ssz.h"
#include "state.h"
#include "sync_committee.h"
#include "unity.h"
#include "verify.h"
#ifdef _MSC_VER
#include <windows.h>
#else
#include <dirent.h>
#endif

// Include platform-specific time headers OR uv.h
#ifdef _WIN32
#include <windows.h>
#else // Non-Windows: Need sys/time.h for current_unix_ms()
#include <sys/time.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define C4_PROVER_FLAG_NO_CACHE (1 << 30)
#define ASSERT_HEX_STRING_EQUAL(expected_hex, actual_array, size, message)              \
  do {                                                                                  \
    uint8_t expected_bytes[size];                                                       \
    hex_to_bytes(expected_hex, -1, bytes(expected_bytes, size));                        \
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected_bytes, actual_array, size, message); \
  } while (0)

typedef struct _cached {
  bytes_t         data;
  char*           filename;
  struct _cached* next;
} _cached_t;
static _cached_t* _file_cache = NULL;
static bool       file_get(char* filename, buffer_t* data) {
  for (_cached_t* val = _file_cache; val; val = val->next) {
    if (strcmp(val->filename, filename) == 0) {
      buffer_append(data, val->data);
      return true;
    }
  }
  return false;
}
static void file_delete(char* filename) {
  _cached_t** prev = &_file_cache;
  for (_cached_t* val = _file_cache; val; prev = &val->next, val = val->next) {
    if (strcmp(val->filename, filename) == 0) {
      *prev = val->next;
      safe_free(val->filename);
      safe_free(val->data.data);
      safe_free(val);
      return;
    }
  }
}

static void file_set(char* key, bytes_t value) {
  _cached_t* n = safe_calloc(1, sizeof(_cached_t));
  n->filename  = strdup(key);
  n->data      = bytes_dup(value);
  n->next      = _file_cache;
  _file_cache  = n;
}

static void reset_local_filecache() {
  while (_file_cache) {
    _cached_t* next = _file_cache->next;
    safe_free(_file_cache->filename);
    safe_free(_file_cache->data.data);
    safe_free(_file_cache);
    _file_cache = next;
  }

  storage_plugin_t plgn = {
      .del             = file_delete,
      .get             = file_get,
      .set             = file_set,
      .max_sync_states = 3};
  c4_set_storage_config(&plgn);
  c4_reset_caches();
}
static uint64_t now() {
#ifndef _WIN32
  struct timeval te;
  gettimeofday(&te, NULL);
  return te.tv_sec * 1000L + te.tv_usec / 1000;
#else
  return 0;
#endif
}

static bytes_t read_testdata(const char* filename) {
  unsigned char buffer[1024];
  size_t        bytesRead;
  buffer_t      data = {0};
  buffer_t      path = {0};
  buffer_add_chars(&path, TESTDATA_DIR "/");
  buffer_add_chars(&path, filename);

  FILE* file = fopen((char*) path.data.data, "rb");
  buffer_free(&path);
  if (file == NULL) return NULL_BYTES;

  while ((bytesRead = fread(buffer, 1, sizeof(buffer), file)) == sizeof(buffer))
    buffer_append(&data, bytes(buffer, bytesRead));

  // Handle the last partial read or error/EOF
  if (bytesRead > 0) buffer_append(&data, bytes(buffer, bytesRead));

  // Explicitly check for read errors *after* the loop
  if (ferror(file)) {
    fprintf(stderr, "Error reading file: %s\n", filename);
    // Clear buffer if error occurred, as data might be incomplete/corrupt
    buffer_free(&data);
    fclose(file);      // Still close the file
    return NULL_BYTES; // Indicate error
  }

  // No error, just EOF (or successful full read if file size was multiple of buffer)
  fclose(file);
  return data.data;
}

// Normalizes line endings in a string by removing '\r' characters.
// Allocates a new string which must be freed by the caller.
static char* normalize_newlines(const char* input) {
  if (!input) return NULL;
  size_t len    = strlen(input);
  char*  output = (char*) safe_malloc(len + 1); // Allocate enough space (max possible)
  if (!output) {
    perror("Failed to allocate memory in normalize_newlines");
    return NULL; // Allocation failed
  }
  char* op = output;
  for (const char* ip = input; *ip != '\0'; ++ip) {
    if (*ip != '\r' && *ip != '\n' && *ip != ' ') {
      *op++ = *ip;
    }
  }
  *op = '\0'; // Null-terminate the output string
  return output;
}

static void set_state(chain_id_t chain_id, char* dirname) {
#ifdef _MSC_VER
  // Windows-specific implementation
  char dir_path[2024];
  sprintf(dir_path, "%s/%s", TESTDATA_DIR, dirname);

  // Convert forward slashes to backslashes for Windows
  for (char* p = dir_path; *p; p++) {
    if (*p == '/') *p = '\\';
  }

  // Add wildcard for FindFirstFile
  char search_path[2048];
  sprintf(search_path, "%s\\*", dir_path);

  WIN32_FIND_DATAA find_data;
  HANDLE           find_handle = FindFirstFileA(search_path, &find_data);

  if (find_handle == INVALID_HANDLE_VALUE) return;

  do {
    char* filename = find_data.cFileName;

    // Skip files containing a period
    if (strchr(filename, '.') != NULL) continue;

    // Skip . and .. directory entries
    if (strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0) continue;

    // Read the file content
    char rel_path[1024];
    sprintf(rel_path, "%s/%s", dirname, filename);
    bytes_t content = read_testdata(rel_path);

    if (content.data) {
      if (strncmp(filename, "tx_pending_", 11) == 0) {
        for (uint32_t off = 0; off + 40 <= content.len; off += 40) {
          uint64_t ts = (uint64_t) time(NULL);
          for (int b = 0; b < 8; b++)
            content.data[off + 32 + b] = (uint8_t) (ts >> (b * 8));
        }
      }
      file_set(filename, content);
      safe_free(content.data);
    }
  } while (FindNextFileA(find_handle, &find_data));

  FindClose(find_handle);
#else
  // Unix/Linux implementation
  char dir_path[2024];
  sprintf(dir_path, "%s/%s", TESTDATA_DIR, dirname);

  DIR* dir = opendir(dir_path);
  if (!dir) return;

  struct dirent* entry;
  while ((entry = readdir(dir)) != NULL) {
    char* filename = entry->d_name;

    // Skip files containing a period
    if (strchr(filename, '.') != NULL) continue;

    // Skip . and .. directory entries
    if (strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0) continue;

    // Read the file content - read_testdata already adds TESTDATA_DIR
    char rel_path[1024];
    sprintf(rel_path, "%s/%s", dirname, filename);
    bytes_t content = read_testdata(rel_path);

    if (content.data) {
      if (strncmp(filename, "tx_pending_", 11) == 0) {
        for (uint32_t off = 0; off + 40 <= content.len; off += 40) {
          uint64_t ts = (uint64_t) time(NULL);
          for (int b = 0; b < 8; b++)
            content.data[off + 32 + b] = (uint8_t) (ts >> (b * 8));
        }
      }
      file_set(filename, content);
      safe_free(content.data);
    }
  }

  closedir(dir);
#endif
}
static void verify_count(char* dirname, char* method, char* args, chain_id_t chain_id, size_t count, prover_flags_t flags, verify_flags_t verify_flags, char* expected_result, bool remote_prover) {

#ifdef PROVER_CACHE
  // Clear the global prover cache before each test to ensure isolation
  // Using max timestamp (0xffffffffffffffff) removes all entries
  c4_prover_cache_cleanup(0xffffffffffffffffULL, 0);
#endif

  if ((flags & C4_PROVER_FLAG_NO_CACHE) == 0)
    set_state(chain_id, dirname);

  c4_rpc_ctx_t* rpc_ctx = c4_rpc_ctx_create(method, args, chain_id, flags, verify_flags, remote_prover);

  bool done = false;
  //  bytes_t  proof_data   = {0};
  while (!done) {
    data_request_t* req;
    switch (c4_rpc_execute(rpc_ctx)) {
      case C4_PENDING:
        while ((req = c4_state_get_pending_request(c4_rpc_get_state(rpc_ctx)))) {
          char tmp[1024];

          char* filename = c4_req_mockname(req);
          sprintf(tmp, "%s/%s", dirname, filename);
          safe_free(filename);
          //          printf("read : %s\n     %s\n", tmp, req->payload.data ? (char*) req->payload.data : "");
          bytes_t content = read_testdata(tmp);
          TEST_ASSERT_NOT_NULL_MESSAGE(content.data, bprintf(NULL, "Did not find the testdata: %s", tmp));
          req->response = content;
        }
        break;

      case C4_ERROR:
        TEST_FAIL_MESSAGE(rpc_ctx->error ? rpc_ctx->error
                                         : (rpc_ctx->verifier.state.error ? rpc_ctx->verifier.state.error : "unknown error"));
        done = true;
        break;

      case C4_SUCCESS:
        if (expected_result) {
          char* result                     = ssz_dump_to_str(rpc_ctx->verifier.data, false, true);
          char* normalized_result          = normalize_newlines(result);
          char* normalized_expected_result = normalize_newlines(expected_result);

          TEST_ASSERT_EQUAL_STRING_MESSAGE(normalized_expected_result, normalized_result, "wrong result");
          safe_free(result);
          safe_free(normalized_result);
          safe_free(normalized_expected_result);
        }
        done = true;
        break;
    }
  }

  c4_rpc_ctx_free(rpc_ctx);
}

static void verify(char* dirname, char* method, char* args, chain_id_t chain_id) {
  verify_count(dirname, method, args, chain_id, 1, C4_PROVER_FLAG_INCLUDE_CODE | C4_PROVER_FLAG_CHAIN_STORE, 0, NULL, false);
}

static void run_rpc_test(char* dirname, prover_flags_t flags, verify_flags_t verify_flags) {
  char test_filename[1024];
  sprintf(test_filename, "%s/test.json", dirname);
  bytes_t    test_content      = read_testdata(test_filename);
  json_t     test              = json_parse((char*) test_content.data);
  char*      method            = bprintf(NULL, "%j", json_get(test, "method"));
  char*      args              = json_new_string(json_get(test, "params"));
  json_t     trusted_blockhash = json_get(test, "trusted_blockhash");
  chain_id_t chain_id          = (chain_id_t) json_get_uint64(test, "chain_id");
  char*      expected_result   = bprintf(NULL, "%J", json_get(test, "expected_result"));
  json_t           remote_prover = json_get(test, "remote_prover");
  json_t           prover_mode   = json_get(test, "prover_mode");
  c4_prover_mode_t mode          = C4_PROVER_MODE_LOCAL;
  if (prover_mode.type == JSON_TYPE_STRING) {
    if (prover_mode.len == 8 && strncmp(prover_mode.start, "\"remote\"", 8) == 0)
      mode = C4_PROVER_MODE_REMOTE;
    else if (prover_mode.len == 8 && strncmp(prover_mode.start, "\"hybrid\"", 8) == 0)
      mode = C4_PROVER_MODE_HYBRID;
    else if (prover_mode.len == 7 && strncmp(prover_mode.start, "\"proxy\"", 7) == 0)
      mode = C4_PROVER_MODE_PROXY;
  }
  else if (remote_prover.type == JSON_TYPE_BOOLEAN && remote_prover.len && remote_prover.start[0] == 't')
    mode = C4_PROVER_MODE_REMOTE;

  if (trusted_blockhash.type == JSON_TYPE_STRING && trusted_blockhash.len == 68) {
    bytes32_t checkpoint;
    hex_to_bytes(trusted_blockhash.start + 1, 66, bytes(checkpoint, 32));
    c4_eth_set_trusted_checkpoint(chain_id, checkpoint);
  }

  verify_count(dirname, method, args, chain_id, 1, flags, verify_flags, expected_result, mode);

  safe_free(method);
  safe_free(args);
  safe_free(expected_result);
  safe_free(test_content.data);
}
