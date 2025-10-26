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
#include "unity.h"

void setUp(void) {
  reset_local_filecache();
}

void tearDown(void) {
  reset_local_filecache();
}

// Test 1: Simple transaction simulation (no events)
void test_simulate_simple() {
  // No special flags needed - uses state from test data
  run_rpc_test("simulate_simple", 0);
}

// Test 2: WETH deposit simulation with events
void test_simulate_weth_deposit() {
  // INCLUDE_CODE flag because contract code is included in test data (code_d0a06...)
  // Could also run without flag since code file is present in test directory
  run_rpc_test("simulate_weth", C4_PROVER_FLAG_INCLUDE_CODE);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_simulate_simple);
  RUN_TEST(test_simulate_weth_deposit);
  return UNITY_END();
}
