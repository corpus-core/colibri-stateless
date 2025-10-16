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

#include "prover.h"
#include "../util/json.h"
#include "../util/state.h"
#include PROVERS_PATH
#include "logger.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// Include platform-specific time headers OR uv.h
#ifdef _WIN32
#include <windows.h>
#else // Non-Windows: Need sys/time.h for current_unix_ms()
#include <sys/time.h>
#endif

#ifdef C4_PROVER_USE_UV_TIME
// Also include uv.h if using uv time source for current_ms()
#include <uv.h>
#endif

// Macro for efficient cache key comparison (32-byte keys)
#define CACHE_KEY_MATCH(entry, key, key_start) \
  (*((uint64_t*) (entry)->key) == (key_start) && memcmp((entry)->key, (key), 32) == 0)

#ifdef PROVER_CACHE
// Structure for the global cache dynamic array
typedef struct {
  cache_entry_t* entries;      // Pointer to the array of cache entries
  size_t         count;        // Current number of entries in the array
  size_t         capacity;     // Allocated capacity of the array
  uint64_t       current_size; // Current total size of data in cache entries
} global_cache_t;

// Initial capacity for the global cache array
#define GLOBAL_CACHE_INITIAL_CAPACITY 32

// Function to get current time as Unix epoch milliseconds using system calls
uint64_t current_unix_ms() {
  struct timeval time_val;
#ifdef _WIN32
  FILETIME       ft;
  ULARGE_INTEGER large_int;
  GetSystemTimeAsFileTime(&ft);
  large_int.LowPart  = ft.dwLowDateTime;
  large_int.HighPart = ft.dwHighDateTime;
  // Convert to microseconds from Jan 1, 1601
  // Then adjust to Unix epoch (Jan 1, 1970)
  uint64_t unix_time = (large_int.QuadPart - 116444736000000000LL) / 10;
  time_val.tv_sec    = unix_time / 1000000;
  time_val.tv_usec   = unix_time % 1000000;
#else
  gettimeofday(&time_val, NULL);
#endif
  return time_val.tv_sec * 1000L + time_val.tv_usec / 1000;
}

// Keep current_ms() as it is (with the conditional uv_now)
uint64_t current_ms() {
#ifdef C4_PROVER_USE_UV_TIME
  return uv_now(uv_default_loop());
#else
  return current_unix_ms();
#endif // C4_PROVER_USE_UV_TIME
}

// Global cache implementation using dynamic array for better performance
static global_cache_t global_cache_array    = {NULL, 0, 0, 0};
static uint64_t       global_cache_max_size = 1024 * 1024 * 100; // 100MB max cache size

const void* c4_prover_cache_get(prover_ctx_t* ctx, bytes32_t key) {
  uint64_t key_start = *((uint64_t*) key); // optimize cache-loop by first checking the first word before doing a memcmp

  // 1. Check local cache (remains linked list)
  for (cache_entry_t* entry = ctx->cache; entry; entry = entry->next) {
    if (CACHE_KEY_MATCH(entry, key, key_start))
      return entry->value;
  }

  // if we are running in the worker-thread, we don't access the global cache anymore
  if (ctx->flags & C4_PROVER_FLAG_UV_WORKER_REQUIRED) {
    log_warn("[CACHEMISS] trying to access the global cache with cachekey %b, but we are running in the worker-thread. Make sure you tried to access in queue thread first!", bytes(key, 32));
    return NULL;
  }

  // 2. Check global cache (now an array)
  for (size_t i = 0; i < global_cache_array.count; ++i) {
    cache_entry_t* entry = &global_cache_array.entries[i]; // Get pointer to entry in array
    if (CACHE_KEY_MATCH(entry, key, key_start)) {

      // Skip invalidated entries (timestamp == 0 means invalidated)
      if (entry->timestamp == 0) {
        log_debug("Found matching key %b in global cache, but it was invalidated. Treating as miss.", bytes(key, 32));
        continue; // Skip this entry, check next
      }

      // Found valid entry in global cache - copy it to local cache
      // Note: The cached value is shared (read-only) between local and global cache.
      // This design enables memory-efficient sharing of immutable objects like Merkle trees.
      cache_entry_t* new_entry = (cache_entry_t*) safe_calloc(1, sizeof(cache_entry_t));
      memcpy(new_entry, entry, sizeof(cache_entry_t)); // Copy entry metadata, value pointer is shared
      new_entry->timestamp         = 0;                // Mark as local-only (will not be added back to global)
      new_entry->next              = ctx->cache;       // Link into local cache list
      new_entry->from_global_cache = true;             // mark it as from the global cache
      ctx->cache                   = new_entry;

      // Increment use counter on the *global* entry
      entry->use_counter++;
      return new_entry->value;
    }
  }

  // Not found in local or global cache
  return NULL;
}

