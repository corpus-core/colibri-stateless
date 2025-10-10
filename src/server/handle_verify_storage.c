/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "bytes.h"
#include "plugin.h"
#include "server.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <uv.h>

// Maximum number of entries in the RAM cache before eviction
#define MAX_CACHE_ENTRIES 100

// Simple linked list for RAM cache (optimized for small number of entries)
typedef struct storage_cache_entry {
  char*                       key;
  bytes_t                     value;
  struct storage_cache_entry* next;
} storage_cache_entry_t;

// Async write operation context
typedef struct {
  uv_fs_t open_req;
  uv_fs_t write_req;
  uv_fs_t close_req;
  char*   key;
  bytes_t value;
  char*   file_path;
  int     fd;
} async_write_ctx_t;

// Async delete operation context
typedef struct {
  uv_fs_t fs_req;
  char*   key;
  char*   file_path;
} async_delete_ctx_t;

static storage_cache_entry_t* cache_head        = NULL;
static bool                   cache_initialized = false;

// Initialize the RAM cache
static void init_cache() {
  if (cache_initialized) return;

  cache_head        = NULL;
  cache_initialized = true;
}

// Get file path for key (similar to plugin.c)
static char* get_file_path(const char* key) {
  const char* base_path = getenv("C4_STATES_DIR");
  if (base_path != NULL) {
    size_t length    = strlen(base_path) + strlen(key) + 2;
    char*  full_path = safe_malloc(length);
    if (full_path == NULL) return NULL;
    snprintf(full_path, length, "%s/%s", base_path, key);
    return full_path;
  }
  else {
    return strdup(key);
  }
}

// Find entry in cache (simple linear search)
static storage_cache_entry_t* find_cache_entry(const char* key) {
  storage_cache_entry_t* entry = cache_head;

  while (entry) {
    if (strcmp(entry->key, key) == 0) {
      return entry;
    }
    entry = entry->next;
  }
  return NULL;
}

// Add entry to cache
static void add_cache_entry(const char* key, bytes_t value) {
  // Check if entry already exists
  storage_cache_entry_t* existing = find_cache_entry(key);
  if (existing) {
    // Update existing entry (no eviction needed)
    safe_free(existing->value.data);
    existing->value.data = safe_malloc(value.len);
    memcpy(existing->value.data, value.data, value.len);
    existing->value.len = value.len;
    return;
  }

  // Create new entry and add to front of list
  storage_cache_entry_t* entry = safe_malloc(sizeof(storage_cache_entry_t));
  entry->key                   = strdup(key);
  entry->value.data            = safe_malloc(value.len);
  memcpy(entry->value.data, value.data, value.len);
  entry->value.len = value.len;
  entry->next      = cache_head;
  cache_head       = entry;

  // Eviction policy: count entries and remove oldest if limit exceeded
  int                    count   = 0;
  storage_cache_entry_t* current = cache_head;
  storage_cache_entry_t* prev    = NULL;

  // Traverse the list to count entries and find the tail
  while (current) {
    count++;
    if (current->next) {
      prev    = current;
      current = current->next;
    }
    else {
      // current is now the tail (oldest entry)
      break;
    }
  }

  // If we exceeded the limit, remove the tail
  if (count > MAX_CACHE_ENTRIES) {
    if (prev) {
      // Remove the tail by updating the previous node's next pointer
      prev->next = NULL;
      // Free the tail's memory
      safe_free(current->key);
      safe_free(current->value.data);
      safe_free(current);
    }
    else {
      // Edge case: only one entry in list (shouldn't happen if MAX_CACHE_ENTRIES >= 1)
      // but handle it gracefully
      if (current) {
        cache_head = NULL;
        safe_free(current->key);
        safe_free(current->value.data);
        safe_free(current);
      }
    }
  }
}

// Remove entry from cache
static void remove_cache_entry(const char* key) {
  storage_cache_entry_t* entry = cache_head;
  storage_cache_entry_t* prev  = NULL;

  while (entry) {
    if (strcmp(entry->key, key) == 0) {
      if (prev) {
        prev->next = entry->next;
      }
      else {
        cache_head = entry->next;
      }
      safe_free(entry->key);
      safe_free(entry->value.data);
      safe_free(entry);
      return;
    }
    prev  = entry;
    entry = entry->next;
  }
}

// Forward declarations
static void on_async_write_close(uv_fs_t* req);
static void on_async_write_data(uv_fs_t* req);
static void on_async_write_open(uv_fs_t* req);

// Callback for async file close after write
static void on_async_write_close(uv_fs_t* req) {
  async_write_ctx_t* ctx = (async_write_ctx_t*) req->data;

  if (req->result < 0) {
    // Log warning but don't treat as critical error
    printf("Warning: Failed to close file after write for key '%s': %s\n",
           ctx->key, uv_strerror((int) req->result));
  }

  // Cleanup
  uv_fs_req_cleanup(req);
  safe_free(ctx->key);
  safe_free(ctx->value.data);
  safe_free(ctx->file_path);
  safe_free(ctx);
}

