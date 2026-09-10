/*
 * Copyright (c) 2025 corpus.core
 *
 * SPDX-License-Identifier: MIT
 */

#include "historic_proof.h"
#include "unity.h"
#include <stdarg.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static syncdata_state_t sync_with_periods(uint64_t block_period, int n, ...) {
  syncdata_state_t s = {0};
  s.block_period     = block_period;
  s.required_period  = block_period;
  va_list ap;
  va_start(ap, n);
  uint64_t oldest = 0;
  uint64_t newest = 0;
  for (int i = 0; i < n && i < MAX_SYNC_PERIODS; i++) {
    uint32_t p     = va_arg(ap, uint32_t);
    s.periods[i]   = p;
    if (!oldest || p < oldest) oldest = p;
    if (!newest || p > newest) newest = p;
  }
  va_end(ap);
  s.oldest_period    = oldest;
  s.newest_period    = newest;
  s.post_sync_period = newest;
  return s;
}

static syncdata_state_t sync_empty(uint64_t post_sync, uint64_t block_period) {
  syncdata_state_t s = {0};
  s.post_sync_period = post_sync;
  s.block_period     = block_period;
  s.required_period  = block_period;
  return s;
}

// --- has / anchor ---

void test_has_period_from_list(void) {
  syncdata_state_t s = sync_with_periods(1852, 2, 1852u, 1853u);
  TEST_ASSERT_TRUE(c4_sync_has_period(&s, 1852));
  TEST_ASSERT_TRUE(c4_sync_has_period(&s, 1853));
  TEST_ASSERT_FALSE(c4_sync_has_period(&s, 1851));
  TEST_ASSERT_FALSE(c4_sync_has_period(&s, 1854));
}

void test_has_period_gap_is_not_membership(void) {
  syncdata_state_t s = sync_with_periods(1852, 2, 1850u, 1853u);
  TEST_ASSERT_TRUE(c4_sync_has_period(&s, 1850));
  TEST_ASSERT_TRUE(c4_sync_has_period(&s, 1853));
  TEST_ASSERT_FALSE(c4_sync_has_period(&s, 1852));
}

void test_has_period_falls_back_to_oldest_newest(void) {
  syncdata_state_t s = {0};
  s.oldest_period    = 1852;
  s.newest_period    = 1853;
  s.block_period     = 1852;
  TEST_ASSERT_TRUE(c4_sync_has_period(&s, 1852));
  TEST_ASSERT_TRUE(c4_sync_has_period(&s, 1853));
  TEST_ASSERT_FALSE(c4_sync_has_period(&s, 1851));
}

void test_has_period_null_or_zero(void) {
  syncdata_state_t s = sync_with_periods(1852, 1, 1852u);
  TEST_ASSERT_FALSE(c4_sync_has_period(NULL, 1852));
  TEST_ASSERT_FALSE(c4_sync_has_period(&s, 0));
}

void test_best_anchor_in_gap(void) {
  syncdata_state_t s = sync_with_periods(1852, 2, 1850u, 1853u);
  TEST_ASSERT_EQUAL_UINT64(1850, c4_sync_best_anchor(&s, 1852));
}

void test_best_anchor_none_below_oldest(void) {
  syncdata_state_t s = sync_with_periods(1849, 2, 1850u, 1853u);
  TEST_ASSERT_EQUAL_UINT64(0, c4_sync_best_anchor(&s, 1849));
}

void test_best_anchor_equals_block_period(void) {
  syncdata_state_t s = sync_with_periods(1852, 2, 1852u, 1853u);
  TEST_ASSERT_EQUAL_UINT64(1852, c4_sync_best_anchor(&s, 1852));
}

void test_best_anchor_null_or_zero(void) {
  syncdata_state_t s = sync_with_periods(1852, 1, 1852u);
  TEST_ASSERT_EQUAL_UINT64(0, c4_sync_best_anchor(NULL, 1852));
  TEST_ASSERT_EQUAL_UINT64(0, c4_sync_best_anchor(&s, 0));
}

void test_best_anchor_picks_highest_at_or_below(void) {
  syncdata_state_t s = sync_with_periods(1852, 4, 1853u, 1848u, 1851u, 1850u);
  TEST_ASSERT_EQUAL_UINT64(1851, c4_sync_best_anchor(&s, 1852));
}

void test_best_anchor_falls_back_to_oldest(void) {
  syncdata_state_t s = {0};
  s.oldest_period    = 1850;
  s.newest_period    = 1853;
  TEST_ASSERT_EQUAL_UINT64(1850, c4_sync_best_anchor(&s, 1852));
  TEST_ASSERT_EQUAL_UINT64(0, c4_sync_best_anchor(&s, 1849));
}

