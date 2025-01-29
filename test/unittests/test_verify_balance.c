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
  verify("eth_getBalance1", "eth_getBalance", "[\"0x95222290DD7278Aa3Ddd389Cc1E1d165CC4BAfe5\",\"latest\"]");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_balance);
  return UNITY_END();
}