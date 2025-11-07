/*
 * Transaction cache unit tests
 */

#include "crypto.h"
#include "unity.h"
#include <stdint.h>
#include <string.h>

// Use relative include path that should be visible via include dirs; fall back to explicit path if needed.
#include "chains/eth/prover/tx_cache.h"

static void make_hash(bytes32_t out, uint64_t x, uint64_t salt) {
  memset(out, 0, BYTES32_SIZE);
  memcpy(out + 0, &x, sizeof(uint64_t));
  memcpy(out + 8, &salt, sizeof(uint64_t));
}

void setUp(void) {
#ifdef PROVER_CACHE
  c4_eth_tx_cache_reset();
#endif
}

void tearDown(void) {
#ifdef PROVER_CACHE
  c4_eth_tx_cache_reset();
#endif
}

#ifdef PROVER_CACHE

void test_tx_cache_set_get_basic(void) {
  // Insert 200 txs in one block and verify retrieval
  uint64_t block_number = 1000;
  for (uint32_t i = 0; i < 200; i++) {
    bytes32_t h = {0};
    make_hash(h, i, 0xA5A5);
    c4_eth_tx_cache_set(h, block_number, i);
  }
  TEST_ASSERT_TRUE(c4_eth_tx_cache_size() >= 200);

  // Spot check a few
  for (uint32_t i = 0; i < 200; i += 37) {
    bytes32_t h   = {0};
    uint64_t  bn  = 0;
    uint32_t  txi = 0xFFFFFFFFu;
    make_hash(h, i, 0xA5A5);
    TEST_ASSERT_TRUE(c4_eth_tx_cache_get(h, &bn, &txi));
    TEST_ASSERT_EQUAL_UINT64(block_number, bn);
    TEST_ASSERT_EQUAL_UINT32(i, txi);
  }
}

void test_tx_cache_update_in_place(void) {
  bytes32_t h = {0};
  make_hash(h, 42, 0);
  c4_eth_tx_cache_set(h, 1234, 7);
  size_t before = c4_eth_tx_cache_size();
  // Update same tx with new location
  c4_eth_tx_cache_set(h, 1235, 8);
  size_t after = c4_eth_tx_cache_size();
  TEST_ASSERT_EQUAL_UINT32(before, after);
  uint64_t bn  = 0;
  uint32_t idx = 0;
  TEST_ASSERT_TRUE(c4_eth_tx_cache_get(h, &bn, &idx));
  TEST_ASSERT_EQUAL_UINT64(1235, bn);
  TEST_ASSERT_EQUAL_UINT32(8, idx);
}

void test_tx_cache_eviction_fifo(void) {
  // Fill more than MAX (10000) entries in 200-sized blocks and verify FIFO eviction
  const uint32_t per_block        = 200;
  const uint32_t blocks_to_insert = 60; // 12000 entries total
  uint32_t       inserted         = 0;
  for (uint32_t b = 0; b < blocks_to_insert; b++) {
    uint64_t block_number = 10000 + b;
    for (uint32_t i = 0; i < per_block; i++) {
      bytes32_t h = {0};
      make_hash(h, (uint64_t) inserted, 0x55);
      c4_eth_tx_cache_set(h, block_number, i);
      inserted++;
    }
  }
  TEST_ASSERT_TRUE(c4_eth_tx_cache_size() <= 10000);

  // The earliest blocks should be evicted; check an early key is gone
  {
    bytes32_t old_key = {0};
    make_hash(old_key, 10, 0x55);
    uint64_t bn;
    uint32_t idx;
    TEST_ASSERT_FALSE(c4_eth_tx_cache_get(old_key, &bn, &idx));
  }
  // A recent key should be present
  {
    bytes32_t new_key = {0};
    make_hash(new_key, (uint64_t) (inserted - 5), 0x55);
    uint64_t bn  = 0;
    uint32_t idx = 0;
    TEST_ASSERT_TRUE(c4_eth_tx_cache_get(new_key, &bn, &idx));
    TEST_ASSERT_TRUE(idx < per_block);
  }
}

#else

void test_tx_cache_skipped(void) {
  TEST_IGNORE_MESSAGE("PROVER_CACHE not enabled; transaction cache tests skipped");
}

#endif

int main(void) {
  UNITY_BEGIN();
#ifdef PROVER_CACHE
  RUN_TEST(test_tx_cache_set_get_basic);
  RUN_TEST(test_tx_cache_update_in_place);
  RUN_TEST(test_tx_cache_eviction_fifo);
#else
  RUN_TEST(test_tx_cache_skipped);
#endif
  return UNITY_END();
}