// --- needs historic ---

void test_client_holding_block_period_skips_historic(void) {
  syncdata_state_t s = sync_with_periods(1852, 2, 1852u, 1853u);
  TEST_ASSERT_FALSE(c4_needs_historic_direct(&s, 1853, true));
}

void test_client_only_has_newer_period_needs_historic(void) {
  syncdata_state_t s = sync_with_periods(1852, 1, 1853u);
  TEST_ASSERT_TRUE(c4_needs_historic_direct(&s, 1853, true));
}

void test_block_in_newest_period_skips_historic(void) {
  syncdata_state_t s = sync_with_periods(1853, 2, 1852u, 1853u);
  TEST_ASSERT_FALSE(c4_needs_historic_direct(&s, 1853, true));
}

void test_block_older_than_oldest_needs_historic(void) {
  syncdata_state_t s = sync_with_periods(1850, 2, 1852u, 1853u);
  TEST_ASSERT_TRUE(c4_needs_historic_direct(&s, 1853, true));
}

void test_gap_with_marker_needs_historic(void) {
  syncdata_state_t s = sync_with_periods(1852, 2, 1850u, 1853u);
  TEST_ASSERT_TRUE(c4_needs_historic_direct(&s, 1854, true));
}

void test_gap_without_marker_skips_historic(void) {
  syncdata_state_t s = sync_with_periods(1852, 2, 1850u, 1853u);
  TEST_ASSERT_FALSE(c4_needs_historic_direct(&s, 1854, false));
}

void test_empty_client_post_sync_older_block_needs_historic(void) {
  syncdata_state_t s = sync_empty(1853, 1852);
  TEST_ASSERT_TRUE(c4_needs_historic_direct(&s, 0, true));
}

void test_empty_client_post_sync_same_period_skips_historic(void) {
  syncdata_state_t s = sync_empty(1852, 1852);
  TEST_ASSERT_FALSE(c4_needs_historic_direct(&s, 0, true));
}

void test_empty_client_no_periods_skips_historic(void) {
  syncdata_state_t s = sync_empty(0, 1852);
  TEST_ASSERT_FALSE(c4_needs_historic_direct(&s, 0, true));
}

void test_null_sync_skips_historic(void) {
  TEST_ASSERT_FALSE(c4_needs_historic_direct(NULL, 1853, true));
}

void test_required_period_used_when_block_period_unset(void) {
  syncdata_state_t s = sync_with_periods(0, 1, 1853u);
  s.block_period     = 0;
  s.required_period  = 1852;
  TEST_ASSERT_TRUE(c4_needs_historic_direct(&s, 1853, true));
}

void test_block_period_wins_over_newer_required(void) {
  syncdata_state_t s = sync_empty(1853, 1852);
  s.required_period  = 1853;
  TEST_ASSERT_TRUE(c4_needs_historic_direct(&s, 0, true));
}

void test_block_period_wins_over_older_required(void) {
  syncdata_state_t s = sync_with_periods(1852, 2, 1852u, 1853u);
  s.required_period  = 1851;
  TEST_ASSERT_FALSE(c4_needs_historic_direct(&s, 1853, true));
}

void test_checkpoint_only_older_block_needs_historic(void) {
  syncdata_state_t s  = sync_with_periods(1852, 1, 1853u);
  s.checkpoint_period = 1853;
  TEST_ASSERT_TRUE(c4_needs_historic_direct(&s, 1853, true));
}

void test_checkpoint_period_alone_is_ignored(void) {
  syncdata_state_t s  = {0};
  s.checkpoint_period = 1853;
  s.block_period      = 1852;
  s.required_period   = 1852;
  TEST_ASSERT_FALSE(c4_needs_historic_direct(&s, 0, true));
}

void test_marker_false_never_historic(void) {
  syncdata_state_t s = sync_with_periods(1850, 1, 1853u);
  TEST_ASSERT_FALSE(c4_needs_historic_direct(&s, 1853, false));
}

void test_current_period_not_historic_even_with_marker(void) {
  syncdata_state_t s = sync_with_periods(1854, 1, 1853u);
  TEST_ASSERT_FALSE(c4_needs_historic_direct(&s, 1854, true));
}

void test_needs_historic_block_after_head(void) {
  syncdata_state_t s = sync_with_periods(1855, 1, 1853u);
  TEST_ASSERT_FALSE(c4_needs_historic_direct(&s, 1854, true));
}

// --- plan ---

