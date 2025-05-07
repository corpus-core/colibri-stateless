// datei: test_addiere.c
#include "bytes.h"
#include "c4_assert.h"
#include "ssz.h"
#include "unity.h"
void setUp(void) {
  reset_local_filecache();
}

void tearDown(void) {
  reset_local_filecache();
}

void test_nonce() {
  run_rpc_test("eth_getTransactionCount1", 0);
  run_rpc_test("eth_getTransactionCount1", C4_PROOFER_FLAG_NO_CACHE);
}
void test_nonce_electra() {
  run_rpc_test("eth_getTransactionCount_electra", C4_PROOFER_FLAG_NO_CACHE);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_nonce);
  RUN_TEST(test_nonce_electra);
  return UNITY_END();
}