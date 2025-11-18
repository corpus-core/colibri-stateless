/*
 * Server tests for period_store: set_block/write verification and LCU cache read
 */

#include "unity.h"

#ifdef HTTP_SERVER

#include "test_server_helper.h"
#include "chains/eth/server/period_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <unistd.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

static char g_ps_path[512];

static void ensure_dir(const char* path) {
  MKDIR(path);
}

void setUp(void) {
  http_server_t config = (http_server_t) {0};
  config.port          = TEST_PORT;
  config.host          = TEST_HOST;
  config.chain_id      = 1;
  // Dedicated period_store dir for this test binary
  snprintf(g_ps_path, sizeof(g_ps_path), "%s/server/period_store_tests", TESTDATA_DIR);
  ensure_dir(g_ps_path);
  config.period_store = g_ps_path;
  // Disable backfill effects for this phase
  config.period_backfill_max_periods = 0;
  c4_test_server_setup(&config);
}

void tearDown(void) {
  c4_test_server_teardown();
}

static void build_header112(uint8_t out[112], const uint8_t parent_root[32]) {
  memset(out, 0, 112);
  // slot (little endian uint64_t)
  uint64_to_le(out, 0);
  // proposer_index (uint64_t LE) left 0
  // parent_root at +16
  memcpy(out + 16, parent_root, 32);
  // state_root at +48 (zero)
  // body_root at +80 (zero)
}

void test_period_store_set_block_write(void) {
  // Choose a deterministic slot within a known period
  const uint64_t SLOTS_PER_PERIOD = 8192;
  uint64_t       slot             = SLOTS_PER_PERIOD * 2 + 123; // period 2, index 123
  uint64_t       period           = slot / SLOTS_PER_PERIOD;
  uint64_t       idx              = slot % SLOTS_PER_PERIOD;

  uint8_t root[32];
  uint8_t parent[32];
  for (int i = 0; i < 32; i++) {
    root[i]   = (uint8_t) 0xA5;
    parent[i] = (uint8_t) 0x5A;
  }
  uint8_t header112[112];
  build_header112(header112, parent);

  // Invoke writer
  c4_period_sync_on_head(slot, root, header112);

  // Wait for async write to complete by polling for file size change
  char dir[512];
  snprintf(dir, sizeof(dir), "%s/%lu", g_ps_path, (unsigned long) period);
  char blocks_path[512], headers_path[512];
  snprintf(blocks_path, sizeof(blocks_path), "%s/blocks.ssz", dir);
  snprintf(headers_path, sizeof(headers_path), "%s/headers.ssz", dir);

  int tries = 0;
  while (tries++ < 200) { // ~200ms
    uv_run(uv_default_loop(), UV_RUN_NOWAIT);
#ifndef _WIN32
    usleep(1000);
#else
    Sleep(1);
#endif
    FILE* fb = fopen(blocks_path, "rb");
    FILE* fh = fopen(headers_path, "rb");
    if (fb && fh) {
      fclose(fb);
      fclose(fh);
      break;
    }
    if (fb) fclose(fb);
    if (fh) fclose(fh);
  }

  // Verify content at expected offsets
  {
    FILE* f = fopen(blocks_path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    int      rc   = fseek(f, (long) (idx * 32), SEEK_SET);
    TEST_ASSERT_EQUAL(0, rc);
    uint8_t  buf[32] = {0};
    size_t   n       = fread(buf, 1, 32, f);
    fclose(f);
    TEST_ASSERT_EQUAL(32, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(root, buf, 32);
  }
  {
    FILE* f = fopen(headers_path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    long    off  = (long) (idx * 112);
    int     rc   = fseek(f, off, SEEK_SET);
    TEST_ASSERT_EQUAL(0, rc);
    uint8_t buf[112] = {0};
    size_t  n        = fread(buf, 1, 112, f);
    fclose(f);
    TEST_ASSERT_EQUAL(112, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(header112, buf, 112);
  }
}

typedef struct {
  bytes_t out;
  char*   err;
  int     done;
} lcu_ctx_t;

static void lcu_cb(void* user_data, bytes_t updates, char* error) {
  lcu_ctx_t* c = (lcu_ctx_t*) user_data;
  c->out       = updates;
  c->err       = error;
  c->done      = 1;
}

void test_period_store_lcu_cache_read(void) {
  // Prepare lcu.ssz in the expected directory and validate c4_get_light_client_updates reads it
  const uint64_t period = 42;

  char dir[512];
  snprintf(dir, sizeof(dir), "%s/%lu", g_ps_path, (unsigned long) period);
  ensure_dir(dir);
  char lcu_path[512];
  snprintf(lcu_path, sizeof(lcu_path), "%s/lcu.ssz", dir);

  const char* payload = "LCU_PAYLOAD";
  {
    FILE* f = fopen(lcu_path, "wb");
    TEST_ASSERT_NOT_NULL(f);
    fwrite(payload, 1, strlen(payload), f);
    fclose(f);
  }

  lcu_ctx_t ctx = {0};
  c4_get_light_client_updates(&ctx, period, 1, lcu_cb);

  // Drive the loop until callback fires
  for (int i = 0; i < 200 && !ctx.done; i++) {
    uv_run(uv_default_loop(), UV_RUN_NOWAIT);
#ifndef _WIN32
    usleep(1000);
#else
    Sleep(1);
#endif
  }
  TEST_ASSERT_TRUE(ctx.done);
  TEST_ASSERT_NULL(ctx.err);
  TEST_ASSERT_EQUAL(strlen(payload), ctx.out.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, ctx.out.data, ctx.out.len);
  free(ctx.out.data);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_period_store_set_block_write);
  RUN_TEST(test_period_store_lcu_cache_read);
  return UNITY_END();
}

#else
int main(void) {
  fprintf(stderr, "test_server_period_store: Skipped (HTTP_SERVER not enabled)\n");
  return 0;
}
#endif


