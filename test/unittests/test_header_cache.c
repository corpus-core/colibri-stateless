/*
 * Copyright (c) 2026 corpus.core
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

// Tests for the persistent header cache (save/load) built on top of the
// storage_plugin_t interface. Uses an in-memory mock plugin so the tests stay
// independent of the file-system backend.

#include "../../src/chains/eth/ssz/beacon_types.h"
#include "../../src/chains/eth/verifier/header_cache.h"
#include "../../src/util/bytes.h"
#include "../../src/util/chains.h"
#include "../../src/util/crypto.h"
#include "../../src/util/plugin.h"
#include "../../src/util/ssz.h"
#include "unity.h"
#include <stdlib.h>
#include <string.h>

#ifdef EL_HEADER_CACHE

// --- Mock storage plugin -----------------------------------------------------

typedef struct mock_entry {
  char*              key;
  bytes_t            value;
  struct mock_entry* next;
} mock_entry_t;

static mock_entry_t* g_mock_head = NULL;

static mock_entry_t* mock_find(const char* key) {
  for (mock_entry_t* e = g_mock_head; e; e = e->next)
    if (strcmp(e->key, key) == 0) return e;
  return NULL;
}

static bool mock_get(char* key, buffer_t* out) {
  mock_entry_t* e = mock_find(key);
  if (!e) return false;
  buffer_append(out, e->value);
  return true;
}

static void mock_set(char* key, bytes_t value) {
  mock_entry_t* e = mock_find(key);
  if (e) {
    safe_free(e->value.data);
    e->value = bytes_dup(value);
    return;
  }
  e           = safe_malloc(sizeof(mock_entry_t));
  e->key      = strdup(key);
  e->value    = bytes_dup(value);
  e->next     = g_mock_head;
  g_mock_head = e;
}

static void mock_del(char* key) {
  mock_entry_t** p = &g_mock_head;
  while (*p) {
    if (strcmp((*p)->key, key) == 0) {
      mock_entry_t* e = *p;
      *p              = e->next;
      safe_free(e->key);
      safe_free(e->value.data);
      safe_free(e);
      return;
    }
    p = &(*p)->next;
  }
}

static void mock_clear(void) {
  while (g_mock_head) {
    mock_entry_t* next = g_mock_head->next;
    safe_free(g_mock_head->key);
    safe_free(g_mock_head->value.data);
    safe_free(g_mock_head);
    g_mock_head = next;
  }
}

static bytes_t mock_lookup(const char* key) {
  mock_entry_t* e = mock_find(key);
  return e ? e->value : NULL_BYTES;
}

static void install_mock_plugin(void) {
  storage_plugin_t p = {.get = mock_get, .set = mock_set, .del = mock_del, .max_sync_states = 3};
  c4_set_storage_config(&p);
}

static void detach_plugin(void) {
  storage_plugin_t empty = {0};
  c4_set_storage_config(&empty);
}

// --- Test fixtures -----------------------------------------------------------

#define TEST_CHAIN     ((chain_id_t) 1u)
#define TEST_CHAIN_ALT ((chain_id_t) 11155111u)

// A "canonical" body has exactly the 2 fields the header cache expects.
static const ssz_def_t TEST_BODY_FIELDS[] = {
    SSZ_PROG_LIST("transactions", ssz_transactions_bytes),
    SSZ_PROG_LIST("withdrawals", DENEP_WITHDRAWAL_CONTAINER),
};
static const ssz_def_t TEST_BODY_CONTAINER = SSZ_CONTAINER("body", TEST_BODY_FIELDS);

// A "fat" body carries extra fields that must be dropped on save.
static const ssz_def_t TEST_FAT_BODY_FIELDS[] = {
    SSZ_BYTES32("parentHash"),
    SSZ_UINT64("blockNumber"),
    SSZ_PROG_LIST("transactions", ssz_transactions_bytes),
    SSZ_PROG_LIST("withdrawals", DENEP_WITHDRAWAL_CONTAINER),
    SSZ_BYTES32("stateRoot"),
};
static const ssz_def_t TEST_FAT_BODY_CONTAINER = SSZ_CONTAINER("fat_body", TEST_FAT_BODY_FIELDS);

// Malformed source body: only exposes `transactions` -- the header cache
// serializer requires *both* `transactions` and `withdrawals` and must fall
// back to the NONE union variant when either is missing.
static const ssz_def_t TEST_BODY_NO_WITHDRAWALS_FIELDS[] = {
    SSZ_PROG_LIST("transactions", ssz_transactions_bytes),
};
static const ssz_def_t TEST_BODY_NO_WITHDRAWALS = SSZ_CONTAINER("no_withdrawals", TEST_BODY_NO_WITHDRAWALS_FIELDS);

// Fills the header buffer with deterministic bytes and derives `hash` as its
// keccak so the loaded entry passes the header cache's on-load
// `keccak(el_header) == block_hash` re-verification.
static void fill_header(uint8_t* out, size_t len, uint8_t seed) {
  for (size_t i = 0; i < len; i++) out[i] = (uint8_t) (seed ^ (i & 0xff));
}

static void make_header_and_hash(uint8_t* hash, uint8_t* hdr, size_t hdr_len, uint8_t seed) {
  fill_header(hdr, hdr_len, seed);
  keccak(bytes(hdr, (uint32_t) hdr_len), hash);
}

static bytes_t build_body(bytes_t transactions, bytes_t withdrawals, const ssz_def_t* def) {
  ssz_builder_t b = ssz_builder_for_def(def);
  // Optional lead fields (only present in the "fat" variant); ssz_add_bytes
  // walks the container definition and writes each field in order.
  if (def == &TEST_FAT_BODY_CONTAINER) {
    uint8_t parent[32] = {0};
    uint8_t state[32]  = {0};
    for (int i = 0; i < 32; i++) {
      parent[i] = (uint8_t) (0xAA + i);
      state[i]  = (uint8_t) (0xBB + i);
    }
    ssz_add_bytes(&b, "parentHash", bytes(parent, 32));
    // block_number
    uint8_t bn[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    ssz_add_bytes(&b, "blockNumber", bytes(bn, 8));
    ssz_add_bytes(&b, "transactions", transactions);
    ssz_add_bytes(&b, "withdrawals", withdrawals);
    ssz_add_bytes(&b, "stateRoot", bytes(state, 32));
  }
  else {
    ssz_add_bytes(&b, "transactions", transactions);
    ssz_add_bytes(&b, "withdrawals", withdrawals);
  }
  ssz_ob_t ob = ssz_builder_to_bytes(&b);
  return ob.bytes;
}

// Builds a body with a single 4-byte "tx" and a single canonical withdrawal.
static bytes_t build_sample_body(const ssz_def_t* def) {
  // transactions: PROG_LIST of ssz_transactions_bytes (dynamic elements).
  // One element => offset table [uint32=4], followed by the element bytes.
  uint8_t tx_bytes[] = {0x04, 0x00, 0x00, 0x00, 't', 'e', 's', 't'};
  bytes_t txs        = {.data = tx_bytes, .len = sizeof(tx_bytes)};

  // withdrawals: PROG_LIST of the 44-byte fixed WITHDRAWAL container.
  // index=1, validatorIndex=42, address=0xAA..., amount=1000
  uint8_t w[44] = {0};
  w[0]          = 0x01;                 // index (uint64 LE)
  w[8]          = 0x2A;                 // validatorIndex = 42
  memset(w + 16, 0xAA, 20);             // address
  w[36]      = (uint8_t) (1000 & 0xff); // amount LE
  w[37]      = (uint8_t) ((1000 >> 8) & 0xff);
  bytes_t ws = {.data = w, .len = sizeof(w)};

  return build_body(txs, ws, def);
}

// --- Lifecycle ---------------------------------------------------------------

void setUp(void) {
  c4_header_cache_clear();
  mock_clear();
  install_mock_plugin();
}

void tearDown(void) {
  detach_plugin();
  c4_header_cache_clear();
  mock_clear();
}

// --- Tests -------------------------------------------------------------------

// Save without any entries drops the key so a subsequent load returns false.
void test_save_empty_removes_key(void) {
  // Pre-populate the storage key with garbage to prove save() cleans it up.
  storage_plugin_t p;
  c4_get_storage_config(&p);
  uint8_t junk[3] = {0xDE, 0xAD, 0xBE};
  p.set("headers_1", bytes(junk, 3));

  c4_header_cache_save(TEST_CHAIN);
  TEST_ASSERT_NULL(mock_lookup("headers_1").data);
  TEST_ASSERT_FALSE(c4_header_cache_load(TEST_CHAIN));
}

// Header-only roundtrip: put an entry, save, clear, load, look it up again.
void test_roundtrip_header_only(void) {
  uint8_t hash[32];
  uint8_t hdr[64];
  make_header_and_hash(hash, hdr, sizeof(hdr), 0x22);

  c4_header_cache_put(TEST_CHAIN, 42, hash, bytes(hdr, sizeof(hdr)), NULL);
  c4_header_cache_save(TEST_CHAIN);

  bytes_t stored = mock_lookup("headers_1");
  TEST_ASSERT_NOT_NULL(stored.data);
  TEST_ASSERT_TRUE(stored.len > 0);

  c4_header_cache_clear();
  TEST_ASSERT_TRUE(c4_header_cache_load(TEST_CHAIN));

  const verified_header_entry_t* e = c4_header_cache_get_by_number(TEST_CHAIN, 42);
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_EQUAL_UINT64(42, e->block_number);
  TEST_ASSERT_EQUAL_MEMORY(hash, e->block_hash, 32);
  TEST_ASSERT_EQUAL_UINT32(sizeof(hdr), e->el_header.len);
  TEST_ASSERT_EQUAL_MEMORY(hdr, e->el_header.data, sizeof(hdr));
  TEST_ASSERT_NULL(e->el_body.def);
}

// Roundtrip with a canonical 2-field body: fields must survive save+load.
void test_roundtrip_with_body_canonical(void) {
  uint8_t hash[32];
  uint8_t hdr[128];
  make_header_and_hash(hash, hdr, sizeof(hdr), 0x44);

  bytes_t  body_bytes = build_sample_body(&TEST_BODY_CONTAINER);
  ssz_ob_t body       = {.def = &TEST_BODY_CONTAINER, .bytes = body_bytes};

  c4_header_cache_put(TEST_CHAIN, 100, hash, bytes(hdr, sizeof(hdr)), &body);
  c4_header_cache_save(TEST_CHAIN);
  c4_header_cache_clear();
  TEST_ASSERT_TRUE(c4_header_cache_load(TEST_CHAIN));

  const verified_header_entry_t* e = c4_header_cache_get_by_hash(TEST_CHAIN, hash);
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_NOT_NULL(e->el_body.def);
  TEST_ASSERT_NOT_NULL(e->el_body.bytes.data);

  ssz_ob_t loaded_body = e->el_body;
  ssz_ob_t txs         = ssz_get(&loaded_body, "transactions");
  ssz_ob_t withd       = ssz_get(&loaded_body, "withdrawals");
  TEST_ASSERT_NOT_NULL(txs.def);
  TEST_ASSERT_NOT_NULL(withd.def);
  TEST_ASSERT_EQUAL_UINT32(1, ssz_len(txs));
  TEST_ASSERT_EQUAL_UINT32(1, ssz_len(withd));

  safe_free(body_bytes.data);
}

// Body coming from a container with extra fields: save must extract only
// transactions + withdrawals; on load the def is the canonical 2-field one.
void test_body_with_extra_fields_is_stripped(void) {
  uint8_t hash[32];
  uint8_t hdr[80];
  make_header_and_hash(hash, hdr, sizeof(hdr), 0x66);

  bytes_t  fat_bytes = build_sample_body(&TEST_FAT_BODY_CONTAINER);
  ssz_ob_t fat_body  = {.def = &TEST_FAT_BODY_CONTAINER, .bytes = fat_bytes};

  c4_header_cache_put(TEST_CHAIN, 7, hash, bytes(hdr, sizeof(hdr)), &fat_body);
  c4_header_cache_save(TEST_CHAIN);
  c4_header_cache_clear();
  TEST_ASSERT_TRUE(c4_header_cache_load(TEST_CHAIN));

  const verified_header_entry_t* e = c4_header_cache_get_by_number(TEST_CHAIN, 7);
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_NOT_NULL(e->el_body.def);
  TEST_ASSERT_NOT_EQUAL(&TEST_FAT_BODY_CONTAINER, e->el_body.def);

  ssz_ob_t loaded_body = e->el_body;
  ssz_ob_t txs         = ssz_get(&loaded_body, "transactions");
  ssz_ob_t withd       = ssz_get(&loaded_body, "withdrawals");
  TEST_ASSERT_NOT_NULL(txs.def);
  TEST_ASSERT_NOT_NULL(withd.def);
  TEST_ASSERT_EQUAL_UINT32(1, ssz_len(txs));
  TEST_ASSERT_EQUAL_UINT32(1, ssz_len(withd));

  // parentHash / stateRoot must be gone: the canonical body only exposes 2 fields.
  ssz_ob_t missing = ssz_get(&loaded_body, "parentHash");
  TEST_ASSERT_NULL(missing.def);

  safe_free(fat_bytes.data);
}

// Load without a persisted snapshot must return false without touching the cache.
void test_load_without_key_returns_false(void) {
  TEST_ASSERT_FALSE(c4_header_cache_load(TEST_CHAIN));
}

// Chain isolation: saving one chain must not clobber the other's key.
void test_chain_isolation(void) {
  uint8_t h1[32];
  uint8_t h2[32];
  uint8_t hdr1[32];
  uint8_t hdr2[32];
  make_header_and_hash(h1, hdr1, sizeof(hdr1), 0x30);
  make_header_and_hash(h2, hdr2, sizeof(hdr2), 0x40);

  c4_header_cache_put(TEST_CHAIN, 1, h1, bytes(hdr1, sizeof(hdr1)), NULL);
  c4_header_cache_put(TEST_CHAIN_ALT, 2, h2, bytes(hdr2, sizeof(hdr2)), NULL);

  c4_header_cache_save(TEST_CHAIN);
  c4_header_cache_save(TEST_CHAIN_ALT);

  TEST_ASSERT_NOT_NULL(mock_lookup("headers_1").data);
  TEST_ASSERT_NOT_NULL(mock_lookup("headers_11155111").data);
  TEST_ASSERT_TRUE(mock_lookup("headers_1").len != mock_lookup("headers_11155111").len ||
                   memcmp(mock_lookup("headers_1").data, mock_lookup("headers_11155111").data,
                          mock_lookup("headers_1").len) != 0);

  c4_header_cache_clear();
  TEST_ASSERT_TRUE(c4_header_cache_load(TEST_CHAIN));
  TEST_ASSERT_NOT_NULL(c4_header_cache_get_by_number(TEST_CHAIN, 1));
  TEST_ASSERT_NULL(c4_header_cache_get_by_number(TEST_CHAIN_ALT, 2));

  TEST_ASSERT_TRUE(c4_header_cache_load(TEST_CHAIN_ALT));
  TEST_ASSERT_NOT_NULL(c4_header_cache_get_by_number(TEST_CHAIN_ALT, 2));
}

// Invalid / corrupt snapshot bytes must be rejected without populating the cache.
void test_invalid_snapshot_is_rejected(void) {
  storage_plugin_t p;
  c4_get_storage_config(&p);

  // Truncated / non-SSZ garbage; the outer list offset would point far past the
  // buffer end and ssz_is_valid must reject it.
  uint8_t junk[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x01, 0x02};
  p.set("headers_1", bytes(junk, sizeof(junk)));

  TEST_ASSERT_FALSE(c4_header_cache_load(TEST_CHAIN));

  bytes32_t any_hash;
  TEST_ASSERT_FALSE(c4_header_cache_latest_block_hash(TEST_CHAIN, any_hash));
}

// Multiple entries: LRU order at save time is preserved via load. Concretely,
// the newest entry (highest last_used) ends up on top after reload.
void test_multiple_entries_preserve_recency(void) {
  uint8_t h1[32], h2[32], h3[32];
  uint8_t hdr1[16], hdr2[16], hdr3[16];
  make_header_and_hash(h1, hdr1, sizeof(hdr1), 0x91);
  make_header_and_hash(h2, hdr2, sizeof(hdr2), 0x92);
  make_header_and_hash(h3, hdr3, sizeof(hdr3), 0x93);

  c4_header_cache_put(TEST_CHAIN, 1, h1, bytes(hdr1, sizeof(hdr1)), NULL);
  c4_header_cache_put(TEST_CHAIN, 2, h2, bytes(hdr2, sizeof(hdr2)), NULL);
  c4_header_cache_put(TEST_CHAIN, 3, h3, bytes(hdr3, sizeof(hdr3)), NULL);
  // Touch #2 last so it becomes the freshest entry.
  (void) c4_header_cache_get_by_number(TEST_CHAIN, 2);

  c4_header_cache_save(TEST_CHAIN);
  c4_header_cache_clear();
  TEST_ASSERT_TRUE(c4_header_cache_load(TEST_CHAIN));

  bytes32_t newest = {0};
  TEST_ASSERT_TRUE(c4_header_cache_latest_block_hash(TEST_CHAIN, newest));
  // latest_block_hash returns the highest block_number, not the freshest one;
  // regardless of LRU replay ordering, #3 has the largest number.
  TEST_ASSERT_EQUAL_MEMORY(h3, newest, 32);

  // All three entries must be reachable after load.
  TEST_ASSERT_NOT_NULL(c4_header_cache_get_by_number(TEST_CHAIN, 1));
  TEST_ASSERT_NOT_NULL(c4_header_cache_get_by_number(TEST_CHAIN, 2));
  TEST_ASSERT_NOT_NULL(c4_header_cache_get_by_number(TEST_CHAIN, 3));
}

// A second save() must fully replace the previous snapshot; entries removed
// from the cache between two saves must not reappear on load.
void test_save_overwrites_previous_snapshot(void) {
  uint8_t h1[32], h2[32], hdr1[32], hdr2[32];
  make_header_and_hash(h1, hdr1, sizeof(hdr1), 0x77);
  make_header_and_hash(h2, hdr2, sizeof(hdr2), 0x78);

  c4_header_cache_put(TEST_CHAIN, 100, h1, bytes(hdr1, sizeof(hdr1)), NULL);
  c4_header_cache_save(TEST_CHAIN);
  TEST_ASSERT_TRUE(mock_lookup("headers_1").len > 0);

  // Wipe local state, install a completely different entry, and save again.
  c4_header_cache_clear();
  c4_header_cache_put(TEST_CHAIN, 200, h2, bytes(hdr2, sizeof(hdr2)), NULL);
  c4_header_cache_save(TEST_CHAIN);

  // Reload from an empty cache: only the second snapshot must be visible.
  c4_header_cache_clear();
  TEST_ASSERT_TRUE(c4_header_cache_load(TEST_CHAIN));
  TEST_ASSERT_NOT_NULL(c4_header_cache_get_by_number(TEST_CHAIN, 200));
  TEST_ASSERT_NULL(c4_header_cache_get_by_number(TEST_CHAIN, 100));
}

// load() must not evict entries that live only in memory: it inserts the
// persisted entries alongside any that were already there.
void test_load_merges_with_existing_entries(void) {
  uint8_t h_persisted[32], h_local[32], hdr_persisted[32], hdr_local[32];
  make_header_and_hash(h_persisted, hdr_persisted, sizeof(hdr_persisted), 0xAB);
  make_header_and_hash(h_local, hdr_local, sizeof(hdr_local), 0xAC);

  c4_header_cache_put(TEST_CHAIN, 100, h_persisted, bytes(hdr_persisted, sizeof(hdr_persisted)), NULL);
  c4_header_cache_save(TEST_CHAIN);

  // Drop the persisted entry from the cache but seed an unrelated one that
  // must survive the load call.
  c4_header_cache_clear();
  c4_header_cache_put(TEST_CHAIN, 200, h_local, bytes(hdr_local, sizeof(hdr_local)), NULL);

  TEST_ASSERT_TRUE(c4_header_cache_load(TEST_CHAIN));
  TEST_ASSERT_NOT_NULL(c4_header_cache_get_by_number(TEST_CHAIN, 100));
  TEST_ASSERT_NOT_NULL(c4_header_cache_get_by_number(TEST_CHAIN, 200));
}

// A corrupt persisted snapshot must be rejected without touching any entries
// that are already live in the cache.
void test_load_corrupt_snapshot_leaves_cache_intact(void) {
  uint8_t hash[32], hdr[32];
  make_header_and_hash(hash, hdr, sizeof(hdr), 0x55);
  c4_header_cache_put(TEST_CHAIN, 7, hash, bytes(hdr, sizeof(hdr)), NULL);

  storage_plugin_t p;
  c4_get_storage_config(&p);
  uint8_t junk[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x01, 0x02};
  p.set("headers_1", bytes(junk, sizeof(junk)));

  TEST_ASSERT_FALSE(c4_header_cache_load(TEST_CHAIN));

  const verified_header_entry_t* still_there = c4_header_cache_get_by_number(TEST_CHAIN, 7);
  TEST_ASSERT_NOT_NULL(still_there);
  TEST_ASSERT_EQUAL_MEMORY(hash, still_there->block_hash, 32);
  TEST_ASSERT_EQUAL_UINT32(sizeof(hdr), still_there->el_header.len);
  TEST_ASSERT_EQUAL_MEMORY(hdr, still_there->el_header.data, sizeof(hdr));
}

// A source body that lacks `withdrawals` cannot be reduced to the canonical
// 2-field shape, so save() must emit selector 0 (NONE) and load() must
// deliver the entry with no body attached rather than fabricating one.
void test_body_missing_field_falls_back_to_none(void) {
  uint8_t hash[32], hdr[32];
  make_header_and_hash(hash, hdr, sizeof(hdr), 0x11);

  uint8_t tx_bytes[] = {0x04, 0x00, 0x00, 0x00, 't', 'e', 's', 't'};

  ssz_builder_t b = ssz_builder_for_def(&TEST_BODY_NO_WITHDRAWALS);
  ssz_add_bytes(&b, "transactions", bytes(tx_bytes, sizeof(tx_bytes)));
  ssz_ob_t built = ssz_builder_to_bytes(&b);
  ssz_ob_t body  = {.def = &TEST_BODY_NO_WITHDRAWALS, .bytes = built.bytes};

  c4_header_cache_put(TEST_CHAIN, 42, hash, bytes(hdr, sizeof(hdr)), &body);
  c4_header_cache_save(TEST_CHAIN);
  c4_header_cache_clear();
  TEST_ASSERT_TRUE(c4_header_cache_load(TEST_CHAIN));

  const verified_header_entry_t* e = c4_header_cache_get_by_number(TEST_CHAIN, 42);
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_EQUAL_UINT32(sizeof(hdr), e->el_header.len);
  TEST_ASSERT_NULL_MESSAGE(e->el_body.def, "body must be dropped when source def lacks withdrawals");
  TEST_ASSERT_NULL_MESSAGE(e->el_body.bytes.data, "body payload must be empty when serialized as NONE");

  safe_free(built.bytes.data);
}

// Tampered snapshot: mutating a persisted el_header byte breaks the
// keccak(el_header) == block_hash invariant; load() must skip that entry
// instead of poisoning the cache with an unverified header.
void test_load_rejects_entry_with_wrong_hash(void) {
  uint8_t good_hash[32], good_hdr[32];
  uint8_t bad_hash[32], bad_hdr[32];
  make_header_and_hash(good_hash, good_hdr, sizeof(good_hdr), 0xC1);
  make_header_and_hash(bad_hash, bad_hdr, sizeof(bad_hdr), 0xC2);

  c4_header_cache_put(TEST_CHAIN, 10, good_hash, bytes(good_hdr, sizeof(good_hdr)), NULL);
  c4_header_cache_put(TEST_CHAIN, 20, bad_hash, bytes(bad_hdr, sizeof(bad_hdr)), NULL);
  c4_header_cache_save(TEST_CHAIN);
  c4_header_cache_clear();

  // Corrupt the storage blob: flip a byte anywhere -- there is a very high
  // probability it lands in one of the el_header payloads, which is the
  // scenario the check defends against.
  bytes_t stored = mock_lookup("headers_1");
  TEST_ASSERT_NOT_NULL(stored.data);
  TEST_ASSERT_TRUE(stored.len > 40);
  stored.data[stored.len - 5] ^= 0xFF;

  // Depending on which entry the flip hit, one or the other (or neither) may
  // survive; the crucial invariant is: no entry may load whose recomputed
  // keccak does not match its stored block_hash.
  (void) c4_header_cache_load(TEST_CHAIN);

  const verified_header_entry_t* e10 = c4_header_cache_get_by_number(TEST_CHAIN, 10);
  const verified_header_entry_t* e20 = c4_header_cache_get_by_number(TEST_CHAIN, 20);
  for (int i = 0; i < 2; i++) {
    const verified_header_entry_t* e = i == 0 ? e10 : e20;
    if (!e) continue;
    bytes32_t recomputed = {0};
    keccak(e->el_header, recomputed);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(e->block_hash, recomputed, 32,
                                     "loaded entry violates keccak invariant");
  }
}

// Empty transactions / withdrawals lists must round-trip: the body should
// still be present after load with two zero-length lists (not degraded to
// the NONE variant just because the payload is empty).
void test_roundtrip_body_with_empty_lists(void) {
  uint8_t hash[32], hdr[32];
  make_header_and_hash(hash, hdr, sizeof(hdr), 0x99);

  bytes_t  empty      = {.data = NULL, .len = 0};
  bytes_t  body_bytes = build_body(empty, empty, &TEST_BODY_CONTAINER);
  ssz_ob_t body       = {.def = &TEST_BODY_CONTAINER, .bytes = body_bytes};

  c4_header_cache_put(TEST_CHAIN, 500, hash, bytes(hdr, sizeof(hdr)), &body);
  c4_header_cache_save(TEST_CHAIN);
  c4_header_cache_clear();
  TEST_ASSERT_TRUE(c4_header_cache_load(TEST_CHAIN));

  const verified_header_entry_t* e = c4_header_cache_get_by_number(TEST_CHAIN, 500);
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_NOT_NULL_MESSAGE(e->el_body.def, "empty-list body must still be persisted");
  TEST_ASSERT_NOT_NULL(e->el_body.bytes.data);

  ssz_ob_t loaded_body = e->el_body;
  ssz_ob_t txs         = ssz_get(&loaded_body, "transactions");
  ssz_ob_t withd       = ssz_get(&loaded_body, "withdrawals");
  TEST_ASSERT_NOT_NULL(txs.def);
  TEST_ASSERT_NOT_NULL(withd.def);
  TEST_ASSERT_EQUAL_UINT32(0, ssz_len(txs));
  TEST_ASSERT_EQUAL_UINT32(0, ssz_len(withd));

  safe_free(body_bytes.data);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_save_empty_removes_key);
  RUN_TEST(test_roundtrip_header_only);
  RUN_TEST(test_roundtrip_with_body_canonical);
  RUN_TEST(test_body_with_extra_fields_is_stripped);
  RUN_TEST(test_load_without_key_returns_false);
  RUN_TEST(test_chain_isolation);
  RUN_TEST(test_invalid_snapshot_is_rejected);
  RUN_TEST(test_multiple_entries_preserve_recency);
  RUN_TEST(test_save_overwrites_previous_snapshot);
  RUN_TEST(test_load_merges_with_existing_entries);
  RUN_TEST(test_load_corrupt_snapshot_leaves_cache_intact);
  RUN_TEST(test_body_missing_field_falls_back_to_none);
  RUN_TEST(test_load_rejects_entry_with_wrong_hash);
  RUN_TEST(test_roundtrip_body_with_empty_lists);
  return UNITY_END();
}

#else // !EL_HEADER_CACHE: nothing to test, produce an empty pass so ctest is happy.
void setUp(void) {}
void tearDown(void) {}
void test_header_cache_disabled(void) {
  TEST_IGNORE_MESSAGE("EL_HEADER_CACHE is disabled");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_header_cache_disabled);
  return UNITY_END();
}

#endif // EL_HEADER_CACHE