// Callback for async write data
static void on_async_write_data(uv_fs_t* req) {
  async_write_ctx_t* ctx = (async_write_ctx_t*) req->data;

  if (req->result < 0) {
    printf("Async write failed for key '%s': %s\n",
           ctx->key, uv_strerror((int) req->result));
    // Still try to close the file
  }

  uv_fs_req_cleanup(req);

  // Close the file
  ctx->close_req.data = ctx;
  uv_fs_close(uv_default_loop(), &ctx->close_req, ctx->fd, on_async_write_close);
}

// Callback for async file open for write
static void on_async_write_open(uv_fs_t* req) {
  async_write_ctx_t* ctx = (async_write_ctx_t*) req->data;

  if (req->result < 0) {
    printf("Failed to open file for async write '%s': %s\n",
           ctx->file_path, uv_strerror((int) req->result));
    uv_fs_req_cleanup(req);
    safe_free(ctx->key);
    safe_free(ctx->value.data);
    safe_free(ctx->file_path);
    safe_free(ctx);
    return;
  }

  ctx->fd = (int) req->result;
  uv_fs_req_cleanup(req);

  // Write data
  uv_buf_t buf        = uv_buf_init((char*) ctx->value.data, ctx->value.len);
  ctx->write_req.data = ctx;
  uv_fs_write(uv_default_loop(), &ctx->write_req, ctx->fd, &buf, 1, -1, on_async_write_data);
}

// Callback for async delete completion
static void on_async_delete_complete(uv_fs_t* req) {
  async_delete_ctx_t* ctx = (async_delete_ctx_t*) req->data;

  if (req->result < 0 && req->result != UV_ENOENT) {
    printf("Async delete failed for key '%s': %s\n",
           ctx->key, uv_strerror((int) req->result));
  }

  // Cleanup
  uv_fs_req_cleanup(req);
  safe_free(ctx->key);
  safe_free(ctx->file_path);
  safe_free(ctx);
}

// RAM-based get function
static bool ram_storage_get(char* key, buffer_t* buffer) {
  init_cache();

  // First check RAM cache
  storage_cache_entry_t* entry = find_cache_entry(key);
  if (entry) {
    buffer_append(buffer, entry->value);
    return true;
  }

  // Not in cache, read from file system (blocking, but only happens once per key)
  char* file_path = get_file_path(key);
  if (!file_path) return false;

  FILE* file = fopen(file_path, "rb");
  if (!file) {
    safe_free(file_path);
    return false;
  }

  // Read file content
  unsigned char read_buffer[1024];
  size_t        bytes_read;
  buffer_t      file_content = {0};

  while ((bytes_read = fread(read_buffer, 1, sizeof(read_buffer), file)) > 0) {
    buffer_append(&file_content, bytes(read_buffer, bytes_read));
  }

  fclose(file);
  safe_free(file_path);

  if (file_content.data.len > 0) {
    // Add to cache
    add_cache_entry(key, file_content.data);

    // Return the data
    buffer_append(buffer, file_content.data);
    buffer_free(&file_content);
    return true;
  }

  buffer_free(&file_content);
  return false;
}

// RAM-based set function with async file write
static void ram_storage_set(char* key, bytes_t value) {
  init_cache();

  // Update RAM cache immediately
  add_cache_entry(key, value);

  // Schedule async file write
  async_write_ctx_t* ctx = safe_malloc(sizeof(async_write_ctx_t));
  ctx->key               = strdup(key);
  ctx->value.data        = safe_malloc(value.len);
  memcpy(ctx->value.data, value.data, value.len);
  ctx->value.len = value.len;
  ctx->file_path = get_file_path(key);

  if (!ctx->file_path) {
    safe_free(ctx->key);
    safe_free(ctx->value.data);
    safe_free(ctx);
    return;
  }

  ctx->open_req.data = ctx;

  // Open file for writing asynchronously
  uv_fs_open(uv_default_loop(), &ctx->open_req, ctx->file_path,
             O_CREAT | O_WRONLY | O_TRUNC, 0644, on_async_write_open);
}

// RAM-based delete function with async file delete
static void ram_storage_del(char* key) {
  init_cache();

  // Remove from RAM cache immediately
  remove_cache_entry(key);

  // Schedule async file delete
  async_delete_ctx_t* ctx = safe_malloc(sizeof(async_delete_ctx_t));
  ctx->key                = strdup(key);
  ctx->file_path          = get_file_path(key);

  if (!ctx->file_path) {
    safe_free(ctx->key);
    safe_free(ctx);
    return;
  }

  ctx->fs_req.data = ctx;

  // Delete file asynchronously
  uv_fs_unlink(uv_default_loop(), &ctx->fs_req, ctx->file_path, on_async_delete_complete);
}

// Initialize server storage configuration
void c4_init_server_storage() {
  static storage_plugin_t server_storage = {
      .get             = ram_storage_get,
      .set             = ram_storage_set,
      .del             = ram_storage_del,
      .max_sync_states = 3};

  c4_set_storage_config(&server_storage);
}
