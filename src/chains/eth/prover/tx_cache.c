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

#include "tx_cache.h"
#include "beacon.h"
#include "beacon_types.h"
#include "crypto.h"
#include "eth_req.h"
#include "eth_tools.h"
#include "historic_proof.h"
#include "json.h"
#include "logger.h"
#include "prover.h"
#include "ssz.h"
#include "sync_committee.h"
#include "version.h"
#include <inttypes.h> // Include this header for PRIu64 and PRIx64
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef PROVER_CACHE
// Default maximum entries; can be adjusted at runtime via API
static size_t g_max_tx_cache_size = 10000u;
typedef struct { // cache key for the transaction proof
  uint64_t block_number;
  uint32_t tx_index;
} eth_tx_cache_value_t;

// Fixed-size open addressing hash table for Tx cache.
// Capacity is a power-of-two for efficient masking.
#define TABLE_CAPACITY 16384u

typedef struct {
  bool      used;
  bytes32_t key; // 32-byte tx hash
  uint64_t  block_number;
  uint32_t  tx_index;
} tx_entry_t;

static tx_entry_t g_table[TABLE_CAPACITY];
static size_t     g_size = 0; // number of used entries

typedef struct block_node_s {
  uint64_t             block_number;
  bytes32_t*           items; // dynamic array of tx hashes for this block
  uint32_t             count;
  uint32_t             cap;
  struct block_node_s* prev;
  struct block_node_s* next;
} block_node_t;

static block_node_t* g_head = NULL; // oldest block
static block_node_t* g_tail = NULL; // newest block

// --- Hashing helpers (fast 64-bit mix; not cryptographic) ---
static inline uint64_t rotl64(uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }
static inline uint64_t mix64(uint64_t x) {
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9ULL;
  x ^= x >> 27;
  x *= 0x94d049bb133111ebULL;
  x ^= x >> 31;
  return x;
}

static inline uint64_t hash_bytes32(const bytes32_t key) {
  // Use first 8 bytes directly (key is already a Keccak hash)
  uint64_t w0;
  memcpy(&w0, key + 0, 8);
  return w0;
}

static inline size_t table_index(uint64_t h) {
  return (size_t) (h & (TABLE_CAPACITY - 1u));
}

static inline bool key_equals(const bytes32_t a, const bytes32_t b) {
  return memcmp(a, b, BYTES32_SIZE) == 0;
}

// Find index of key, or return (size_t)(-1) if not found
static size_t table_find_index(const bytes32_t key) {
  size_t i = table_index(hash_bytes32(key));
  for (;;) {
    if (!g_table[i].used) {
      return (size_t) (-1);
    }
    if (key_equals(g_table[i].key, key)) {
      return i;
    }
    i = (i + 1u) & (TABLE_CAPACITY - 1u);
  }
}

// Backshift deletion starting from position pos
static void table_delete_at(size_t pos) {
  size_t i = pos;
  size_t j = (i + 1u) & (TABLE_CAPACITY - 1u);
  while (g_table[j].used) {
    uint64_t h    = hash_bytes32(g_table[j].key);
    size_t   home = table_index(h);
    // If entry is in its home bucket, stop shifting
    if (home == j) {
      break;
    }
    g_table[i] = g_table[j];
    i          = j;
    j          = (j + 1u) & (TABLE_CAPACITY - 1u);
  }
  g_table[i].used = false;
}

// Insert or update. Returns true if inserted as new key, false if updated.
static bool table_set(const bytes32_t key, uint64_t block_number, uint32_t tx_index) {
  size_t i = table_index(hash_bytes32(key));
  for (;;) {
    if (!g_table[i].used) {
      g_table[i].used = true;
      memcpy(g_table[i].key, key, BYTES32_SIZE);
      g_table[i].block_number = block_number;
      g_table[i].tx_index     = tx_index;
      g_size++;
      return true;
    }
    if (key_equals(g_table[i].key, key)) {
      g_table[i].block_number = block_number;
      g_table[i].tx_index     = tx_index;
      return false;
    }
    i = (i + 1u) & (TABLE_CAPACITY - 1u);
  }
}

static bool table_get(const bytes32_t key, uint64_t* block_number, uint32_t* tx_index) {
  size_t idx = table_find_index(key);
  if (idx == (size_t) (-1)) return false;
  if (block_number) *block_number = g_table[idx].block_number;
  if (tx_index) *tx_index = g_table[idx].tx_index;
  return true;
}

