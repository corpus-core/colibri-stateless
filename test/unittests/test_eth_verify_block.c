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

void test_block_by_number() {
  run_rpc_test("eth_getBlockByNumber1", C4_PROOFER_FLAG_NO_CACHE);
}

void test_block_by_hash() {
  run_rpc_test("eth_getBlockByHash1", C4_PROOFER_FLAG_NO_CACHE);
}

void test_block_by_trusted_hash() {
  run_rpc_test("trusted_block1", C4_PROOFER_FLAG_NO_CACHE);
}

void test_block_electra() {
  run_rpc_test("eth_getBlockByNumber_electra", C4_PROOFER_FLAG_NO_CACHE);
}

void test_block_number_electra() {
  run_rpc_test("eth_blockNumber_electra", C4_PROOFER_FLAG_NO_CACHE);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_block_by_number);
  RUN_TEST(test_block_by_hash);
  RUN_TEST(test_block_by_trusted_hash);
  RUN_TEST(test_block_electra);
  RUN_TEST(test_block_number_electra);
  return UNITY_END();
}