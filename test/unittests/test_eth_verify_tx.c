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
  verify("eth_getTransactionByHash1", "eth_getTransactionByHash", "[\"0xbe5d48ce06f29c69f57e1ac885a0486b7f7198dc1652a7ada78ffd782dc2dcbc\"]", C4_CHAIN_MAINNET);
}

void test_tx_electra() {
  verify("eth_getTransactionByHash_electra", "eth_getTransactionByHash", "[\"0x7e3e4bfb2ac3266669923de636d01911df73fa9d2ae43d72dcbe44f27dc01d10\"]", C4_CHAIN_MAINNET);
}

void test_tx_with_history() {
  verify("eth_getTransactionByHash2", "eth_getTransactionByHash", "[\"0xcaa25fb86d488aff51d177f811753f03b035590d82dc7df737eb2041ee76ae30\"]", C4_CHAIN_MAINNET);
}

void test_tx_by_hash_and_index() {
  run_rpc_test("eth_getTransactionByBlockHashAndIndex1", 0);
}

void test_tx_type_4() {
  run_rpc_test("eth_getTransaction_Type_4", 0);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_tx);
  RUN_TEST(test_tx_by_hash_and_index);
  RUN_TEST(test_tx_with_history);
  RUN_TEST(test_tx_electra);
  RUN_TEST(test_tx_type_4);
  return UNITY_END();
}