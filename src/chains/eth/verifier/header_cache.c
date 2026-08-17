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

#include "header_cache.h"
#include <string.h>

#ifdef EL_HEADER_CACHE

// The cache is a process-global shared by all contexts, so all operations are
// serialized by a lock to stay safe in multi-threaded bindings.
// Single-threaded WASM builds need no locking; only when Emscripten is built with
// -pthread (worker threads, __EMSCRIPTEN_PTHREADS__) real mutexes are required.
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
#define CACHE_LOCK()
#define CACHE_UNLOCK()
#elif defined(_MSC_VER)
#include <windows.h>
static SRWLOCK g_cache_lock = SRWLOCK_INIT;
#define CACHE_LOCK()   AcquireSRWLockExclusive(&g_cache_lock)
#define CACHE_UNLOCK() ReleaseSRWLockExclusive(&g_cache_lock)
#else
#include <pthread.h>
static pthread_mutex_t g_cache_lock = PTHREAD_MUTEX_INITIALIZER;
#define CACHE_LOCK()   pthread_mutex_lock(&g_cache_lock)
#define CACHE_UNLOCK() pthread_mutex_unlock(&g_cache_lock)
#endif

static verified_header_entry_t g_entries[HEADER_CACHE_SIZE] = {0};
// monotonic counter for LRU ordering (0 is reserved for "slot unused")
static uint64_t g_lru_counter = 0;

static verified_header_entry_t* touch(verified_header_entry_t* entry) {
  entry->last_used = ++g_lru_counter;
  return entry;
}

static verified_header_entry_t* find_by_number(chain_id_t chain_id, uint64_t block_number) {
  for (uint32_t i = 0; i < HEADER_CACHE_SIZE; i++) {
    verified_header_entry_t* e = g_entries + i;
    if (e->last_used && e->chain_id == chain_id && e->block_number == block_number) return e;
  }
  return NULL;
}

static verified_header_entry_t* find_by_hash(chain_id_t chain_id, const uint8_t* block_hash) {
  for (uint32_t i = 0; i < HEADER_CACHE_SIZE; i++) {
    verified_header_entry_t* e = g_entries + i;
    if (e->last_used && e->chain_id == chain_id && memcmp(e->block_hash, block_hash, 32) == 0) return e;
  }
  return NULL;
}

static void entry_reset(verified_header_entry_t* entry) {
  safe_free(entry->el_header.data);
  safe_free(entry->header_data.bytes.data);
  safe_free(entry->execution.bytes.data);
  memset(entry, 0, sizeof(*entry));
}

// returns an unused slot or, if the cache is full, the least recently used one (reset).
static verified_header_entry_t* lru_slot(void) {
  verified_header_entry_t* victim = g_entries;
  for (uint32_t i = 0; i < HEADER_CACHE_SIZE; i++) {
    if (!g_entries[i].last_used) return g_entries + i;
    if (g_entries[i].last_used < victim->last_used) victim = g_entries + i;
  }
  entry_reset(victim);
  return victim;
}

// finds the entry for (chain_id, block_number, block_hash) or creates one by evicting
// the LRU slot. An existing entry with the same number but a different hash (reorg or
// stale data) is reset before reuse so no fields of the old block survive.
static verified_header_entry_t* acquire_entry(chain_id_t chain_id, uint64_t block_number, const uint8_t* block_hash) {
  verified_header_entry_t* entry = find_by_number(chain_id, block_number);
  if (entry && memcmp(entry->block_hash, block_hash, 32) != 0) {
    entry_reset(entry);
    entry = NULL;
  }
  if (!entry) {
    // prefer reusing the slot we just reset over evicting another entry
    for (uint32_t i = 0; i < HEADER_CACHE_SIZE && !entry; i++)
      if (!g_entries[i].last_used) entry = g_entries + i;
    if (!entry) entry = lru_slot();
    entry->chain_id     = chain_id;
    entry->block_number = block_number;
    memcpy(entry->block_hash, block_hash, 32);
  }
  return touch(entry);
}

