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

void test_balance() {
  verify("eth_getTransactionReceipt1", "eth_getTransactionReceipt", "[\"0x5f41c75eabb3fee183e0896859a82c81635dbb40edf5630fa29555e8d6c3e7f1\"]", C4_CHAIN_MAINNET);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_balance);
  return UNITY_END();
}