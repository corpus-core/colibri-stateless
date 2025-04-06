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

void test_tx() {
  verify("eth_getTransactionByHash1", "eth_getTransactionByHash", "[\"0xbe5d48ce06f29c69f57e1ac885a0486b7f7198dc1652a7ada78ffd782dc2dcbc\"]", C4_CHAIN_MAINNET);
}

void test_tx_by_hash_and_index() {
  run_rpc_test("eth_getTransactionByBlockHashAndIndex1", 0);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_tx);
  RUN_TEST(test_tx_by_hash_and_index);
  return UNITY_END();
}