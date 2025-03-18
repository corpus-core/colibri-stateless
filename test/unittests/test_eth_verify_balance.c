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
  verify("eth_getBalance1", "eth_getBalance", "[\"0x95222290DD7278Aa3Ddd389Cc1E1d165CC4BAfe5\",\"0x14d0303\"]", C4_CHAIN_MAINNET);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_balance);
  return UNITY_END();
}