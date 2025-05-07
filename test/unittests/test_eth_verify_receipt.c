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

void test_denep() {
  verify("eth_getTransactionReceipt1", "eth_getTransactionReceipt", "[\"0x1c6a9f182ee398a34d6e4a28bbb55f8f5b10101d60589279b26cec199f021d99\"]", C4_CHAIN_MAINNET);
}

void test_electra() {
  verify("eth_getTransactionreceipt_electra", "eth_getTransactionReceipt", "[\"0xf7887c3f56c041c3f99506bb3b44482e77ecb6b645689ea451b5749e3f2bf48f\"]", C4_CHAIN_MAINNET);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_denep);
  RUN_TEST(test_electra);
  return UNITY_END();
}