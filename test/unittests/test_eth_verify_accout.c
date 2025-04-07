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
  verify("eth_getBalance1", "eth_getBalance", "[\"0x95222290DD7278Aa3Ddd389Cc1E1d165CC4BAfe5\",\"latest\"]", C4_CHAIN_MAINNET);
}

void test_eth_get_proof() {
  run_rpc_test("eth_getProof2", 0);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_balance);
  RUN_TEST(test_eth_get_proof);
  return UNITY_END();
}