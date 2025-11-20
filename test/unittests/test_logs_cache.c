/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */
#include "c4_assert.h"

#ifdef PROVER_CACHE

#include "bytes.h"
#include "json.h"
#include "logs_cache.h"
#include "prover.h"
#include "unity.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define TEST_START_BLOCK 23839610
#define TEST_BLOCK_COUNT 6

// Mock context for tests
static prover_ctx_t* g_ctx = NULL;

static bool json_matches_string(json_t value, const char* expected) {
  if (value.type != JSON_TYPE_STRING || !expected) return false;
  char* str = json_as_string(value, NULL);
  if (!str) return false;
  bool equal = strcmp(str, expected) == 0;
  safe_free(str);
  return equal;
}

void setUp(void) {
  // Enable cache with enough capacity
  c4_eth_logs_cache_enable(100);
  g_ctx = c4_prover_create("eth_getLogs", "[]", 1, 0);
}

void tearDown(void) {
  c4_eth_logs_cache_disable();
  if (g_ctx) c4_prover_free(g_ctx);
  g_ctx = NULL;
}

// Helper to compute OR-ed bloom from receipts
static void compute_block_bloom(json_t receipts, uint8_t* out_bloom) {
  memset(out_bloom, 0, 256);
  uint8_t  tmp_bloom[256];
  buffer_t b = stack_buffer(tmp_bloom);

  json_for_each_value(receipts, r) {
    json_t logs_bloom_json = json_get(r, "logsBloom");
    if (logs_bloom_json.type == JSON_TYPE_STRING) {
      buffer_reset(&b);
      bytes_t bloomb = json_as_bytes(logs_bloom_json, &b);
      if (bloomb.len == 256) {
        for (int i = 0; i < 256; i++) {
          out_bloom[i] |= bloomb.data[i];
        }
      }
    }
  }
}

static void load_cache_data(void) {
  char filename[256];
  for (int i = 0; i < TEST_BLOCK_COUNT; i++) {
    uint64_t bn = TEST_START_BLOCK + i;
    snprintf(filename, sizeof(filename), "log_cache/receipts_%lu.json", (unsigned long) bn);
    bytes_t content = read_testdata(filename);
    TEST_ASSERT_NOT_NULL_MESSAGE(content.data, "Failed to read receipt file");

    json_t  receipts = json_get(json_parse((char*) content.data), "result");
    uint8_t bloom[256];
    compute_block_bloom(receipts, bloom);

    c4_eth_logs_cache_add_block(bn, bloom, receipts);
    safe_free(content.data);
  }
}

// Helper to run the scan loop with mock RPC responses
static void run_scan_expect_success(json_t filter, json_t* out_result, bool* out_cached) {
  c4_status_t status;
  int         loop_limit = 100;
  char        tmp[100];
  buffer_t    buf = stack_buffer(tmp);

  if (g_ctx)      c4_prover_free(g_ctx);
  g_ctx = c4_prover_create("eth_getLogs", "[]", 1, 0);
  
  do {
    status = c4_eth_logs_cache_scan(g_ctx, filter, out_result, out_cached);

    if (status == C4_PENDING) {
      data_request_t* req = c4_state_get_pending_request(&g_ctx->state);
      while (req) {
        if (req->type != C4_DATA_TYPE_ETH_RPC || req->payload.len == 0) {
          TEST_FAIL_MESSAGE("Invalid request type or payload");
          return;
        }
        json_t payload = json_parse((char*) req->payload.data);
        char*  method  = json_as_string(json_get(payload, "method"), &buf);
        json_t params  = json_get(payload, "params");

        // Mock eth_getBlockReceipts response
        if (strcmp(method, "eth_getBlockReceipts") == 0) {
          json_t   block_id_json = json_at(params, 0);
          uint64_t bn            = json_as_uint64(block_id_json);

          // Load corresponding file
          char filename[256];
          snprintf(filename, sizeof(filename), "log_cache/receipts_%lu.json", (unsigned long) bn);
          bytes_t content = read_testdata(filename);
          if (content.data) {
            req->response = content; // Transfers ownership
          }
          else {
            TEST_FAIL_MESSAGE("Requested block receipts not found in test data");
          }
        }
        req = req->next;
      }
    }
  } while (status == C4_PENDING && --loop_limit > 0);

  TEST_ASSERT_EQUAL(C4_SUCCESS, status);
}

