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
#include "beacon_types.h"
#include "crypto.h"
#include "plugin.h"
#include <stdlib.h>
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
  safe_free(entry->el_body.bytes.data);
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
  entry                          = (entry && entry->el_header.data) ? touch(entry) : NULL;
  CACHE_UNLOCK();
  return entry;
}

const verified_header_entry_t* c4_header_cache_get_by_hash(chain_id_t chain_id, const uint8_t* block_hash) {
  CACHE_LOCK();
  verified_header_entry_t* entry = find_by_hash(chain_id, block_hash);
  entry                          = (entry && entry->el_header.data) ? touch(entry) : NULL;
  CACHE_UNLOCK();
  return entry;
}

void c4_header_cache_put(chain_id_t chain_id, uint64_t block_number, const uint8_t* block_hash, bytes_t el_header, ssz_ob_t* el_body) {
  if (!block_hash || !el_header.data) return;
  CACHE_LOCK();
  verified_header_entry_t* entry = acquire_entry(chain_id, block_number, block_hash);
  safe_free(entry->el_header.data);
  safe_free(entry->el_body.bytes.data);
  entry->el_header = bytes_dup(el_header);
  if (el_body) {
    entry->el_body       = *el_body;
    entry->el_body.bytes = bytes_dup(el_body->bytes);
  }
  CACHE_UNLOCK();
}

