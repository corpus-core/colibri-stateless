/*
 * Copyright (c) 2025 corpus.core
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include "bytes.h"
#include "c4_assert.h"
#include "ssz.h"
#include "tx_cache.h"
#include "unity.h"
#ifdef PAP
#include "pap_tx_cache.h"
#endif
void setUp(void) {
  reset_local_filecache();
#ifdef PROVER_CACHE
  c4_eth_tx_cache_reset();
#endif
#ifdef PAP
  pap_tx_cache_reset();
#endif
}

void tearDown(void) {
  reset_local_filecache();
}

void test_tx() {
  run_rpc_test("eth_getTransactionByHash1", 0, 0);
}

void test_tx_electra() {
  run_rpc_test("eth_getTransactionByHash_electra", 0, 0);
}

void test_tx_with_history() {
  run_rpc_test("eth_getTransactionByHash2", C4_PROVER_FLAG_INCLUDE_CODE | C4_PROVER_FLAG_CHAIN_STORE, 0);
}

void test_tx_by_hash_and_index() {
  run_rpc_test("eth_getTransactionByBlockHashAndIndex1", 0,0);
}

void test_tx_type_4() {
  run_rpc_test("eth_getTransaction_Type_4", 0, 0);
}

#ifdef PAP
void test_pap_tx_by_hash() {
  run_rpc_test("pap_tx_by_hash", 0, VERIFY_FLAG_PAP);
}

void test_pap_tx_by_block_index() {
  run_rpc_test("pap_tx_by_block_index", 0, VERIFY_FLAG_PAP);
}

void test_pap_tx_pending() {
  run_rpc_test("pap_tx_pending", 0, VERIFY_FLAG_PAP);
}

void test_pap_tx_fallback() {
  run_rpc_test("pap_tx_fallback", 0, VERIFY_FLAG_PAP);
}
#endif

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_tx);
  RUN_TEST(test_tx_by_hash_and_index);
  RUN_TEST(test_tx_with_history);
  RUN_TEST(test_tx_electra);
  RUN_TEST(test_tx_type_4);
#ifdef PAP
  RUN_TEST(test_pap_tx_by_hash);
  RUN_TEST(test_pap_tx_by_block_index);
  RUN_TEST(test_pap_tx_pending);
  RUN_TEST(test_pap_tx_fallback);
#endif
  return UNITY_END();
}