void test_cache_range_check(void) {
  load_cache_data();
  TEST_ASSERT_TRUE(c4_eth_logs_cache_has_range(TEST_START_BLOCK, TEST_START_BLOCK + TEST_BLOCK_COUNT - 1));
  TEST_ASSERT_TRUE(c4_eth_logs_cache_has_range(TEST_START_BLOCK + 1, TEST_START_BLOCK + 2));
  TEST_ASSERT_FALSE(c4_eth_logs_cache_has_range(TEST_START_BLOCK - 1, TEST_START_BLOCK));
  TEST_ASSERT_FALSE(c4_eth_logs_cache_has_range(TEST_START_BLOCK + TEST_BLOCK_COUNT, TEST_START_BLOCK + TEST_BLOCK_COUNT + 1));
}

void test_simple_address_match(void) {
  load_cache_data();

  // Address known to be in the data (e.g. USDT from receipts_23839615.json: 0xdac17f958d2ee523a2206206994597c13d831ec7)
  // Block 23839615 is TEST_START_BLOCK + 5
  char filter_json[512];
  snprintf(filter_json, sizeof(filter_json),
           "{\"fromBlock\": \"0x%lx\", \"toBlock\": \"0x%lx"
           "\", \"address\": \"0xdac17f958d2ee523a2206206994597c13d831ec7\"}",
           (unsigned long) TEST_START_BLOCK, (unsigned long) (TEST_START_BLOCK + TEST_BLOCK_COUNT - 1));

  json_t result = {0};
  bool   cached = false;
  run_scan_expect_success(json_parse(filter_json), &result, &cached);

  TEST_ASSERT_TRUE(cached);
  TEST_ASSERT_EQUAL(JSON_TYPE_ARRAY, result.type);
  // Verify we found some logs
  TEST_ASSERT_TRUE(json_len(result) > 0);

  // Verify all logs have the correct address
  json_for_each_value(result, log) {
    json_t addr = json_get(log, "address");
    TEST_ASSERT_TRUE(json_matches_string(addr, "0xdac17f958d2ee523a2206206994597c13d831ec7"));
  }
}

void test_topic_match(void) {
  load_cache_data();

  // Filter by topic0 (Transfer event: 0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef)
  char filter_json[512];
  snprintf(filter_json, sizeof(filter_json),
           "{\"fromBlock\": \"0x%lx\", \"toBlock\": \"0x%lx"
           "\", \"topics\": [\"0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef\"]}",
           (unsigned long) TEST_START_BLOCK, (unsigned long) (TEST_START_BLOCK + TEST_BLOCK_COUNT - 1));

  json_t result = {0};
  bool   cached = false;
  run_scan_expect_success(json_parse(filter_json), &result, &cached);

  TEST_ASSERT_TRUE(cached);
  TEST_ASSERT_TRUE(json_len(result) > 0);

  // Verify topic0
  json_for_each_value(result, log) {
    json_t topics = json_get(log, "topics");
    json_t t0     = json_at(topics, 0);
    TEST_ASSERT_TRUE(json_matches_string(t0, "0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef"));
  }
}

void test_wildcard_topic_match(void) {
  load_cache_data();

  // Filter: [null, topic1] - wildcard first topic
  // Find a log with a specific topic1 to test against
  // Example from receipts: topic1 = 0x0000000000000000000000009ee32627a6dde5408c1821b3615c2d42c0575246 (address padding)
  char filter_json[512];
  snprintf(filter_json, sizeof(filter_json),
           "{\"fromBlock\": \"0x%lx\", \"toBlock\": \"0x%lx"
           "\", \"topics\": [null, \"0x0000000000000000000000009ee32627a6dde5408c1821b3615c2d42c0575246\"]}",
           (unsigned long) TEST_START_BLOCK, (unsigned long) (TEST_START_BLOCK + TEST_BLOCK_COUNT - 1));

  json_t result = {0};
  bool   cached = false;
  run_scan_expect_success(json_parse(filter_json), &result, &cached);

  TEST_ASSERT_TRUE(cached);
  TEST_ASSERT_TRUE(json_len(result) > 0);

  json_for_each_value(result, log) {
    json_t topics = json_get(log, "topics");
    json_t t1     = json_at(topics, 1);
    TEST_ASSERT_TRUE(json_matches_string(t1, "0x0000000000000000000000009ee32627a6dde5408c1821b3615c2d42c0575246"));
  }
}

