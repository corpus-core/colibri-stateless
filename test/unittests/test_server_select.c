/*
 * Tests for http_servers_select.c (selection, health, method support)
 */

#include "unity.h"

#ifdef HTTP_SERVER

#include "../../src/server/server.h"
#include <stdlib.h>
#include <string.h>

static server_list_t servers = {0};

static void init_servers(size_t count) {
  memset(&servers, 0, sizeof(servers));
  servers.count        = count;
  servers.urls         = (char**) calloc(count, sizeof(char*));
  servers.health_stats = (server_health_t*) calloc(count, sizeof(server_health_t));
  servers.client_types = (beacon_client_type_t*) calloc(count, sizeof(beacon_client_type_t));
  for (size_t i = 0; i < count; i++) {
    servers.urls[i]                          = strdup("http://example");
    servers.health_stats[i].is_healthy       = true;
    servers.health_stats[i].recovery_allowed = true;
  }
}

static void free_servers(void) {
  if (servers.urls) {
    for (size_t i = 0; i < servers.count; i++) free(servers.urls[i]);
  }
  free(servers.urls);
  free(servers.health_stats);
  free(servers.client_types);
  memset(&servers, 0, sizeof(servers));
}

void setUp(void) {}
void tearDown(void) { free_servers(); }

void test_select_prefers_healthy(void) {
  init_servers(2);
  servers.health_stats[1].is_healthy = false;

  int idx = c4_select_best_server(&servers, 0, 0);
  TEST_ASSERT_EQUAL(0, idx);
}

void test_has_available_with_exclude_mask(void) {
  init_servers(2);
  // Exclude server 0 via bitmask 1<<0
  bool ok = c4_has_available_servers(&servers, 1);
  TEST_ASSERT_TRUE(ok);

  int idx = c4_select_best_server(&servers, 1, 0);
  TEST_ASSERT_EQUAL(1, idx);
}

void test_method_support_mark_unsupported(void) {
  init_servers(1);

  // Initially supported
  TEST_ASSERT_TRUE(c4_is_method_supported(&servers, 0, "web3_clientVersion"));

  c4_mark_method_unsupported(&servers, 0, "web3_clientVersion");
  TEST_ASSERT_FALSE(c4_is_method_supported(&servers, 0, "web3_clientVersion"));
}

void test_update_server_health_counters(void) {
  init_servers(1);
  server_health_t* h = &servers.health_stats[0];

  c4_update_server_health(&servers, 0, 123, true);
  TEST_ASSERT_EQUAL_UINT64(1, h->total_requests);
  TEST_ASSERT_EQUAL_UINT64(1, h->successful_requests);
  TEST_ASSERT_TRUE(h->total_response_time >= 123);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_select_prefers_healthy);
  RUN_TEST(test_has_available_with_exclude_mask);
  RUN_TEST(test_method_support_mark_unsupported);
  RUN_TEST(test_update_server_health_counters);
  return UNITY_END();
}

#else
int main(void) {
  fprintf(stderr, "test_server_select: Skipped (HTTP_SERVER not enabled)\n");
  return 0;
}
#endif
