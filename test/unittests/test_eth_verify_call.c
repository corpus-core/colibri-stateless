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
  char* method          = "eth_call";
  char* args            = "[{\"to\":\"0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48\",\"data\":\"0x70a0823100000000000000000000000037305b1cd40574e4c5ce33f8e8306be057fd7341\"},\"latest\"]";
  char* dir             = "eth_call1";
  char* expected_result = "\"0x000000000000000000000000000000000000000000000000000f2112ee7f7c12\"";

  verify_count(dir, method, args, C4_CHAIN_MAINNET, 1, C4_PROOFER_FLAG_INCLUDE_CODE, expected_result);
  verify_count(dir, method, args, C4_CHAIN_MAINNET, 1, C4_PROOFER_FLAG_INCLUDE_CODE | C4_PROOFER_FLAG_INCLUDE_DATA, expected_result);
}

void test_calls() {
  run_rpc_test("eth_call3", C4_PROOFER_FLAG_NO_CACHE);
  run_rpc_test("eth_call3", C4_PROOFER_FLAG_INCLUDE_CODE);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_tx);
  RUN_TEST(test_calls);
  return UNITY_END();
}