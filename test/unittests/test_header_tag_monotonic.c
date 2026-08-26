/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: MIT
 *
 * Tests for the monotonic tag-write rule in the hybrid header tag cache
 * (`src/chains/eth/prover/beacon_header.c`). The rule keeps `latest`/`safe`/
 * `finalized` from being rolled back to an older block by a slower in-flight
 * `latest` fetch from a parallel request, while still allowing a same-height
 * reorg (equal number, different hash) and a legitimate refetch after TTL
 * expiry (deeper reorg with a lower head).
 *
 * The tests hit the decision function directly through a small test-only
 * introspection API declared in `prover/beacon.h` under `#ifdef TEST`. No
 * network, no fixture data.
 */

#include "chains/eth/prover/beacon.h"
#include "unity.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Local mirror of the private enum in `beacon_header.c`. Kept in sync manually;
// the test-only API deliberately accepts a raw `uint32_t` so tests do not need
// to import the private header.
#define TAG_LATEST    0u
#define TAG_SAFE      1u
#define TAG_FINALIZED 2u

// Sample hashes with distinct byte patterns so mix-ups show up in the diff.
static const uint8_t HASH_A[32] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
                                   0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
                                   0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
                                   0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
static const uint8_t HASH_B[32] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
                                   0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
                                   0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
                                   0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};

void setUp(void) {
  c4_prover_header_tags_clear();
}

void tearDown(void) {
  c4_prover_header_tags_clear();
}

// Baseline: empty tag → any write is accepted.
void test_empty_tag_accepts_write(void) {
  TEST_ASSERT_TRUE(c4_prover_header_tags_test_apply_write(TAG_LATEST, /*new_number=*/100, /*now_ms=*/1000, /*ttl_ms=*/6000));
}

// Rule 1: forward progress on the chain always writes.
void test_higher_number_within_ttl_writes(void) {
  c4_prover_header_tags_test_set(TAG_LATEST, HASH_A, /*block_number=*/100, /*cached_at_ms=*/1000);
  // Only 500 ms elapsed, TTL is 6000 ms → old entry is still fresh. Higher number wins.
  TEST_ASSERT_TRUE(c4_prover_header_tags_test_apply_write(TAG_LATEST, 101, 1500, 6000));
}

// Rule 2: same-height reorg (equal number, different hash) must land while the tag is fresh.
// The rule itself only takes the number as input; the caller decides based on the returned
// bool whether to overwrite the hash. The intent is documented by the write regardless.
void test_equal_number_within_ttl_writes(void) {
  c4_prover_header_tags_test_set(TAG_LATEST, HASH_A, 100, 1000);
  TEST_ASSERT_TRUE(c4_prover_header_tags_test_apply_write(TAG_LATEST, 100, 1500, 6000));
}

// Rule 3: the core protection. A slower `latest` from a `Promise.all` race resolves to an
// older block after the fresher entry has already been written. Rolling back would hide
// the newer head from every subsequent call for another TTL window.
void test_lower_number_within_ttl_is_rejected(void) {
  c4_prover_header_tags_test_set(TAG_LATEST, HASH_B, /*block_number=*/101, /*cached_at_ms=*/1000);
  // 500 ms later a slower fetch for the same tag resolves to block 100. Still within TTL.
  TEST_ASSERT_FALSE(c4_prover_header_tags_test_apply_write(TAG_LATEST, 100, 1500, 6000));
}

// Rule 4: after TTL expiry any number wins, so a deeper reorg with a lower head can land.
// Never doing this would freeze the tag on an orphaned higher block forever.
void test_lower_number_after_ttl_expiry_writes(void) {
  c4_prover_header_tags_test_set(TAG_LATEST, HASH_B, 101, 1000);
  // 6001 ms elapsed, TTL is 6000 ms → old entry is stale. Even a lower number now wins.
  TEST_ASSERT_TRUE(c4_prover_header_tags_test_apply_write(TAG_LATEST, 100, 7001, 6000));
}

// Boundary: `now - cached_at_ms == ttl_ms` is treated as *expired* (strict less-than in the
// rule). A tag written exactly at TTL boundary is not counted as fresh anymore.
void test_lower_number_at_exact_ttl_boundary_writes(void) {
  c4_prover_header_tags_test_set(TAG_LATEST, HASH_B, 101, 1000);
  TEST_ASSERT_TRUE(c4_prover_header_tags_test_apply_write(TAG_LATEST, 100, 7000, 6000));
}

