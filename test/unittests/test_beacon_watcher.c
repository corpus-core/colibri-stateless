/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: MIT
 *
 * Beacon Watcher tests - SSE stream playback from recorded events
 */

#include "unity.h"

#ifdef HTTP_SERVER

#include "../../src/chains/eth/server/eth_conf.h"
#include "../../src/chains/eth/server/handler.h"
#include "../../src/server/server.h" // Access http_server to toggle stream flag
#include "test_server_helper.h"
#ifndef _WIN32
#include <unistd.h>
#endif

// Unity setup - called before each test
void setUp(void) {
  http_server_t config = {0};
  config.port          = TEST_PORT;
  config.host          = TEST_HOST;
  config.chain_id      = 1;
  config.beacon_nodes  = "http://localhost:5052/"; // Must match recorded mock URLs
  c4_test_server_setup(&config);
}

// Unity teardown - called after each test
void tearDown(void) {
  c4_test_stop_beacon_watcher();
  c4_test_server_teardown();
}

// Test 1: Parse head event from recorded SSE stream
void test_beacon_watcher_head_event(void) {
  c4_test_server_seed_for_test("sse");

  // Point to recorded SSE file
  char sse_file[512];
  snprintf(sse_file, sizeof(sse_file), "file://%s/server/sse/beacon_events.sse", TESTDATA_DIR);
  c4_test_set_beacon_watcher_url(sse_file);
  c4_test_set_beacon_watcher_no_reconnect(true);

  fprintf(stderr, "[TEST] Starting watcher (head_event)\n");
  // Enable watcher start for this test
  eth_config.stream_beacon_events = 1;
  // Start watcher
  c4_watch_beacon_events();

  // Sehr kurze Eventloop-Phase: brich ab, sobald 2 Events verarbeitet wurden (head + finalized)
  for (int i = 0; i < 5; i++) {
    if (http_server.stats.beacon_events_total >= 2) break;
    c4_server_run_once(&server_instance);
    usleep(20000);
  }

  // Verify: last_sync_event should be updated
  TEST_ASSERT_TRUE(http_server.stats.last_sync_event > 0);
  // Verify: events processed
  TEST_ASSERT_TRUE(http_server.stats.beacon_events_total >= 2); // at least head + finalized
  TEST_ASSERT_TRUE(http_server.stats.beacon_events_head >= 1);
  TEST_ASSERT_TRUE(http_server.stats.beacon_events_finalized >= 1);

  // Verify: Block requests should have been made (check mock files were used)
  // This is implicit - if head_update ran, it made the requests
}

// Test 2: SSE parsing - verify events are parsed correctly
void test_beacon_watcher_event_parsing(void) {
  c4_test_server_seed_for_test("sse");

  char sse_file[512];
  snprintf(sse_file, sizeof(sse_file), "file://%s/server/sse/beacon_events.sse", TESTDATA_DIR);
  c4_test_set_beacon_watcher_url(sse_file);
  c4_test_set_beacon_watcher_no_reconnect(true);

  uint64_t start_time = http_server.stats.last_sync_event;

  eth_config.stream_beacon_events = 1;
  c4_watch_beacon_events();

  // Sehr kurze Eventloop-Phase
  for (int i = 0; i < 3; i++) {
    if (http_server.stats.beacon_events_head >= 1) break;
    c4_server_run_once(&server_instance);
    usleep(20000);
  }

  TEST_ASSERT_TRUE(http_server.stats.last_sync_event > start_time);
  TEST_ASSERT_TRUE(http_server.stats.beacon_events_head >= 1);
}

// Test 3: Watcher processes events and stops cleanly
void test_beacon_watcher_stops_after_eof(void) {
  c4_test_server_seed_for_test("sse");

  char sse_file[512];
  snprintf(sse_file, sizeof(sse_file), "file://%s/server/sse/beacon_events.sse", TESTDATA_DIR);
  c4_test_set_beacon_watcher_url(sse_file);
  c4_test_set_beacon_watcher_no_reconnect(true);

  eth_config.stream_beacon_events = 1;
  c4_watch_beacon_events();

  // File-EOF sollte den watcher stoppen (Reconnect disabled)
  for (int i = 0; i < 5; i++) {
    if (!c4_beacon_watcher_is_running()) break;
    c4_server_run_once(&server_instance);
    usleep(20000);
  }

  TEST_ASSERT_FALSE(c4_beacon_watcher_is_running());
  TEST_ASSERT_TRUE(http_server.stats.beacon_events_total >= 1);
}

// Main test runner
int main(void) {
  UNITY_BEGIN();
#ifndef _WIN32
  RUN_TEST(test_beacon_watcher_event_parsing);
  RUN_TEST(test_beacon_watcher_head_event);
  RUN_TEST(test_beacon_watcher_stops_after_eof);
#endif
  return UNITY_END();
}

#else // !HTTP_SERVER

// Stub main when HTTP_SERVER is not enabled
int main(void) {
  fprintf(stderr, "test_beacon_watcher: Skipped (HTTP_SERVER not enabled)\n");
  return 0;
}

#endif // HTTP_SERVER
