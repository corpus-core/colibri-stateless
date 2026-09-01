/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */

#include "bytes.h"
#include "chains.h"
#include "json.h"
#include "unity.h"

#include "cli/default_chains.generated.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void assert_alias(const char* name, chain_id_t expected) {
  chain_id_t id = 0;
  TEST_ASSERT_TRUE_MESSAGE(c4_default_chain_id_from_name(name, &id), name);
  TEST_ASSERT_EQUAL_UINT64(expected, id);
}

static char* prover_at(chain_id_t chain_id, size_t index, buffer_t* buf) {
  const char* cfg = c4_default_chain_config_json(chain_id);
  TEST_ASSERT_NOT_NULL(cfg);
  json_t provers = json_get(json_parse(cfg), "prover");
  TEST_ASSERT_EQUAL_INT(JSON_TYPE_ARRAY, provers.type);
  TEST_ASSERT_TRUE(json_len(provers) > index);
  return json_as_string(json_at(provers, index), buf);
}

static void assert_config_has_lists(chain_id_t chain_id) {
  const char* cfg = c4_default_chain_config_json(chain_id);
  TEST_ASSERT_NOT_NULL(cfg);
  json_t json = json_parse(cfg);
  TEST_ASSERT_EQUAL_INT(JSON_TYPE_OBJECT, json.type);
  TEST_ASSERT_TRUE(json_len(json_get(json, "prover")) > 0);
  TEST_ASSERT_TRUE(json_len(json_get(json, "eth_rpc")) > 0);
  TEST_ASSERT_TRUE(json_len(json_get(json, "beacon_api")) > 0);
  TEST_ASSERT_TRUE(json_len(json_get(json, "checkpointz")) > 0);
}

void test_name_aliases_resolve_known_chains(void) {
  assert_alias("mainnet", 1ULL);
  assert_alias("eth", 1ULL);
  assert_alias("0x1", 1ULL);
  assert_alias("sepolia", 11155111ULL);
  assert_alias("0xaa36a7", 11155111ULL);
  assert_alias("gnosis", 100ULL);
  assert_alias("xdai", 100ULL);
  assert_alias("0x64", 100ULL);
  assert_alias("chiado", 10200ULL);
  assert_alias("0x27d8", 10200ULL);
  assert_alias("plataberget", 7091047534ULL);
  assert_alias("glamsterdam-devnet-8", 7091047534ULL);
  assert_alias("0x1a6a8cc6e", 7091047534ULL);
}

void test_unknown_or_null_name_is_rejected(void) {
  chain_id_t id = 42;
  TEST_ASSERT_FALSE(c4_default_chain_id_from_name("unknown", &id));
  TEST_ASSERT_EQUAL_UINT64(42, id);
  TEST_ASSERT_FALSE(c4_default_chain_id_from_name(NULL, &id));
  TEST_ASSERT_FALSE(c4_default_chain_id_from_name("mainnet", NULL));
}

void test_known_chains_have_config_lists(void) {
  const chain_id_t ids[] = {1ULL, 11155111ULL, 100ULL, 10200ULL, 7091047534ULL};
  for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++)
    assert_config_has_lists(ids[i]);
}

void test_unknown_chain_has_no_config(void) {
  TEST_ASSERT_NULL(c4_default_chain_config_json(999999ULL));
}

void test_cloudflare_prover_first_and_direct_lb_second(void) {
  buffer_t buf = {0};
  TEST_ASSERT_EQUAL_STRING("https://mainnet.colibri-proof.tech", prover_at(1ULL, 0, &buf));
  TEST_ASSERT_EQUAL_STRING("https://mainnet1.colibri-proof.tech", prover_at(1ULL, 1, &buf));
  TEST_ASSERT_EQUAL_STRING("https://sepolia.colibri-proof.tech", prover_at(11155111ULL, 0, &buf));
  TEST_ASSERT_EQUAL_STRING("https://sepolia1.colibri-proof.tech", prover_at(11155111ULL, 1, &buf));
  TEST_ASSERT_EQUAL_STRING("https://gnosis.colibri-proof.tech", prover_at(100ULL, 0, &buf));
  TEST_ASSERT_EQUAL_STRING("https://gnosis1.colibri-proof.tech", prover_at(100ULL, 1, &buf));
  buffer_free(&buf);
}

void test_plataberget_defaults(void) {
  const char* cfg = c4_default_chain_config_json(7091047534ULL);
  TEST_ASSERT_NOT_NULL(cfg);

  buffer_t buf = {0};
  json_t   json = json_parse(cfg);
  TEST_ASSERT_EQUAL_STRING("https://plataberget.colibri-proof.tech",
                           json_as_string(json_at(json_get(json, "prover"), 0), &buf));
  TEST_ASSERT_EQUAL_STRING("https://plataberget.colibri-proof.tech/execution",
                           json_as_string(json_at(json_get(json, "eth_rpc"), 0), &buf));
  TEST_ASSERT_EQUAL_STRING("https://plataberget.colibri-proof.tech/consensus",
                           json_as_string(json_at(json_get(json, "beacon_api"), 0), &buf));
  buffer_free(&buf);
}

void test_removed_dead_urls_are_absent(void) {
  const chain_id_t ids[] = {1ULL, 11155111ULL, 100ULL, 10200ULL, 7091047534ULL};
  const char*      dead[] = {
      "sepolia.drpc.org",
      "sepolia-prover.incubed.net",
      "sepolia.colimind.com",
      "gnosis-prover.incubed.net",
      "gnosis.colimind.com",
  };

  for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
    const char* cfg = c4_default_chain_config_json(ids[i]);
    TEST_ASSERT_NOT_NULL(cfg);
    for (size_t d = 0; d < sizeof(dead) / sizeof(dead[0]); d++) {
      TEST_ASSERT_NULL_MESSAGE(strstr(cfg, dead[d]), dead[d]);
    }
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_name_aliases_resolve_known_chains);
  RUN_TEST(test_unknown_or_null_name_is_rejected);
  RUN_TEST(test_known_chains_have_config_lists);
  RUN_TEST(test_unknown_chain_has_no_config);
  RUN_TEST(test_cloudflare_prover_first_and_direct_lb_second);
  RUN_TEST(test_plataberget_defaults);
  RUN_TEST(test_removed_dead_urls_are_absent);
  return UNITY_END();
}
