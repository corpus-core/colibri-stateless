/*
 * Copyright (c) 2025,2026 corpus.core
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

// Tests for the generic adaptive retry-delay learner (`retry_delay.h`).
//
// Covers:
//   - default base when no value is persisted
//   - persist + reload via a mock storage_plugin_t (in-memory map)
//   - asymmetric adaptation: fast up (UP_SHIFT = 1/2), slow down (DOWN_SHIFT = 1/8)
//   - bounds clamping at [MIN, MAX]
//   - rejection of corrupt/unknown storage blobs (fallback to default)
//   - category and chain separation
//   - safety against NULL category

#include "bytes.h"
#include "chains.h"
#include "plugin.h"
#include "retry_delay.h"
#include "unity.h"
#include <stdlib.h>
#include <string.h>

// --- Mock storage plugin -----------------------------------------------------

typedef struct mock_entry {
  char*              key;
  bytes_t            value;
  struct mock_entry* next;
} mock_entry_t;

static mock_entry_t* g_mock_head;
static int           g_mock_set_calls;

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
  g_mock_set_calls++;
  mock_entry_t* e = mock_find(key);
  if (e) {
    safe_free(e->value.data);
    e->value = bytes_dup(value);
    return;
  }
  e        = safe_malloc(sizeof(mock_entry_t));
  e->key   = strdup(key);
  e->value = bytes_dup(value);
  e->next  = g_mock_head;
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
  g_mock_set_calls = 0;
}

static void install_mock_plugin(void) {
  storage_plugin_t p = {.get = mock_get, .set = mock_set, .del = mock_del, .max_sync_states = 3};
  c4_set_storage_config(&p);
}

static void detach_plugin(void) {
  storage_plugin_t empty = {0};
  c4_set_storage_config(&empty);
}

#define TEST_CATEGORY      "test_cat"
#define TEST_CATEGORY_ALT  "other_cat"
#define TEST_CHAIN         ((chain_id_t) 11155111u)
#define TEST_CHAIN_ALT     ((chain_id_t) 1u)
#define EXPECTED_KEY       "rdelay_test_cat_11155111"

void setUp(void) {
  c4_retry_delay_reset();
  mock_clear();
  install_mock_plugin();
}

void tearDown(void) {
  detach_plugin();
  c4_retry_delay_reset();
  mock_clear();
}

// --- default & backoff -------------------------------------------------------

void test_default_when_storage_empty(void) {
  TEST_ASSERT_EQUAL_UINT32(C4_RETRY_DELAY_DEFAULT_MS, c4_retry_delay_for(TEST_CATEGORY, TEST_CHAIN, 0));
  TEST_ASSERT_EQUAL_UINT32(C4_RETRY_DELAY_DEFAULT_MS * 2, c4_retry_delay_for(TEST_CATEGORY, TEST_CHAIN, 1));
  TEST_ASSERT_EQUAL_UINT32(C4_RETRY_DELAY_MAX_MS, c4_retry_delay_for(TEST_CATEGORY, TEST_CHAIN, 100));
}

// --- asymmetric adaptation ---------------------------------------------------

void test_observe_R1_probes_down_slowly(void) {
  // R == 1 only proves T_warm <= base; the learner treats it as evidence
  // that the current base may be too generous and probes down toward the
  // midpoint of (0, base]. With default base = 1000:
  //   target = base/2 = 500. delta = 500. step = ceil(500/8) = 63.
  //   new_base = 1000 - 63 = 937.
  TEST_ASSERT_EQUAL_UINT32(C4_RETRY_DELAY_DEFAULT_MS, c4_retry_delay_for(TEST_CATEGORY, TEST_CHAIN, 0));
  c4_retry_delay_observe(TEST_CATEGORY, TEST_CHAIN, 1);
  TEST_ASSERT_EQUAL_UINT32(937, c4_retry_delay_for(TEST_CATEGORY, TEST_CHAIN, 0));
  TEST_ASSERT_EQUAL_INT(1, g_mock_set_calls);
}

void test_observe_R2_jumps_up_fast(void) {
  // R == 2 -> target = 3 * base = 3000. delta = 2000. UP_SHIFT=1 -> +1000.
  // New base = 2000 (doubled). Verified via the backoff sequence.
  c4_retry_delay_observe(TEST_CATEGORY, TEST_CHAIN, 2);
  TEST_ASSERT_EQUAL_UINT32(2000, c4_retry_delay_for(TEST_CATEGORY, TEST_CHAIN, 0));
  // Persisted.
  TEST_ASSERT_EQUAL_INT(1, g_mock_set_calls);
}

void test_observe_R0_probes_down(void) {
  // Start the learner at a known high value by feeding two R=2 observations.
  c4_retry_delay_observe(TEST_CATEGORY, TEST_CHAIN, 2); // 1000 -> 2000
  c4_retry_delay_observe(TEST_CATEGORY, TEST_CHAIN, 2); // 2000 -> 4000
  TEST_ASSERT_EQUAL_UINT32(4000, c4_retry_delay_for(TEST_CATEGORY, TEST_CHAIN, 0));

  // R == 0 -> target = base/2 = 2000. delta = 2000. DOWN_SHIFT=3 -> -250.
  // New base = 3750.
  c4_retry_delay_observe(TEST_CATEGORY, TEST_CHAIN, 0);
  TEST_ASSERT_EQUAL_UINT32(3750, c4_retry_delay_for(TEST_CATEGORY, TEST_CHAIN, 0));
}

void test_repeated_high_R_saturates_at_max(void) {
  // Feeding very large R values must drive base to C4_RETRY_DELAY_MAX_MS and
  // stay there (no overflow, no oscillation).
  for (int i = 0; i < 50; i++) c4_retry_delay_observe(TEST_CATEGORY, TEST_CHAIN, 8);
  TEST_ASSERT_EQUAL_UINT32(C4_RETRY_DELAY_MAX_MS, c4_retry_delay_for(TEST_CATEGORY, TEST_CHAIN, 0));
}

void test_repeated_R0_floors_at_min(void) {
  // Many R == 0 observations should converge to (but never breach) the floor.
  for (int i = 0; i < 1000; i++) c4_retry_delay_observe(TEST_CATEGORY, TEST_CHAIN, 0);
  uint32_t v = c4_retry_delay_for(TEST_CATEGORY, TEST_CHAIN, 0);
  TEST_ASSERT_TRUE_MESSAGE(v >= C4_RETRY_DELAY_MIN_MS, "base must respect MIN floor");
  TEST_ASSERT_TRUE_MESSAGE(v <= C4_RETRY_DELAY_DEFAULT_MS, "base must have moved downward");
}

// --- persistence -------------------------------------------------------------

void test_persist_and_reload(void) {
  c4_retry_delay_observe(TEST_CATEGORY, TEST_CHAIN, 3); // base 1000 -> 4000
  TEST_ASSERT_EQUAL_UINT32(4000, c4_retry_delay_for(TEST_CATEGORY, TEST_CHAIN, 0));

  // A blob should have been written under the documented key.
  TEST_ASSERT_NOT_NULL(mock_find(EXPECTED_KEY));

  // Drop the in-memory cache: the next `for` call must re-load from storage
  // and surface the same value.
  c4_retry_delay_reset();
  TEST_ASSERT_EQUAL_UINT32(4000, c4_retry_delay_for(TEST_CATEGORY, TEST_CHAIN, 0));
}

void test_corrupt_blob_falls_back_to_default(void) {
  // Wrong size.
  uint8_t junk[3] = {0xff, 0x01, 0x02};
  mock_set(EXPECTED_KEY, bytes(junk, sizeof(junk)));
  c4_retry_delay_reset();
  TEST_ASSERT_EQUAL_UINT32(C4_RETRY_DELAY_DEFAULT_MS, c4_retry_delay_for(TEST_CATEGORY, TEST_CHAIN, 0));

  // Right size, unknown version.
  uint8_t bad_ver[5] = {0xff, 0xe8, 0x03, 0x00, 0x00};
  mock_del(EXPECTED_KEY);
  mock_set(EXPECTED_KEY, bytes(bad_ver, sizeof(bad_ver)));
  c4_retry_delay_reset();
  TEST_ASSERT_EQUAL_UINT32(C4_RETRY_DELAY_DEFAULT_MS, c4_retry_delay_for(TEST_CATEGORY, TEST_CHAIN, 0));
}

void test_persisted_value_is_clamped_on_read(void) {
  // Persisted base way above MAX should be clamped on load.
  uint8_t huge[5] = {0x01, 0xff, 0xff, 0xff, 0x7f};
  mock_set(EXPECTED_KEY, bytes(huge, sizeof(huge)));
  c4_retry_delay_reset();
  TEST_ASSERT_EQUAL_UINT32(C4_RETRY_DELAY_MAX_MS, c4_retry_delay_for(TEST_CATEGORY, TEST_CHAIN, 0));

  // Persisted base below MIN should be clamped upward.
  uint8_t tiny[5] = {0x01, 0x01, 0x00, 0x00, 0x00};
  mock_del(EXPECTED_KEY);
  mock_set(EXPECTED_KEY, bytes(tiny, sizeof(tiny)));
  c4_retry_delay_reset();
  TEST_ASSERT_EQUAL_UINT32(C4_RETRY_DELAY_MIN_MS, c4_retry_delay_for(TEST_CATEGORY, TEST_CHAIN, 0));
}

// --- key isolation -----------------------------------------------------------

void test_categories_learn_independently(void) {
  c4_retry_delay_observe(TEST_CATEGORY, TEST_CHAIN, 3);     // boost cat A
  c4_retry_delay_observe(TEST_CATEGORY_ALT, TEST_CHAIN, 0); // probe cat B down
  uint32_t a = c4_retry_delay_for(TEST_CATEGORY, TEST_CHAIN, 0);
  uint32_t b = c4_retry_delay_for(TEST_CATEGORY_ALT, TEST_CHAIN, 0);
  TEST_ASSERT_TRUE(a > C4_RETRY_DELAY_DEFAULT_MS);
  TEST_ASSERT_TRUE(b < C4_RETRY_DELAY_DEFAULT_MS);
}

void test_chains_learn_independently(void) {
  c4_retry_delay_observe(TEST_CATEGORY, TEST_CHAIN, 3);
  // The alt chain must still report the default; the learner is keyed by chain too.
  TEST_ASSERT_EQUAL_UINT32(C4_RETRY_DELAY_DEFAULT_MS, c4_retry_delay_for(TEST_CATEGORY, TEST_CHAIN_ALT, 0));
}

// --- defensive ---------------------------------------------------------------

void test_null_category_is_safe(void) {
  TEST_ASSERT_EQUAL_UINT32(C4_RETRY_DELAY_DEFAULT_MS, c4_retry_delay_for(NULL, TEST_CHAIN, 0));
  c4_retry_delay_observe(NULL, TEST_CHAIN, 5); // must not crash, must not persist
  TEST_ASSERT_EQUAL_INT(0, g_mock_set_calls);
}

// --- partial / absent storage plugin -----------------------------------------

void test_observe_with_only_get_plugin_does_not_crash(void) {
  // Replace the full mock with a get-only plugin (set==NULL). `observe` must
  // detect the missing setter and skip persistence -- otherwise a NULL deref
  // would crash the host. The in-memory cache still updates so subsequent
  // `for` calls return the freshly learned value.
  storage_plugin_t getter_only = {.get = mock_get, .set = NULL, .del = NULL};
  c4_set_storage_config(&getter_only);

  c4_retry_delay_observe(TEST_CATEGORY, TEST_CHAIN, 2); // 1000 -> 2000
  TEST_ASSERT_EQUAL_UINT32(2000, c4_retry_delay_for(TEST_CATEGORY, TEST_CHAIN, 0));
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_mock_set_calls,
                                "no setter installed -> persist_entry must be a no-op");
}

void test_null_get_plugin_is_overridden_by_default_storage(void) {
  // Surfaces a non-obvious interaction between retry_delay and plugin.c:
  // c4_get_storage_config() auto-installs the compile-time default backend
  // (MEMORY_STORAGE or FILE_STORAGE) whenever storage_conf.get is NULL --
  // regardless of what the host passed to c4_set_storage_config. As a
  // consequence, a "set-only" plugin (get == NULL, set != NULL) is silently
  // replaced and the host's set callback is never invoked by retry_delay.
  //
  // This test pins the observation so a future refactor of the fallback
  // logic is forced to consider how partial plugins interact with the
  // retry_delay learner.
  storage_plugin_t set_only = {.get = NULL, .set = mock_set, .del = NULL};
  c4_set_storage_config(&set_only);

  c4_retry_delay_observe(TEST_CATEGORY, TEST_CHAIN, 2);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_mock_set_calls,
                                "c4_get_storage_config auto-installs default storage when .get is NULL "
                                "-> the host-supplied set callback is bypassed");
}

// --- cache eviction (> RETRY_DELAY_CACHE_SIZE distinct (category, chain)) ----

void test_cache_eviction_round_trips_via_storage(void) {
  // RETRY_DELAY_CACHE_SIZE = 8. Register 8 distinct chains for the same
  // category; each observe(R=2) drives the base from 1000 to 2000 and
  // persists. All 8 slots are then in use.
  for (uint64_t c = 100; c < 108; c++)
    c4_retry_delay_observe(TEST_CATEGORY, (chain_id_t) c, 2);
  TEST_ASSERT_EQUAL_INT(8, g_mock_set_calls);

  // Adding a 9th distinct chain triggers the pathological-case branch in
  // lookup_entry, which reuses slot 0. The entry originally in slot 0
  // (chain 100) is dropped from the in-memory cache but its persisted blob
  // survives in storage.
  c4_retry_delay_observe(TEST_CATEGORY, (chain_id_t) 108, 2);

  // Reading chain 100 must round-trip through the storage plugin and surface
  // the previously learned base. (The lookup itself evicts whatever sits in
  // slot 0 right now, but the persisted blob makes that lossless.)
  TEST_ASSERT_EQUAL_UINT32(2000, c4_retry_delay_for(TEST_CATEGORY, (chain_id_t) 100, 0));

  // The 9th chain's value must also be readable.
  TEST_ASSERT_EQUAL_UINT32(2000, c4_retry_delay_for(TEST_CATEGORY, (chain_id_t) 108, 0));
}

// --- saturation guards -------------------------------------------------------

void test_observe_at_max_skips_persist_for_high_r(void) {
  // Drive base to MAX first (saturated). Any further observe with R >= 2
  // computes target >= MAX, gets clamped back to MAX, finds new_base ==
  // current_base, and must short-circuit the persist write. (R == 0 / R == 1
  // are a legitimate downward probe, so they are NOT short-circuited.)
  for (int i = 0; i < 50; i++) c4_retry_delay_observe(TEST_CATEGORY, TEST_CHAIN, 8);
  TEST_ASSERT_EQUAL_UINT32(C4_RETRY_DELAY_MAX_MS, c4_retry_delay_for(TEST_CATEGORY, TEST_CHAIN, 0));

  int before = g_mock_set_calls;
  c4_retry_delay_observe(TEST_CATEGORY, TEST_CHAIN, 8);
  TEST_ASSERT_EQUAL_INT_MESSAGE(before, g_mock_set_calls,
                                "observe(R >= 2) at MAX must be a no-op (no redundant write)");
}

// --- reset semantics ---------------------------------------------------------

void test_reset_does_not_touch_persisted_value(void) {
  c4_retry_delay_observe(TEST_CATEGORY, TEST_CHAIN, 3); // 1000 -> 4000
  TEST_ASSERT_NOT_NULL(mock_find(EXPECTED_KEY));

  // Reset only drops the in-memory cache; it must NOT delete the persisted
  // blob (otherwise restart semantics would silently forget every learner).
  c4_retry_delay_reset();
  TEST_ASSERT_NOT_NULL_MESSAGE(mock_find(EXPECTED_KEY),
                               "c4_retry_delay_reset() must not affect persisted state");
  TEST_ASSERT_EQUAL_UINT32(4000, c4_retry_delay_for(TEST_CATEGORY, TEST_CHAIN, 0));
}

// --- contract test for the prover-side gating in eth_req.c -------------------

void test_observe_R0_is_a_meaningful_update(void) {
  // Pins the precondition that motivates the explicit `retry_count > 0` gate
  // in `eth_req.c::c4_send_eth_rpc` for `eth_getProof`: feeding R == 0 into
  // the learner IS a non-trivial event (downward probe + persist write). If a
  // refactor ever makes R == 0 a no-op, this test fails and the reviewer is
  // reminded to also revisit the gating logic that prevents non-oblivious
  // getProof successes from polluting the oblivious learner.
  c4_retry_delay_observe(TEST_CATEGORY, TEST_CHAIN, 0);
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_mock_set_calls,
                                "observe(R=0) must trigger persistence -- prover code must gate it explicitly");
  uint32_t v = c4_retry_delay_for(TEST_CATEGORY, TEST_CHAIN, 0);
  TEST_ASSERT_TRUE_MESSAGE(v < C4_RETRY_DELAY_DEFAULT_MS,
                           "observe(R=0) must probe downward");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_default_when_storage_empty);
  RUN_TEST(test_observe_R1_probes_down_slowly);
  RUN_TEST(test_observe_R2_jumps_up_fast);
  RUN_TEST(test_observe_R0_probes_down);
  RUN_TEST(test_repeated_high_R_saturates_at_max);
  RUN_TEST(test_repeated_R0_floors_at_min);
  RUN_TEST(test_persist_and_reload);
  RUN_TEST(test_corrupt_blob_falls_back_to_default);
  RUN_TEST(test_persisted_value_is_clamped_on_read);
  RUN_TEST(test_categories_learn_independently);
  RUN_TEST(test_chains_learn_independently);
  RUN_TEST(test_null_category_is_safe);
  RUN_TEST(test_observe_with_only_get_plugin_does_not_crash);
  RUN_TEST(test_null_get_plugin_is_overridden_by_default_storage);
  RUN_TEST(test_cache_eviction_round_trips_via_storage);
  RUN_TEST(test_observe_at_max_skips_persist_for_high_r);
  RUN_TEST(test_reset_does_not_touch_persisted_value);
  RUN_TEST(test_observe_R0_is_a_meaningful_update);
  return UNITY_END();
}