// A stored `cached_at_ms == 0` counts as no-existing-entry, even if a stale hash is left in
// place: the persistence code drops sentinels but keeps hashes, and a fresh process must not
// treat those as authoritative.
void test_zero_timestamp_counts_as_no_entry(void) {
  c4_prover_header_tags_test_set(TAG_LATEST, HASH_A, 200, /*cached_at_ms=*/0);
  TEST_ASSERT_TRUE(c4_prover_header_tags_test_apply_write(TAG_LATEST, 100, 5000, 6000));
}

// The other half of the "have_existing" compound guard: fresh timestamp but a zero-hash slot
// also counts as "no existing entry". Prevents a null hash from freezing the tag when the
// number happens to look higher than the incoming one.
void test_zero_hash_counts_as_no_entry(void) {
  c4_prover_header_tags_test_set(TAG_LATEST, /*hash=*/NULL, 200, /*cached_at_ms=*/1000);
  TEST_ASSERT_TRUE(c4_prover_header_tags_test_apply_write(TAG_LATEST, 100, 1500, 6000));
}

// Clock skew: `now < cached_at_ms` underflows the unsigned subtraction to a huge value, so
// `within_ttl` becomes false and the write is accepted. Pinning this behavior so a later
// refactor cannot silently switch to signed arithmetic or an explicit skew branch.
void test_backwards_clock_treats_entry_as_expired(void) {
  c4_prover_header_tags_test_set(TAG_LATEST, HASH_B, 200, /*cached_at_ms=*/1000);
  TEST_ASSERT_TRUE(c4_prover_header_tags_test_apply_write(TAG_LATEST, /*new_number=*/100, /*now_ms=*/500, /*ttl_ms=*/6000));
}

// Sanity: the test-only API rejects tag indices >= HEADER_TAG_COUNT. If someone reorders or
// grows the enum in `beacon_header.c`, this assertion at least catches a size mismatch.
void test_out_of_range_tag_is_rejected(void) {
  TEST_ASSERT_FALSE(c4_prover_header_tags_test_apply_write(/*tag=*/3, 100, 1000, 6000));
  // `set`/`get` must be silent no-ops for out-of-range indices — no crash, no side effects.
  c4_prover_header_tags_test_set(/*tag=*/3, HASH_A, 999, 999);
  uint8_t  hash[32]     = {0};
  uint64_t block_number = 999;
  uint64_t cached_at_ms = 999;
  c4_prover_header_tags_test_get(/*tag=*/3, hash, &block_number, &cached_at_ms);
  TEST_ASSERT_EQUAL_UINT64(999, block_number);
  TEST_ASSERT_EQUAL_UINT64(999, cached_at_ms);
}

// The rule is per-tag: writing SAFE must not be affected by LATEST state.
void test_rule_is_per_tag(void) {
  c4_prover_header_tags_test_set(TAG_LATEST, HASH_A, 200, 1000);
  // SAFE is empty → accepts.
  TEST_ASSERT_TRUE(c4_prover_header_tags_test_apply_write(TAG_SAFE, 190, 1500, 6000));
  // LATEST is still frozen against a rollback.
  TEST_ASSERT_FALSE(c4_prover_header_tags_test_apply_write(TAG_LATEST, 199, 1500, 6000));
}

// Sanity check for the introspection API itself: what we set is what we read back.
void test_set_get_roundtrip(void) {
  c4_prover_header_tags_test_set(TAG_LATEST, HASH_A, 123, 456);
  uint8_t  hash[32]     = {0};
  uint64_t block_number = 0;
  uint64_t cached_at_ms = 0;
  c4_prover_header_tags_test_get(TAG_LATEST, hash, &block_number, &cached_at_ms);
  TEST_ASSERT_EQUAL_MEMORY(HASH_A, hash, 32);
  TEST_ASSERT_EQUAL_UINT64(123, block_number);
  TEST_ASSERT_EQUAL_UINT64(456, cached_at_ms);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_tag_accepts_write);
  RUN_TEST(test_higher_number_within_ttl_writes);
  RUN_TEST(test_equal_number_within_ttl_writes);
  RUN_TEST(test_lower_number_within_ttl_is_rejected);
  RUN_TEST(test_lower_number_after_ttl_expiry_writes);
  RUN_TEST(test_lower_number_at_exact_ttl_boundary_writes);
  RUN_TEST(test_zero_timestamp_counts_as_no_entry);
  RUN_TEST(test_zero_hash_counts_as_no_entry);
  RUN_TEST(test_backwards_clock_treats_entry_as_expired);
  RUN_TEST(test_out_of_range_tag_is_rejected);
  RUN_TEST(test_rule_is_per_tag);
  RUN_TEST(test_set_get_roundtrip);
  return UNITY_END();
}
