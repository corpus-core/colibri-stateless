/*
 * Copyright (c) 2025,2026 corpus.core GmbH
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

// The verifier's wall clock (`c4_set_fixed_time`, `c4_now_unix_s`) and the PAP pending list,
// whose entries carry the clock's time and expire after `PAP_PENDING_TX_TTL_S`.

#include "../../src/util/bytes.h"
#include "../../src/util/plugin.h"
#include "unity.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef PAP
#include "pap_tx_cache.h"
#endif

// One-entry storage, enough for one chain's pending list.
static char    g_key[64];
static bytes_t g_value;

static bool mem_get(char* key, buffer_t* buffer) {
  if (!g_value.data || strcmp(key, g_key) != 0) return false;
  buffer_append(buffer, g_value);
  return true;
}

static void mem_set(char* key, bytes_t value) {
  strncpy(g_key, key, sizeof(g_key) - 1);
  free(g_value.data);
  g_value = value.len ? bytes_dup(value) : NULL_BYTES;
}

static void mem_del(char* key) {
  if (strcmp(key, g_key) != 0) return;
  free(g_value.data);
  g_value = NULL_BYTES;
}

void setUp(void) {
  storage_plugin_t plugin = {.get = mem_get, .set = mem_set, .del = mem_del};
  c4_set_storage_config(&plugin);
  g_key[0] = 0;
  free(g_value.data);
  g_value = NULL_BYTES;
}

void tearDown(void) {
  c4_set_fixed_time(0);
}

void test_fixed_time(void) {
  c4_set_fixed_time(1800000000);
  TEST_ASSERT_EQUAL_UINT64(1800000000, c4_now_unix_s());
}

void test_system_clock_by_default(void) {
  c4_set_fixed_time(0);
  uint64_t before = (uint64_t) time(NULL);
  uint64_t now    = c4_now_unix_s();
  uint64_t after  = (uint64_t) time(NULL);
  TEST_ASSERT_TRUE(now >= before && now <= after);
}

// The repository's root, from this file's path (`<root>/test/unittests/...`).
static size_t root_length(const char* here) {
  for (const char* p = strstr(here, "unittests"); p; p = strstr(p + 1, "unittests")) {
    size_t at = (size_t) (p - here);
    if (at >= 5 && (here[at - 1] == '/' || here[at - 1] == '\\') && strncmp(here + at - 5, "test", 4) == 0)
      return at - 5;
  }
  return 0;
}

// `rel` reads no system clock directly: a `time(NULL)` there would escape
// `c4_set_fixed_time`.
static void assert_no_system_clock(const char* rel) {
  const char* here = __FILE__;
  size_t      root = root_length(here);
  TEST_ASSERT_TRUE_MESSAGE(root > 0, "the repository root from __FILE__");
  char path[2048];
  snprintf(path, sizeof(path), "%.*s%s", (int) root, here, rel);
  FILE* f = fopen(path, "rb");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  char* text = calloc((size_t) len + 1, 1);
  TEST_ASSERT_NOT_NULL(text);
  TEST_ASSERT_EQUAL_UINT64((uint64_t) len, (uint64_t) fread(text, 1, (size_t) len, f));
  fclose(f);
  bool direct = strstr(text, "time(NULL)") || strstr(text, "time(0)");
  free(text);
  TEST_ASSERT_FALSE_MESSAGE(direct, rel);
}

// Every clock read of the PAP cache and its lookup goes through `c4_now_unix_s`,
// including the cache's age (`last_updated`, and the lookup's staleness check),
// which the tests below don't reach.
void test_the_pap_cache_reads_the_verifier_clock(void) {
  assert_no_system_clock("src/chains/eth/verifier/pap_tx_cache.c");
  assert_no_system_clock("src/chains/eth/verifier/verify_pap_tx.c");
}

#ifdef PAP
// The timestamp of the stored list's only entry: 32 bytes tx_hash, then 8 bytes timestamp (LE).
static uint64_t stored_timestamp(void) {
  uint64_t ts = 0;
  for (int i = 7; i >= 0; i--) ts = (ts << 8) | g_value.data[32 + i];
  return ts;
}

void test_pending_entry_carries_the_fixed_time(void) {
  bytes32_t hash;
  memset(hash, 0xab, 32);
  c4_set_fixed_time(1800000000);
  pap_tx_cache_add_pending(1, hash);
  TEST_ASSERT_EQUAL_STRING("tx_pending_1", g_key);
  TEST_ASSERT_EQUAL_UINT32(40, g_value.len);
  TEST_ASSERT_EQUAL_UINT64(1800000000, stored_timestamp());
}

void test_adding_again_refreshes_the_entry(void) {
  bytes32_t hash;
  memset(hash, 0xef, 32);
  c4_set_fixed_time(1800000000);
  pap_tx_cache_add_pending(1, hash);
  c4_set_fixed_time(1800000000 + 100);
  pap_tx_cache_add_pending(1, hash);
  TEST_ASSERT_EQUAL_UINT32(40, g_value.len);
  TEST_ASSERT_EQUAL_UINT64(1800000000 + 100, stored_timestamp());
}

void test_removing_prunes_entries_expired_at_the_fixed_time(void) {
  bytes32_t old_hash, other;
  memset(old_hash, 0x11, 32);
  memset(other, 0x22, 32);
  c4_set_fixed_time(1800000000);
  pap_tx_cache_add_pending(1, old_hash);
  c4_set_fixed_time(1800000000 + 10);
  pap_tx_cache_add_pending(1, other);
  TEST_ASSERT_EQUAL_UINT32(80, g_value.len);

  // At this time the first entry is past its TTL, the second isn't: removing
  // the second leaves nothing.
  c4_set_fixed_time(1800000000 + PAP_PENDING_TX_TTL_S + 1);
  pap_tx_cache_remove_pending(1, other);
  TEST_ASSERT_EQUAL_UINT32(0, g_value.len);
}

void test_pending_entry_expires_after_its_ttl(void) {
  bytes32_t hash;
  memset(hash, 0xcd, 32);
  c4_set_fixed_time(1800000000);
  pap_tx_cache_add_pending(1, hash);

  c4_set_fixed_time(1800000000 + PAP_PENDING_TX_TTL_S);
  TEST_ASSERT_TRUE(pap_tx_cache_is_pending(1, hash));

  c4_set_fixed_time(1800000000 + PAP_PENDING_TX_TTL_S + 1);
  TEST_ASSERT_FALSE(pap_tx_cache_is_pending(1, hash));
}
#endif

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_fixed_time);
  RUN_TEST(test_system_clock_by_default);
  RUN_TEST(test_the_pap_cache_reads_the_verifier_clock);
#ifdef PAP
  RUN_TEST(test_pending_entry_carries_the_fixed_time);
  RUN_TEST(test_pending_entry_expires_after_its_ttl);
  RUN_TEST(test_adding_again_refreshes_the_entry);
  RUN_TEST(test_removing_prunes_entries_expired_at_the_fixed_time);
#endif
  return UNITY_END();
}
