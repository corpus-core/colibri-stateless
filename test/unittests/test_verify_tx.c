// datei: test_addiere.c
#include "c4_assert.h"
#include "unity.h"
#include "util/bytes.h"
#include "util/ssz.h"
void setUp(void) {
  reset_local_filecache();
}

void tearDown(void) {
  reset_local_filecache();
}

void test_balance() {
  verify("eth_getTransactionByHash1", "eth_getTransactionByHash", "[\"0x5f41c75eabb3fee183e0896859a82c81635dbb40edf5630fa29555e8d6c3e7f1\"]");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_balance);
  return UNITY_END();
}