void test_plan_client_has_block_is_sync_aggregate(void) {
  syncdata_state_t s = sync_with_periods(1852, 2, 1852u, 1853u);
  uint64_t         required = 0, start = 99;
  TEST_ASSERT_EQUAL_INT(C4_HISTORIC_PLAN_SYNC_AGGREGATE, c4_plan_blockroot_proof(&s, 1853, true, &required, &start));
  TEST_ASSERT_EQUAL_UINT64(1852, required);
  TEST_ASSERT_EQUAL_UINT64(0, start);
}

void test_plan_gap_with_marker_is_historic_to_head(void) {
  syncdata_state_t s = sync_with_periods(1852, 2, 1850u, 1853u);
  uint64_t         required = 0, start = 99;
  TEST_ASSERT_EQUAL_INT(C4_HISTORIC_PLAN_HISTORIC, c4_plan_blockroot_proof(&s, 1854, true, &required, &start));
  TEST_ASSERT_EQUAL_UINT64(1854, required);
  TEST_ASSERT_EQUAL_UINT64(0, start);
}

void test_plan_gap_without_marker_is_lcu_from_anchor(void) {
  syncdata_state_t s = sync_with_periods(1852, 2, 1850u, 1853u);
  uint64_t         required = 0, start = 0;
  TEST_ASSERT_EQUAL_INT(C4_HISTORIC_PLAN_LCU_GAP, c4_plan_blockroot_proof(&s, 1854, false, &required, &start));
  TEST_ASSERT_EQUAL_UINT64(1852, required);
  TEST_ASSERT_EQUAL_UINT64(1850, start);
}

void test_plan_current_period_is_lcu_forward(void) {
  syncdata_state_t s = sync_with_periods(1854, 1, 1853u);
  uint64_t         required = 0, start = 99;
  TEST_ASSERT_EQUAL_INT(C4_HISTORIC_PLAN_LCU_FORWARD, c4_plan_blockroot_proof(&s, 1854, true, &required, &start));
  TEST_ASSERT_EQUAL_UINT64(1854, required);
  TEST_ASSERT_EQUAL_UINT64(0, start);
}

void test_plan_older_than_oldest_without_marker_is_error(void) {
  syncdata_state_t s = sync_with_periods(1849, 2, 1850u, 1853u);
  uint64_t         required = 0, start = 99;
  TEST_ASSERT_EQUAL_INT(C4_HISTORIC_PLAN_ERROR, c4_plan_blockroot_proof(&s, 1854, false, &required, &start));
  TEST_ASSERT_EQUAL_UINT64(1849, required);
  TEST_ASSERT_EQUAL_UINT64(0, start);
}

void test_plan_block_after_head_is_lcu_forward(void) {
  syncdata_state_t s = sync_with_periods(1855, 1, 1853u);
  uint64_t         required = 0, start = 99;
  TEST_ASSERT_EQUAL_INT(C4_HISTORIC_PLAN_LCU_FORWARD, c4_plan_blockroot_proof(&s, 1854, true, &required, &start));
  TEST_ASSERT_EQUAL_UINT64(1855, required);
  TEST_ASSERT_EQUAL_UINT64(0, start);
}

void test_plan_behind_without_marker_is_lcu_from_oldest(void) {
  syncdata_state_t s = sync_with_periods(1852, 1, 1850u);
  uint64_t         required = 0, start = 0;
  TEST_ASSERT_EQUAL_INT(C4_HISTORIC_PLAN_LCU_GAP, c4_plan_blockroot_proof(&s, 1854, false, &required, &start));
  TEST_ASSERT_EQUAL_UINT64(1852, required);
  TEST_ASSERT_EQUAL_UINT64(1850, start);
}

void test_plan_behind_with_marker_is_historic_to_head(void) {
  syncdata_state_t s = sync_with_periods(1852, 1, 1850u);
  uint64_t         required = 0, start = 99;
  TEST_ASSERT_EQUAL_INT(C4_HISTORIC_PLAN_HISTORIC, c4_plan_blockroot_proof(&s, 1854, true, &required, &start));
  TEST_ASSERT_EQUAL_UINT64(1854, required);
  TEST_ASSERT_EQUAL_UINT64(0, start);
}

void test_plan_null_sync_is_sync_aggregate(void) {
  uint64_t required = 99, start = 99;
  TEST_ASSERT_EQUAL_INT(C4_HISTORIC_PLAN_SYNC_AGGREGATE, c4_plan_blockroot_proof(NULL, 1853, true, &required, &start));
  TEST_ASSERT_EQUAL_UINT64(0, required);
  TEST_ASSERT_EQUAL_UINT64(0, start);
}

