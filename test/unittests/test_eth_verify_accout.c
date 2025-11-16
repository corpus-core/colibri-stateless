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

void test_balance_electra() {
  verify("eth_getBalance_electra", "eth_getBalance", "[\"0x95222290DD7278Aa3Ddd389Cc1E1d165CC4BAfe5\",\"latest\"]", C4_CHAIN_MAINNET);
}

void test_eth_get_proof() {
  run_rpc_test("eth_getProof2", 0);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_balance);
  RUN_TEST(test_balance_electra);
  RUN_TEST(test_eth_get_proof);
  return UNITY_END();
}