const verified_header_entry_t* c4_header_cache_get_by_number(chain_id_t chain_id, uint64_t block_number) {
  CACHE_LOCK();
  verified_header_entry_t* entry = find_by_number(chain_id, block_number);
  entry                          = (entry && entry->header_data.bytes.data) ? touch(entry) : NULL;
  CACHE_UNLOCK();
  return entry;
}

const verified_header_entry_t* c4_header_cache_get_by_hash(chain_id_t chain_id, const uint8_t* block_hash) {
  CACHE_LOCK();
  verified_header_entry_t* entry = find_by_hash(chain_id, block_hash);
  entry                          = (entry && entry->header_data.bytes.data) ? touch(entry) : NULL;
  CACHE_UNLOCK();
  return entry;
}

void c4_header_cache_put(chain_id_t chain_id, uint64_t block_number, const uint8_t* block_hash, ssz_ob_t header_data) {
  if (!block_hash || !header_data.bytes.data) return;
  CACHE_LOCK();
  verified_header_entry_t* entry = acquire_entry(chain_id, block_number, block_hash);
  safe_free(entry->header_data.bytes.data);
  entry->header_data.bytes = bytes_dup(header_data.bytes);
  entry->header_data.def   = header_data.def;
  CACHE_UNLOCK();
}

void c4_header_cache_set_execution(chain_id_t chain_id, uint64_t block_number, const uint8_t* block_hash, ssz_ob_t execution) {
  if (!block_hash || !execution.bytes.data) return;
  CACHE_LOCK();
  verified_header_entry_t* entry = find_by_number(chain_id, block_number);
  // require a hash match: after a same-height reorg the stale entry must not receive
  // the new block's payload (mixing blocks would corrupt the entry).
  if (entry && memcmp(entry->block_hash, block_hash, 32) == 0) {
    safe_free(entry->execution.bytes.data);
    entry->execution.bytes = bytes_dup(execution.bytes);
    entry->execution.def   = execution.def;
    touch(entry);
  }
  CACHE_UNLOCK();
}

void c4_header_cache_put_el_header(chain_id_t chain_id, uint64_t block_number, const uint8_t* block_hash, bytes_t el_header) {
  if (!block_hash || !el_header.data || !el_header.len) return;
  CACHE_LOCK();
  verified_header_entry_t* entry = acquire_entry(chain_id, block_number, block_hash);
  safe_free(entry->el_header.data);
  entry->el_header = bytes_dup(el_header);
  CACHE_UNLOCK();
}

bytes_t c4_header_cache_get_el_header(chain_id_t chain_id, const uint8_t* block_hash) {
  bytes_t result = NULL_BYTES;
  CACHE_LOCK();
  verified_header_entry_t* entry = find_by_hash(chain_id, block_hash);
  if (entry && entry->el_header.data) {
    touch(entry);
    // copy-out: the returned bytes must survive concurrent eviction/reorg resets
    result = bytes_dup(entry->el_header);
  }
  CACHE_UNLOCK();
  return result;
}

bool c4_header_cache_latest_block_hash(chain_id_t chain_id, bytes32_t block_hash) {
  CACHE_LOCK();
  verified_header_entry_t* best = NULL;
  for (uint32_t i = 0; i < HEADER_CACHE_SIZE; i++) {
    verified_header_entry_t* e = g_entries + i;
    if (e->last_used && e->chain_id == chain_id && e->el_header.data &&
        (!best || e->block_number > best->block_number)) best = e;
  }
  if (best) {
    // protect the advertised hash from eviction until the prover response is verified
    touch(best);
    memcpy(block_hash, best->block_hash, 32);
  }
  CACHE_UNLOCK();
  return best != NULL;
}

bool c4_header_cache_has_el_header(chain_id_t chain_id, const uint8_t* block_hash) {
  CACHE_LOCK();
  verified_header_entry_t* entry = find_by_hash(chain_id, block_hash);
  bool                     found = entry && entry->el_header.data;
  if (found) touch(entry);
  CACHE_UNLOCK();
  return found;
}

void c4_header_cache_clear(void) {
  CACHE_LOCK();
  for (uint32_t i = 0; i < HEADER_CACHE_SIZE; i++)
    if (g_entries[i].last_used) entry_reset(g_entries + i);
  g_lru_counter = 0;
  CACHE_UNLOCK();
}

#endif // EL_HEADER_CACHE
