/*
 * Backfill tests using recorded SSE head events and file-based Beacon API mocks
 */

#include "unity.h"

#ifdef HTTP_SERVER

#include "../../src/chains/eth/server/handler.h"
#include "../../src/server/server.h"
#include "test_server_helper.h"

#ifndef _WIN32
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#endif

static char g_ps_path[512];

static void ensure_dir(const char* path) {
#ifdef _WIN32
#include <windows.h>
  CreateDirectoryA(path, NULL);
#else
  mkdir(path, 0755);
#endif
}

void setUp(void) {
  http_server_t config        = (http_server_t) {0};
  config.port                 = TEST_PORT;
  config.host                 = TEST_HOST;
  config.chain_id             = 1;
  config.stream_beacon_events = 0;                                // Start watcher manually
  config.beacon_nodes         = (char*) "http://localhost:5052/"; // match recorded URLs
  snprintf(g_ps_path, sizeof(g_ps_path), "%s/server/period_backfill", TESTDATA_DIR);
  ensure_dir(g_ps_path);
  config.period_store = g_ps_path;
  // Conservative backfill config to limit runtime
  config.period_backfill_max_periods = 1;
  config.period_backfill_delay_ms    = 0;
  c4_test_server_setup(&config);
}

void tearDown(void) {
  c4_stop_beacon_watcher();
  c4_test_server_teardown();
}

static int file_exists(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) return 0;
  fclose(f);
  return 1;
}

static int read_slot_root(const char* blocks_path, size_t idx, uint8_t out[32]) {
  FILE* f = fopen(blocks_path, "rb");
  if (!f) return 0;
  if (fseek(f, (long) (idx * 32), SEEK_SET) != 0) {
    fclose(f);
    return 0;
  }
  size_t n = fread(out, 1, 32, f);
  fclose(f);
  return n == 32 ? 1 : 0;
}

static int all_zero(const uint8_t* d, size_t n) {
  for (size_t i = 0; i < n; i++)
    if (d[i] != 0) return 0;
  return 1;
}

void test_period_backfill_writes_head_slot(void) {
  // Use recorded SSE
  // Use 'headers' mock set for Beacon API requests while reusing SSE from 'sse'
  c4_test_server_seed_for_test("headers");
  char sse_file[512];
  snprintf(sse_file, sizeof(sse_file), "file://%s/server/sse/beacon_events.sse", TESTDATA_DIR);
  c4_test_set_beacon_watcher_url(sse_file);
  c4_test_set_beacon_watcher_no_reconnect(true);

  http_server.stream_beacon_events = 1;
  c4_watch_beacon_events();

  // Let the watcher process head(s); wait up to ~5s
  {
    const int max_iters = 500;
    for (int i = 0; i < max_iters && http_server.stats.beacon_events_head < 1 && http_server.stats.period_sync_last_slot == 0; i++) {
      c4_server_run_once(&server_instance);
#ifndef _WIN32
      usleep(10000);
#else
      Sleep(10);
#endif
    }
  }

  // Expect at least one head processed
  TEST_ASSERT_TRUE(http_server.stats.beacon_events_head >= 1);

  // Wait until at least one period write completed (period_sync_last_slot > 0), up to ~5s
  {
    const int max_iters = 500;
    for (int i = 0; i < max_iters && http_server.stats.period_sync_last_slot == 0; i++) {
      c4_server_run_once(&server_instance);
#ifndef _WIN32
      usleep(10000);
#else
      Sleep(10);
#endif
    }
  }
  TEST_ASSERT_TRUE(http_server.stats.period_sync_last_slot > 0);

  // Wait briefly for async writes/backfill to flush
  {
    const int max_iters = 300;
    for (int i = 0; i < max_iters; i++) {
      c4_server_run_once(&server_instance);
#ifndef _WIN32
      usleep(10000);
#else
      Sleep(10);
#endif
    }
  }

  // Validate at the precise last written slot according to stats
  const uint64_t SLOTS_PER_PERIOD = 8192;
  uint64_t       last_slot        = http_server.stats.period_sync_last_slot;
  TEST_ASSERT_TRUE(last_slot > 0);
  uint64_t period = last_slot / SLOTS_PER_PERIOD;
  uint64_t idx    = last_slot % SLOTS_PER_PERIOD;
  char     dir[512];
  snprintf(dir, sizeof(dir), "%s/%lu", g_ps_path, (unsigned long) period);
  char blocks_path[512];
  snprintf(blocks_path, sizeof(blocks_path), "%s/blocks.ssz", dir);
  // Wait up to ~2s for blocks.ssz to appear
  {
    const int max_iters = 200;
    for (int i = 0; i < max_iters && !file_exists(blocks_path); i++) {
      c4_server_run_once(&server_instance);
#ifndef _WIN32
      usleep(10000);
#else
      Sleep(10);
#endif
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(file_exists(blocks_path), "blocks.ssz missing for computed period");
  uint8_t buf[32];
  TEST_ASSERT_TRUE(read_slot_root(blocks_path, (size_t) idx, buf));
  TEST_ASSERT_FALSE(all_zero(buf, 32));
}

int main(void) {
  UNITY_BEGIN();
#ifndef _WIN32
  RUN_TEST(test_period_backfill_writes_head_slot);
#endif
  return UNITY_END();
}

#else
int main(void) {
  fprintf(stderr, "test_server_period_backfill: Skipped (HTTP_SERVER not enabled)\n");
  return 0;
}
#endif
