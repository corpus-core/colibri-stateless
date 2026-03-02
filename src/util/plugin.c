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

#ifdef MEMORY_STORAGE
typedef struct mem_entry {
  char*             key;
  bytes_t           value;
  struct mem_entry* next;
} mem_entry_t;

static mem_entry_t* mem_head = NULL;

static mem_entry_t* memory_find(const char* key) {
  for (mem_entry_t* e = mem_head; e; e = e->next)
    if (strcmp(e->key, key) == 0)
      return e;
  return NULL;
}

static bool memory_get(char* key, buffer_t* data) {
  mem_entry_t* e = memory_find(key);
  if (!e) return false;
  buffer_append(data, e->value);
  return true;
}

static void memory_set(char* key, bytes_t value) {
  mem_entry_t* e = memory_find(key);
  if (e) {
    safe_free(e->value.data);
    e->value = bytes_dup(value);
    return;
  }
  e        = safe_malloc(sizeof(mem_entry_t));
  e->key   = strdup(key);
  e->value = bytes_dup(value);
  e->next  = mem_head;
  mem_head = e;
}

static void memory_delete(char* key) {
  mem_entry_t** p = &mem_head;
  while (*p) {
    if (strcmp((*p)->key, key) == 0) {
      mem_entry_t* e = *p;
      *p = e->next;
      safe_free(e->key);
      safe_free(e->value.data);
      safe_free(e);
      return;
    }
    p = &(*p)->next;
  }
}
#endif

#ifdef FILE_STORAGE
char* state_data_dir = NULL;

static char* combine_filename(char* name) {
  if (state_data_dir == NULL)
    state_data_dir = getenv("C4_STATES_DIR");
  if (state_data_dir == NULL)
    state_data_dir = ".";
  if (strcmp(state_data_dir, "."))
    return bprintf(NULL, "%s/%s", state_data_dir, name);
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
  if (!full_path) return;
  char* tmp_path = bprintf(NULL, "%s.tmp", full_path);
  FILE* file     = fopen(tmp_path, "wb");
  if (!file) {
    safe_free(full_path);
    safe_free(tmp_path);
    return;
  }
  fwrite(value.data, 1, value.len, file);
  fclose(file);
  rename(tmp_path, full_path);
  safe_free(tmp_path);
  safe_free(full_path);
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
  if (!storage_conf.get) {
#ifdef MEMORY_STORAGE
    storage_conf.get = memory_get;
    storage_conf.set = memory_set;
    storage_conf.del = memory_delete;
#elif defined(FILE_STORAGE)
    storage_conf.get = file_get;
    storage_conf.set = file_set;
    storage_conf.del = file_delete;
#endif
  }
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
