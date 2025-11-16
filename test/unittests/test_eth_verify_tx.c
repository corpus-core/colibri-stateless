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

// datei: test_addiere.c
#include "bytes.h"
#include "c4_assert.h"
#include "ssz.h"
#include "tx_cache.h"
#include "unity.h"
void setUp(void) {
  reset_local_filecache();
#ifdef PROVER_CACHE
  c4_eth_tx_cache_reset();
#endif
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