void c4_prover_cache_set(prover_ctx_t* ctx, bytes32_t key, void* value, uint32_t size, uint64_t duration_ms, cache_free_cb free) {
  cache_entry_t* entry = (cache_entry_t*) safe_calloc(1, sizeof(cache_entry_t));
  memcpy(entry->key, key, 32);
  entry->value     = value;
  entry->size      = size;
  entry->timestamp = duration_ms; // Store the relative duration
  entry->free      = free;
  entry->next      = ctx->cache;
  ctx->cache       = entry;
}
void c4_prover_cache_stats(uint64_t* entries, uint64_t* size, uint64_t* max_size, uint64_t* capacity) {
  *entries  = (uint64_t) global_cache_array.count;
  *size     = (uint64_t) global_cache_array.current_size;
  *max_size = (uint64_t) global_cache_max_size;
  *capacity = (uint64_t) global_cache_array.capacity;
}

void c4_prover_cache_cleanup(uint64_t now, uint64_t extra_size) {
  if (!global_cache_array.entries || global_cache_array.count == 0) return; // Nothing to clean

  size_t   write_index       = 0; // Index where the next kept entry should be written
  uint64_t current_kept_size = 0; // Tracks the size of entries kept so far
  uint64_t max_size          = (extra_size > global_cache_max_size) ? global_cache_max_size : global_cache_max_size - extra_size;

  log_debug("Starting global cache cleanup. Current count: %d, Current size: %l", global_cache_array.count, global_cache_array.current_size);

  for (size_t read_index = 0; read_index < global_cache_array.count; ++read_index) {
    cache_entry_t* entry = global_cache_array.entries + read_index;

    // Determine if entry should be removed: Expired OR adding it exceeds max size (and not in use)
    // Note: This logic preserves entries currently in use (use_counter > 0) even if they are expired or push the cache over size.
    bool should_remove = (entry->timestamp < now || (current_kept_size + entry->size > max_size)) && entry->use_counter == 0;

    if (!should_remove) {
      // Keep this entry
      current_kept_size += entry->size;

      // If read_index is ahead of write_index, we need to move the entry
      if (write_index < read_index)
        // Move the current entry to the write_index position
        global_cache_array.entries[write_index] = global_cache_array.entries[read_index];
      write_index++; // Move write_index forward for the next kept entry
    }
    else {
      // Remove this entry
      log_debug("Removing cache entry %b (Size: %d, Expired: %s, OverSizeLimit: %s, UseCount: %d)",
                bytes(entry->key, 32), entry->size, (entry->timestamp < now) ? "Yes" : "No",
                (current_kept_size + entry->size > max_size) ? "Yes" : "No", entry->use_counter);

      // Free the associated value if a free function exists
      if (entry->free && entry->value)
        entry->free(entry->value);
      // Entry structure will be overwritten or fall beyond new count
    }
  }

  // Update the count and size of the global cache
  global_cache_array.count        = write_index;
  global_cache_array.current_size = current_kept_size;
}