void test_array_variants(void) {
  load_cache_data();

  // Test address array (OR condition)
  // Use USDT and USDC (0xa0b86991c6218b36c1d19d4a2e9eb0ce3606eb48)
  char filter_json[512];
  snprintf(filter_json, sizeof(filter_json),
           "{\"fromBlock\": \"0x%lx\", \"toBlock\": \"0x%lx"
           "\", \"address\": [\"0xdac17f958d2ee523a2206206994597c13d831ec7\", \"0xa0b86991c6218b36c1d19d4a2e9eb0ce3606eb48\"]}",
           (unsigned long) TEST_START_BLOCK, (unsigned long) (TEST_START_BLOCK + TEST_BLOCK_COUNT - 1));

  json_t result = {0};
  bool   cached = false;
  run_scan_expect_success(json_parse(filter_json), &result, &cached);

  TEST_ASSERT_TRUE(cached);
  TEST_ASSERT_TRUE(json_len(result) > 0);

  json_for_each_value(result, log) {
    json_t addr    = json_get(log, "address");
    bool   is_usdt = json_matches_string(addr, "0xdac17f958d2ee523a2206206994597c13d831ec7");
    bool   is_usdc = json_matches_string(addr, "0xa0b86991c6218b36c1d19d4a2e9eb0ce3606eb48");
    TEST_ASSERT_TRUE(is_usdt || is_usdc);
  }
}

void test_metrics(void) {
  load_cache_data();

  uint64_t blocks, txs, events;
  c4_eth_logs_cache_stats(&blocks, &txs, &events);

  TEST_ASSERT_EQUAL_UINT64(TEST_BLOCK_COUNT, blocks);
  TEST_ASSERT_TRUE(txs > 0);
  TEST_ASSERT_TRUE(events > 0);

  // Run a hit query
  test_simple_address_match();

  uint64_t hits, misses, skips;
  c4_eth_logs_cache_counters(&hits, &misses, &skips);
  TEST_ASSERT_TRUE(hits > 0);

  // Run a miss query (range outside)
  c4_prover_free(g_ctx);
  g_ctx = c4_prover_create("eth_getLogs", "[]", 1, 0);
  char filter_json[128];
  snprintf(filter_json, sizeof(filter_json), "{\"fromBlock\": \"0x1\", \"toBlock\": \"0x2\"}");
  json_t result = {0};
  bool   cached = false;
  run_scan_expect_success(json_parse(filter_json), &result, &cached);

  c4_eth_logs_cache_counters(&hits, &misses, &skips);
  TEST_ASSERT_TRUE(misses > 0);
}

void test_bloomfilter_creation(void) {
  for (int i = 0; i < TEST_BLOCK_COUNT; i++) {
    uint64_t bn = TEST_START_BLOCK + i;
    char     filename[128];
    snprintf(filename, sizeof(filename), "log_cache/receipts_%lu.json", (unsigned long) bn);
    bytes_t content = read_testdata(filename);
    TEST_ASSERT_NOT_NULL_MESSAGE(content.data, "Failed to read receipt file");

    json_t response = json_parse((char*) content.data);
    json_t receipts = json_get(response, "result");
    TEST_ASSERT_EQUAL(JSON_TYPE_ARRAY, receipts.type);

    json_for_each_value(receipts, r) {
      uint8_t bloom_receipt[256]  = {0};
      uint8_t bloom_computed[256] = {0};

      TEST_ASSERT_EQUAL_UINT32_MESSAGE(256, json_to_var(json_get(r, "logsBloom"), bloom_receipt), "Unexpected receipt bloom size");

      json_t logs = json_get(r, "logs");
      TEST_ASSERT_EQUAL(JSON_TYPE_ARRAY, logs.type);

      json_for_each_value(logs, l) {
        bytes_t bloom = c4_eth_create_bloomfilter(l);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(256, bloom.len, "Bloom length mismatch");
        for (uint32_t j = 0; j < bloom.len; j++) bloom_computed[j] |= bloom.data[j];
        safe_free(bloom.data);
      }
      TEST_ASSERT_EQUAL_MEMORY_MESSAGE(bloom_receipt, bloom_computed, sizeof(bloom_receipt), "Bloom mismatch");
    }

    safe_free(content.data);
  }
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_bloomfilter_creation);
  RUN_TEST(test_cache_range_check);
  RUN_TEST(test_simple_address_match);
  RUN_TEST(test_topic_match);
  RUN_TEST(test_wildcard_topic_match);
  RUN_TEST(test_array_variants);
  RUN_TEST(test_metrics);
  return UNITY_END();
}

#else


int main(int argc, char** argv) {
  UNITY_BEGIN();
  return UNITY_END();
}
#endif
