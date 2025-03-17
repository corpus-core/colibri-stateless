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
  verify("eth_call1", "eth_call", "[{\"to\":\"0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48\",\"data\":\"0x70a0823100000000000000000000000037305b1cd40574e4c5ce33f8e8306be057fd7341\"},\"latest\"]", C4_CHAIN_MAINNET);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_tx);
  return UNITY_END();
}