bytes_t c4_header_cache_get_el_header(chain_id_t chain_id, const uint8_t* block_hash, ssz_ob_t* el_body) {
  bytes_t result = NULL_BYTES;
  CACHE_LOCK();
  verified_header_entry_t* entry = find_by_hash(chain_id, block_hash);
  if (entry && entry->el_header.data) {
    touch(entry);
    // copy-out: the returned bytes must survive concurrent eviction/reorg resets
    result = bytes_dup(entry->el_header);
    if (el_body && entry->el_body.bytes.data) {
      *el_body       = entry->el_body;
      el_body->bytes = bytes_dup(entry->el_body.bytes);
    }
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

// :: Persistence
//
// The snapshot layout mirrors `ETH_BLOCK_BODY_CONTENT` for the body payload so a
// loaded entry can be consumed by the existing verifier / prover paths without
// any special-casing. Only the intersection of `transactions` + `withdrawals` is
// persisted; any additional fields of the source container (e.g. a full
// execution payload) are dropped.
//
// The maximum RLP header size across all supported forks is well below 4 KiB;
// the 8192-byte cap is a defense-in-depth limit for malformed storage input.
#define HEADER_CACHE_MAX_EL_HEADER 8192

// Absolute cap for the persisted snapshot. Even with progressive lists (which
// have no per-list capacity), a valid snapshot can be bounded by:
// HEADER_CACHE_SIZE (256) * (typical block body ~2 MiB worst case). We pick a
// generous 32 MiB to comfortably fit real mainnet data while rejecting
// pathological storage-supplied blobs before ssz_is_valid has to walk them.
#define HEADER_CACHE_MAX_SNAPSHOT_BYTES (32u * 1024u * 1024u)

static const ssz_def_t HEADER_CACHE_BODY_FIELDS[] = {
    SSZ_PROG_LIST("transactions", ssz_transactions_bytes),
    SSZ_PROG_LIST("withdrawals", DENEP_WITHDRAWAL_CONTAINER),
};
static const ssz_def_t HEADER_CACHE_BODY_UNION[] = {
    SSZ_NONE,
    SSZ_CONTAINER("content", HEADER_CACHE_BODY_FIELDS),
};
static const ssz_def_t HEADER_CACHE_ENTRY_FIELDS[] = {
    SSZ_UINT64("block_number"),
    SSZ_BYTES32("block_hash"),
    SSZ_BYTES("el_header", HEADER_CACHE_MAX_EL_HEADER),
    SSZ_UNION("body", HEADER_CACHE_BODY_UNION),
};
static const ssz_def_t HEADER_CACHE_ENTRY    = SSZ_CONTAINER("entry", HEADER_CACHE_ENTRY_FIELDS);
static const ssz_def_t HEADER_CACHE_SNAPSHOT = SSZ_LIST("headers", HEADER_CACHE_ENTRY, HEADER_CACHE_SIZE);

static void header_cache_storage_key(chain_id_t chain_id, char* buf, size_t buf_len) {
  buffer_t b = (buffer_t) {.data = bytes((uint8_t*) buf, 0), .allocated = -(int32_t) buf_len};
  bprintf(&b, "headers_%l", (uint64_t) chain_id);
}

static int compare_entries_by_lru(const void* a, const void* b) {
  const verified_header_entry_t* ea = *(const verified_header_entry_t* const*) a;
  const verified_header_entry_t* eb = *(const verified_header_entry_t* const*) b;
  if (ea->last_used < eb->last_used) return -1;
  if (ea->last_used > eb->last_used) return 1;
  return 0;
}

// Serializes one live cache entry into `entry_builder`. Must be called with
// the cache lock held so the entry's bytes are stable; `ssz_add_bytes` copies
// them into the builder, so after this call the entry can be mutated again
// (or another entry serialized) without affecting the builder contents.
static void serialize_entry(ssz_builder_t* entry_builder, const verified_header_entry_t* entry) {
  ssz_add_uint64(entry_builder, entry->block_number);
  ssz_add_bytes(entry_builder, "block_hash", bytes((uint8_t*) entry->block_hash, 32));
  ssz_add_bytes(entry_builder, "el_header", entry->el_header);

  bool has_body = false;
  if (entry->el_body.def && entry->el_body.bytes.data &&
      ssz_get_def(entry->el_body.def, "transactions") &&
      ssz_get_def(entry->el_body.def, "withdrawals")) {
    ssz_ob_t body_ob = entry->el_body;
    ssz_ob_t txs     = ssz_get(&body_ob, "transactions");
    ssz_ob_t withd   = ssz_get(&body_ob, "withdrawals");
    if (txs.def && withd.def) {
      ssz_builder_t bb = ssz_builder_for_def(&HEADER_CACHE_BODY_UNION[1]);
      ssz_add_bytes(&bb, "transactions", txs.bytes);
      ssz_add_bytes(&bb, "withdrawals", withd.bytes);
      ssz_ob_t content = ssz_builder_to_bytes(&bb);
      // Prepend the union selector (variant 1 = content) and pass the whole
      // payload to ssz_add_bytes; the body field is dynamic (union), so this
      // writes the offset into the fixed part and the payload into the dynamic
      // part in one go.
      uint8_t* payload = safe_malloc(content.bytes.len + 1);
      payload[0]       = 1;
      if (content.bytes.len) memcpy(payload + 1, content.bytes.data, content.bytes.len);
      ssz_add_bytes(entry_builder, "body", bytes(payload, (uint32_t) (content.bytes.len + 1)));
      safe_free(payload);
      safe_free(content.bytes.data);
      has_body = true;
    }
  }
  if (!has_body) {
    uint8_t none_selector = 0;
    ssz_add_bytes(entry_builder, "body", bytes(&none_selector, 1));
  }
}

void c4_header_cache_save(chain_id_t chain_id) {
  storage_plugin_t plugin = {0};
  c4_get_storage_config(&plugin);
  if (!plugin.set) return;

  // save/load are typically CLI-start / CLI-shutdown calls: no other threads
  // contend for the cache in that scenario, so it is safe (and much simpler)
  // to serialize directly from the live entries under the lock. ssz_add_bytes
  // copies the bytes into the builder, so once the SSZ snapshot is built we
  // can release the lock before touching the storage backend.
  verified_header_entry_t* ordered[HEADER_CACHE_SIZE];
  uint32_t                 count = 0;
  CACHE_LOCK();
  for (uint32_t i = 0; i < HEADER_CACHE_SIZE; i++) {
    verified_header_entry_t* e = g_entries + i;
    if (e->last_used && e->chain_id == chain_id && e->el_header.data)
      ordered[count++] = e;
  }

  if (count == 0) {
    CACHE_UNLOCK();
    // Nothing to persist: drop any previous snapshot so subsequent loads do
    // not resurrect stale entries.
    if (plugin.del) {
      char key[64] = {0};
      header_cache_storage_key(chain_id, key, sizeof(key));
      plugin.del(key);
    }
    return;
  }

  // Persist in LRU order (oldest first) so a subsequent load restores the
  // relative recency: the freshest entry ends up with the highest LRU counter.
  qsort(ordered, count, sizeof(*ordered), compare_entries_by_lru);

  ssz_builder_t list_builder = ssz_builder_for_def(&HEADER_CACHE_SNAPSHOT);
  for (uint32_t i = 0; i < count; i++) {
    ssz_builder_t entry_builder = ssz_builder_for_def(&HEADER_CACHE_ENTRY);
    serialize_entry(&entry_builder, ordered[i]);
    ssz_add_dynamic_list_builders(&list_builder, 0, entry_builder);
  }
  ssz_builder_fix_list_offsets(&list_builder, count);
  ssz_ob_t snapshot = ssz_builder_to_bytes(&list_builder);
  CACHE_UNLOCK();

  char key[64] = {0};
  header_cache_storage_key(chain_id, key, sizeof(key));
  plugin.set(key, snapshot.bytes);
  safe_free(snapshot.bytes.data);
}

const ssz_def_t* c4_header_cache_snapshot_def(void) {
  return &HEADER_CACHE_SNAPSHOT;
}

bool c4_header_cache_load(chain_id_t chain_id) {
  storage_plugin_t plugin = {0};
  c4_get_storage_config(&plugin);
  if (!plugin.get) return false;

  char key[64] = {0};
  header_cache_storage_key(chain_id, key, sizeof(key));
  buffer_t buf = {0};
  if (!plugin.get(key, &buf) || !buf.data.data || buf.data.len == 0 ||
      buf.data.len > HEADER_CACHE_MAX_SNAPSHOT_BYTES) {
    buffer_free(&buf);
    return false;
  }

  ssz_ob_t snapshot = {.def = &HEADER_CACHE_SNAPSHOT, .bytes = buf.data};
  if (!ssz_is_valid(snapshot, true, NULL)) {
    buffer_free(&buf);
    return false;
  }

  uint32_t inserted = 0;
  uint32_t n        = ssz_len(snapshot);
  // Cap the loop at the cache capacity: with progressive lists inside the
  // entries, ssz_is_valid does not enforce the list capacity, so a hostile
  // storage blob could otherwise force millions of put()/dup() cycles even
  // though the cache would only retain HEADER_CACHE_SIZE of them.
  if (n > HEADER_CACHE_SIZE) n = HEADER_CACHE_SIZE;
  for (uint32_t i = 0; i < n; i++) {
    ssz_ob_t entry = ssz_at(snapshot, i);
    if (!entry.def) continue;
    ssz_ob_t hash_ob   = ssz_get(&entry, "block_hash");
    ssz_ob_t header_ob = ssz_get(&entry, "el_header");
    if (hash_ob.bytes.len != 32 || header_ob.bytes.len == 0) continue;

    // Re-establish the "cache only holds verified data" invariant across the
    // storage boundary: reject entries where the persisted RLP header does not
    // hash to the persisted block_hash. Without this check, anyone able to
    // write the storage file could poison the cache with fake el_headers that
    // downstream consumers (e.g. verifier/beacon_header.c) treat as trusted.
    bytes32_t recomputed = {0};
    keccak(header_ob.bytes, recomputed);
    if (memcmp(recomputed, hash_ob.bytes.data, 32) != 0) continue;

    uint64_t  block_number = ssz_get_uint64(&entry, "block_number");
    ssz_ob_t  body         = ssz_get(&entry, "body");
    ssz_ob_t  body_ob      = {0};
    ssz_ob_t* body_arg     = NULL;
    if (body.def && body.def->type != SSZ_TYPE_NONE && body.bytes.data) {
      // The verifier / prover only ever accesses the body via
      // `ssz_get(&el_body, "transactions"|"withdrawals")`, so we pin the def to
      // our canonical 2-field container regardless of the original source.
      // NOTE: the body payload itself is not cryptographically bound to the
      // header hash here; consumers that require the body must treat it as
      // hint-only until roots (transactionsRoot / withdrawalsRoot) are
      // re-checked. See header_cache.h for the load-path trust caveat.
      body_ob.def   = &HEADER_CACHE_BODY_UNION[1];
      body_ob.bytes = body.bytes;
      body_arg      = &body_ob;
    }

    c4_header_cache_put(chain_id, block_number, hash_ob.bytes.data, header_ob.bytes, body_arg);
    inserted++;
  }

  buffer_free(&buf);
  return inserted > 0;
}

#endif // EL_HEADER_CACHE
