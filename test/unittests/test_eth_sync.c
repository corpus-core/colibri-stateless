// datei: test_addiere.c
#include "bytes.h"
#include "c4_assert.h"
#include "ssz.h"
#include "sync_committee.h"
#include "unity.h"
void setUp(void) {
  reset_local_filecache();
}

void tearDown(void) {
  reset_local_filecache();
}

void test_sync() {
  set_state(C4_CHAIN_MAINNET, "eth_sync");
  bytes_t      update = read_testdata("eth_sync/light_client_update.ssz");
  verify_ctx_t ctx    = {0};
  ctx.chain_id        = C4_CHAIN_MAINNET;
  TEST_ASSERT_TRUE_MESSAGE(c4_handle_client_updates(&ctx, update, NULL), "Failed to update");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_sync);
  return UNITY_END();
}