static bool table_remove(const bytes32_t key) {
  size_t idx = table_find_index(key);
  if (idx == (size_t) (-1)) return false;
  table_delete_at(idx);
  if (g_size > 0) g_size--;
  return true;
}

// --- FIFO block list helpers ---
static block_node_t* create_block_node(uint64_t block_number) {
  block_node_t* n = (block_node_t*) malloc(sizeof(block_node_t));
  if (!n) return NULL;
  n->block_number = block_number;
  n->items        = NULL;
  n->count        = 0;
  n->cap          = 0;
  n->prev         = g_tail;
  n->next         = NULL;
  if (g_tail)
    g_tail->next = n;
  else
    g_head = n;
  g_tail = n;
  return n;
}

static block_node_t* ensure_tail_block(uint64_t block_number) {
  if (g_tail && g_tail->block_number == block_number) {
    return g_tail;
  }
  return create_block_node(block_number);
}

static void block_items_push(block_node_t* node, const bytes32_t key) {
  if (node->count == node->cap) {
    uint32_t   new_cap   = node->cap ? (node->cap * 2u) : 256u; // amortized growth
    bytes32_t* new_items = (bytes32_t*) realloc(node->items, (size_t) new_cap * sizeof(bytes32_t));
    if (!new_items) return; // drop on OOM (cache best-effort)
    node->items = new_items;
    node->cap   = new_cap;
  }
  memcpy(node->items[node->count], key, BYTES32_SIZE);
  node->count++;
}

static void free_block_node(block_node_t* node) {
  if (!node) return;
  if (node->items) free(node->items);
  free(node);
}

// remove as many entries as needed, so the number_of_entries_to_add can be added.
static void clean_up_cache(int number_of_entries_to_add) {
  // Evict whole blocks from the head until there is enough room
  while (g_size + (size_t) number_of_entries_to_add > g_max_tx_cache_size) {
    if (!g_head) break; // nothing to evict
    block_node_t* victim = g_head;
    // remove all txs of this block from the table
    for (uint32_t i = 0; i < victim->count; i++) {
      table_remove(victim->items[i]);
    }
    // unlink victim
    g_head = victim->next;
    if (g_head)
      g_head->prev = NULL;
    else
      g_tail = NULL;
    free_block_node(victim);
  }
}

void c4_eth_tx_cache_set(bytes32_t tx_hash, uint64_t block_number, uint32_t tx_index) {
  // Best-effort pre-evict if full and we are adding a new entry.
  if (g_size >= g_max_tx_cache_size) {
    clean_up_cache(1);
  }
  bool is_new = table_set(tx_hash, block_number, tx_index);
  if (is_new) {
    block_node_t* node = ensure_tail_block(block_number);
    if (node) {
      block_items_push(node, tx_hash);
    }
  }
}

bool c4_eth_tx_cache_get(bytes32_t tx_hash, uint64_t* block_number, uint32_t* tx_index) {
  bool table_get(tx_hash, block_number, tx_index);
}

void c4_eth_tx_cache_reset(void) {
  // Clear table state
  for (size_t i = 0; i < TABLE_CAPACITY; i++) {
    g_table[i].used = false;
  }
  g_size = 0;

  // Free all block nodes
  block_node_t* n = g_head;
  while (n) {
    block_node_t* next = n->next;
    free_block_node(n);
    n = next;
  }
  g_head = NULL;
  g_tail = NULL;
}

size_t c4_eth_tx_cache_size(void) {
  return g_size;
}

void c4_eth_tx_cache_reserve(uint32_t number_of_entries_to_add) {
  // Batch-evict to ensure capacity for upcoming inserts
  if ((int) number_of_entries_to_add <= 0) return;
  clean_up_cache((int) number_of_entries_to_add);
}

void c4_eth_tx_cache_set_max_size(uint32_t max) {
  if (max == 0) max = 1; // avoid zero which breaks invariants
  g_max_tx_cache_size = (size_t) max;
  // Ensure we do not exceed the new limit
  clean_up_cache(0);
}

size_t c4_eth_tx_cache_capacity(void) {
  return g_max_tx_cache_size;
}

#endif
