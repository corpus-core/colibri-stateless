/*
 * Server store tests
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HTTP_SERVER

#include "../../src/server/server.h"
#include "unity.h"

bool c4_get_from_store(const char* path, void* uptr, handle_stored_data_cb cb);
bool c4_get_from_store_by_type(chain_id_t chain_id, uint64_t period, store_type_t type, uint32_t slot, void* uptr, handle_stored_data_cb cb);
bool c4_get_preconf(chain_id_t chain_id, uint64_t block_number, char* file_name, void* uptr, handle_preconf_data_cb cb);

typedef struct {
  bytes_t     data;
  const char* err;
  uint64_t    id;
} cb_capture_t;

static void store_cb(void* u, uint64_t id, bytes_t data, const char* err) {
  cb_capture_t* c = (cb_capture_t*) u;
  c->id           = id;
  c->data         = data;
  c->err          = err;
}

static void preconf_cb(void* u, uint64_t block_number, bytes_t data, const char* err) {
  cb_capture_t* c = (cb_capture_t*) u;
  c->id           = block_number;
  c->data         = data;
  c->err          = err;
}

void setUp(void) {
  // period_store read-only testdata path
  http_server.period_store = (char*) "test/data/server/period_store";
  // preconf directory (read-only test data root as well)
  http_server.preconf_storage_dir = (char*) "test/data/server/period_store";
}

void tearDown(void) {}

void test_store_get_from_store_missing(void) {
  cb_capture_t cap = {0};
  bool         ok  = c4_get_from_store("nonexistent/1/headers.ssz", &cap, store_cb);
  TEST_ASSERT_TRUE(ok); // async
  // We cannot synchronously assert contents without uv loop; just check function returns.
}

void test_store_get_by_type_paths(void) {
  cb_capture_t cap = {0};
  // This exercises path building; callback will be hit by loop in integration context
  bool ok = c4_get_from_store_by_type(1, 1, STORE_TYPE_BLOCK_HEADER, 0, &cap, store_cb);
  TEST_ASSERT_TRUE(ok);
}

void test_store_get_preconf_missing(void) {
  cb_capture_t cap = {0};
  bool         ok  = c4_get_preconf(1, 123, NULL, &cap, preconf_cb);
  TEST_ASSERT_TRUE(ok);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_store_get_from_store_missing);
  RUN_TEST(test_store_get_by_type_paths);
  RUN_TEST(test_store_get_preconf_missing);
  return UNITY_END();
}

#else // !HTTP_SERVER

// Stub main when HTTP_SERVER is not enabled
int main(void) {
  fprintf(stderr, "test_server_store: Skipped (HTTP_SERVER not enabled)\n");
  return 0;
}

#endif // HTTP_SERVER