// Helper function to ensure capacity and add an entry to the global cache array
static bool add_entry_to_global_cache(cache_entry_t* entry_to_add) {
  if (entry_to_add->size + global_cache_array.current_size > global_cache_max_size)
    c4_prover_cache_cleanup(current_ms(), entry_to_add->size);
  // Ensure initial allocation
  if (global_cache_array.entries == NULL) {
    global_cache_array.entries = (cache_entry_t*) safe_malloc(GLOBAL_CACHE_INITIAL_CAPACITY * sizeof(cache_entry_t));
    if (!global_cache_array.entries) {
      log_error("Failed to allocate initial global cache array");
      return false; // Allocation failed
    }
    global_cache_array.capacity     = GLOBAL_CACHE_INITIAL_CAPACITY;
    global_cache_array.count        = 0;
    global_cache_array.current_size = 0;
  }
  // Check if resize is needed
  else if (global_cache_array.count >= global_cache_array.capacity) {
    size_t         new_capacity = global_cache_array.capacity > 0 ? global_cache_array.capacity * 2 : GLOBAL_CACHE_INITIAL_CAPACITY; // Double the capacity, handle 0 start
    cache_entry_t* new_entries  = (cache_entry_t*) safe_realloc(global_cache_array.entries, new_capacity * sizeof(cache_entry_t));
    if (!new_entries) {
      log_error("Failed to reallocate global cache array from %d to %d entries", global_cache_array.capacity, new_capacity);
      // Keep the old array, but cannot add the new entry
      return false;
    }
    log_debug("Reallocated global cache array from %d to %d entries", (uint32_t) global_cache_array.capacity, (uint32_t) new_capacity);
    global_cache_array.entries  = new_entries;
    global_cache_array.capacity = new_capacity;
  }

  // Convert relative duration to absolute expiry time
  uint64_t duration_ms = entry_to_add->timestamp; // Get the stored duration

  // Add the entry (copy data)
  memcpy(&global_cache_array.entries[global_cache_array.count], entry_to_add, sizeof(cache_entry_t));

  // Now, update the copied entry in the global array
  cache_entry_t* global_entry = &global_cache_array.entries[global_cache_array.count];

  // Reset fields specific to linked list or local context
  global_entry->next              = NULL;  // Not used in array
  global_entry->from_global_cache = false; // Not used for entries originating in global cache
  global_entry->use_counter       = 0;     // Reset use counter when added to global cache

  // Convert relative duration to absolute expiry timestamp
  global_entry->timestamp = current_ms() + duration_ms;

  // Update counts for the array
  global_cache_array.count++;
  global_cache_array.current_size += global_entry->size; // Use global_entry->size

  log_debug("Added cache entry %b to global cache", bytes(global_entry->key, 32));

  return true;
}

// Find an entry in the global cache array by key
static cache_entry_t* find_global_cache_entry(bytes32_t key) {
  uint64_t key_start = *((uint64_t*) key); // optimize cache-loop by first checking the first word before doing a memcmp
  for (size_t i = 0; i < global_cache_array.count; ++i) {
    cache_entry_t* entry = &global_cache_array.entries[i];
    if (CACHE_KEY_MATCH(entry, key, key_start)) return entry; // Return pointer to the entry in the array
  }
  return NULL; // Not found
}

// Invalidate a specific entry in the global cache by setting its timestamp to 0
void c4_prover_cache_invalidate(bytes32_t key) {
  cache_entry_t* entry = find_global_cache_entry(key);
  if (entry) {
    log_debug("Invalidating global cache entry %b", bytes(key, 32));
    entry->timestamp = 0; // Mark as immediately expired/invalid
  }
  else {
    log_debug("Attempted to invalidate key %b, but it was not found in global cache.", bytes(key, 32));
  }
}

#endif

