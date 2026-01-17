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

#include "plugin.h"
#include "bytes.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SYNC_STATES_DEFAULT 3

storage_plugin_t storage_conf = {0};

static c4_parallel_for_fn g_parallel_for = NULL;
#ifdef FILE_STORAGE
char* state_data_dir = NULL;

static char* combine_filename(char* name) {
  // Determine state directory on every call.
  // This allows tests or embeddings (e.g. Node.js) to switch the state directory at runtime
  // by updating the C4_STATES_DIR environment variable between operations.
  const char* env_dir = getenv("C4_STATES_DIR");
  if (env_dir && (!state_data_dir || strcmp(state_data_dir, env_dir) != 0)) state_data_dir = (char*) env_dir;
  if (state_data_dir == NULL) state_data_dir = ".";

  if (strcmp(state_data_dir, ".")) return bprintf(NULL, "%s/%s", state_data_dir, name);
  else
    return strdup(name);
}

static bool file_get(char* filename, buffer_t* data) {
  unsigned char buffer[1024];
  size_t        bytesRead;
  char*         full_path = combine_filename(filename);
  if (full_path == NULL) return false;

  FILE* file = strcmp(filename, "-") ? fopen(full_path, "rb") : stdin;
  safe_free(full_path);
  if (file == NULL) return false;

  while ((bytesRead = fread(buffer, 1, 1024, file)) == sizeof(buffer))
    buffer_append(data, bytes(buffer, bytesRead));

  if (bytesRead > 0) buffer_append(data, bytes(buffer, bytesRead));

#ifndef __clang_analyzer__
  if (file != stdin)
#endif
    fclose(file);

  return true;
}

static void file_set(char* key, bytes_t value) {
  char* full_path = combine_filename(key);
  if (full_path == NULL) return;
  FILE* file = fopen(full_path, "wb");
  safe_free(full_path);
  if (!file) return;
  fwrite(value.data, 1, value.len, file);
  fclose(file);
}
static void file_delete(char* filename) {
  char* full_path = combine_filename(filename);
  if (full_path == NULL) return;
  remove(full_path);
  safe_free(full_path);
}

#endif

void c4_get_storage_config(storage_plugin_t* plugin) {
  if (!storage_conf.max_sync_states) storage_conf.max_sync_states = MAX_SYNC_STATES_DEFAULT;
#ifdef FILE_STORAGE
  if (!storage_conf.get) {
    storage_conf.get = file_get;
    storage_conf.set = file_set;
    storage_conf.del = file_delete;
  }
#endif
  *plugin = storage_conf;
}

void c4_set_storage_config(storage_plugin_t* plugin) {
  storage_conf = *plugin;
  if (!storage_conf.max_sync_states) storage_conf.max_sync_states = MAX_SYNC_STATES_DEFAULT;
}

void c4_set_parallel_for(c4_parallel_for_fn fn) {
  g_parallel_for = fn;
}

c4_parallel_for_fn c4_get_parallel_for(void) {
  return g_parallel_for;
}
