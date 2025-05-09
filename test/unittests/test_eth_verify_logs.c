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

void test_denep() {
  verify("eth_getLogs1", "eth_getLogs", "[{\"address\":[\"0xdac17f958d2ee523a2206206994597c13d831ec7\"],\"fromBlock\":\"0x14d7970\",\"toBlock\":\"0x14d7970\"}]", C4_CHAIN_MAINNET);
}

void test_electra() {
  run_rpc_test("eth_getLogs_electra", C4_PROOFER_FLAG_NO_CACHE);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_denep);
  RUN_TEST(test_electra);
  return UNITY_END();
}