prover_ctx_t* c4_prover_create(char* method, char* params, chain_id_t chain_id, prover_flags_t flags) {
  // Always create context to return error information
  prover_ctx_t* ctx = (prover_ctx_t*) safe_calloc(1, sizeof(prover_ctx_t));
  ctx->chain_id     = chain_id;
  ctx->flags        = flags;

  // Input validation
  if (!method) {
    c4_state_add_error(&ctx->state, "c4_prover_create: method cannot be NULL");
    return ctx;
  }
  ctx->method = strdup(method);

  // Use empty array as default if params is NULL
  json_t params_json = json_parse(params ? params : "[]");
  if (params_json.type != JSON_TYPE_ARRAY) {
    c4_state_add_error(&ctx->state, "c4_prover_create: params must be a valid JSON array");
    return ctx;
  }

  char* params_str = (char*) safe_malloc(params_json.len + 1);
  memcpy(params_str, params_json.start, params_json.len);
  params_str[params_json.len] = 0;
  params_json.start           = params_str;

  // Set valid parameters
  ctx->params = params_json;
  return ctx;
}

void c4_prover_free(prover_ctx_t* ctx) {
  c4_state_free(&ctx->state);
  if (ctx->method) safe_free(ctx->method);
  if (ctx->params.start) safe_free((void*) ctx->params.start);
  if (ctx->proof.data) safe_free(ctx->proof.data);
  if (ctx->client_state.data) safe_free(ctx->client_state.data);
#ifdef PROVER_CACHE
  while (ctx->cache) {
    cache_entry_t* next                = ctx->cache->next;
    cache_entry_t* current_local_entry = ctx->cache; // Keep pointer to current local entry

    // Check if the entry should be moved to the global cache
    if (current_local_entry->timestamp &&                   // Has a TTL (intended for global)
        !current_local_entry->from_global_cache &&          // Not sourced/copied from global
        !find_global_cache_entry(current_local_entry->key)) // Doesn't already exist in global
    {
      // Attempt to add to global cache array by copying data
      if (add_entry_to_global_cache(current_local_entry)) {
        // Success: Ownership of value transferred to global cache.
        // We only need to free the local cache entry *structure*.
        log_debug("Moved cache entry %b to global cache", bytes(current_local_entry->key, 32));
      }
      else {
        // Failed to add (e.g., allocation failure). Must free the value now.
        log_warn("Failed to add cache entry %b to global cache, freeing value.", bytes(current_local_entry->key, 32));
        if (current_local_entry->free && current_local_entry->value)
          current_local_entry->free(current_local_entry->value);
      }
    }
    else {
      // Entry is NOT being moved to global cache.
      // Either it was sourced from global (src != NULL) or it's local-only (timestamp == 0).

      if (current_local_entry->from_global_cache) {
        // This was copied from global, decrement the counter on the source.
        // Re-find the global entry by key before decrementing for safety against realloc.
        cache_entry_t* global_src = find_global_cache_entry(current_local_entry->key);
        if (global_src && global_src->use_counter > 0) // Prevent underflow
          global_src->use_counter--;
        else
          log_warn("Source entry for key %b not found in global cache or use_counter is 0 during free, use_counter not decremented.", bytes(current_local_entry->key, 32));
      }
      else if (current_local_entry->free && current_local_entry->value)
        current_local_entry->free(current_local_entry->value);
    }
    // Free the local linked-list node structure itself
    safe_free(current_local_entry);
    ctx->cache = next; // Move to the next node in the local list
  }
#endif // PROVER_CACHE
  // Finally, free the context itself
  safe_free(ctx);
}

c4_status_t c4_prover_status(prover_ctx_t* ctx) {
  if (ctx->state.error) return C4_ERROR;
  if (ctx->proof.data) return C4_SUCCESS;
  if (c4_state_get_pending_request(&ctx->state)) return C4_PENDING;
  return C4_PENDING;
}

c4_status_t c4_prover_execute(prover_ctx_t* ctx) {
  // we always check the state first, so we don't execute if the result or error is already there.
  if (c4_state_get_pending_request(&ctx->state)) return C4_PENDING;
  if (ctx->state.error) return C4_ERROR;
  if (ctx->proof.data) return C4_SUCCESS;

  // execute the prover. The return value does not matter, we always check the state again after execution.
  prover_execute(ctx);

  return c4_prover_status(ctx);
}
