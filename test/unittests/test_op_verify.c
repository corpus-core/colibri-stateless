/*
 * Copyright (c) 2026 corpus.core
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

#include "c4_assert.h"
#include "unity.h"

void setUp(void) {
  reset_local_filecache();
}

void tearDown(void) {
  reset_local_filecache();
}

#ifndef NO_CHAIN_OP

void test_op_eth_block_number_remote(void) {
  run_rpc_test("op_eth_blockNumber", 0, 0);
}

void test_op_eth_get_block_by_number_remote(void) {
  run_rpc_test("op_eth_getBlockByNumber", 0, 0);
}

#endif

int main(void) {
  UNITY_BEGIN();
#ifndef NO_CHAIN_OP
  RUN_TEST(test_op_eth_block_number_remote);
  RUN_TEST(test_op_eth_get_block_by_number_remote);
#endif
  return UNITY_END();
}
