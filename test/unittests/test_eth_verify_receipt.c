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
  verify("eth_getTransactionReceipt1", "eth_getTransactionReceipt", "[\"0x1c6a9f182ee398a34d6e4a28bbb55f8f5b10101d60589279b26cec199f021d99\"]", C4_CHAIN_MAINNET);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_balance);
  return UNITY_END();
}