void test_plan_head_unknown_older_with_marker_uses_oldest(void) {
  syncdata_state_t s = sync_with_periods(1850, 2, 1852u, 1853u);
  uint64_t         required = 0, start = 99;
  TEST_ASSERT_EQUAL_INT(C4_HISTORIC_PLAN_HISTORIC, c4_plan_blockroot_proof(&s, 0, true, &required, &start));
  TEST_ASSERT_EQUAL_UINT64(1852, required);
  TEST_ASSERT_EQUAL_UINT64(0, start);
}

void test_plan_older_than_oldest_with_marker_is_historic(void) {
  syncdata_state_t s = sync_with_periods(1849, 2, 1850u, 1853u);
  uint64_t         required = 0, start = 99;
  TEST_ASSERT_EQUAL_INT(C4_HISTORIC_PLAN_HISTORIC, c4_plan_blockroot_proof(&s, 1854, true, &required, &start));
  TEST_ASSERT_EQUAL_UINT64(1854, required);
}

void test_plan_empty_older_without_marker_is_error(void) {
  syncdata_state_t s = sync_empty(1853, 1852);
  TEST_ASSERT_EQUAL_INT(C4_HISTORIC_PLAN_ERROR, c4_plan_blockroot_proof(&s, 1853, false, NULL, NULL));
}

void test_plan_same_head_and_newest_historic_no_extra_lcu(void) {
  syncdata_state_t s = sync_with_periods(1852, 1, 1853u);
  uint64_t         required = 0, start = 99;
  TEST_ASSERT_EQUAL_INT(C4_HISTORIC_PLAN_HISTORIC, c4_plan_blockroot_proof(&s, 1853, true, &required, &start));
  TEST_ASSERT_EQUAL_UINT64(1853, required);
  TEST_ASSERT_EQUAL_UINT64(0, start);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_has_period_from_list);
  RUN_TEST(test_has_period_gap_is_not_membership);
  RUN_TEST(test_has_period_falls_back_to_oldest_newest);
  RUN_TEST(test_has_period_null_or_zero);
  RUN_TEST(test_best_anchor_in_gap);
  RUN_TEST(test_best_anchor_none_below_oldest);
  RUN_TEST(test_best_anchor_equals_block_period);
  RUN_TEST(test_best_anchor_null_or_zero);
  RUN_TEST(test_best_anchor_picks_highest_at_or_below);
  RUN_TEST(test_best_anchor_falls_back_to_oldest);
  RUN_TEST(test_client_holding_block_period_skips_historic);
  RUN_TEST(test_client_only_has_newer_period_needs_historic);
  RUN_TEST(test_block_in_newest_period_skips_historic);
  RUN_TEST(test_block_older_than_oldest_needs_historic);
  RUN_TEST(test_gap_with_marker_needs_historic);
  RUN_TEST(test_gap_without_marker_skips_historic);
  RUN_TEST(test_empty_client_post_sync_older_block_needs_historic);
  RUN_TEST(test_empty_client_post_sync_same_period_skips_historic);
  RUN_TEST(test_empty_client_no_periods_skips_historic);
  RUN_TEST(test_null_sync_skips_historic);
  RUN_TEST(test_required_period_used_when_block_period_unset);
  RUN_TEST(test_block_period_wins_over_newer_required);
  RUN_TEST(test_block_period_wins_over_older_required);
  RUN_TEST(test_checkpoint_only_older_block_needs_historic);
  RUN_TEST(test_checkpoint_period_alone_is_ignored);
  RUN_TEST(test_marker_false_never_historic);
  RUN_TEST(test_current_period_not_historic_even_with_marker);
  RUN_TEST(test_needs_historic_block_after_head);
  RUN_TEST(test_plan_client_has_block_is_sync_aggregate);
  RUN_TEST(test_plan_gap_with_marker_is_historic_to_head);
  RUN_TEST(test_plan_gap_without_marker_is_lcu_from_anchor);
  RUN_TEST(test_plan_current_period_is_lcu_forward);
  RUN_TEST(test_plan_older_than_oldest_without_marker_is_error);
  RUN_TEST(test_plan_older_than_oldest_with_marker_is_historic);
  RUN_TEST(test_plan_empty_older_without_marker_is_error);
  RUN_TEST(test_plan_same_head_and_newest_historic_no_extra_lcu);
  RUN_TEST(test_plan_block_after_head_is_lcu_forward);
  RUN_TEST(test_plan_behind_without_marker_is_lcu_from_oldest);
  RUN_TEST(test_plan_behind_with_marker_is_historic_to_head);
  RUN_TEST(test_plan_null_sync_is_sync_aggregate);
  RUN_TEST(test_plan_head_unknown_older_with_marker_uses_oldest);
  return UNITY_END();
}
