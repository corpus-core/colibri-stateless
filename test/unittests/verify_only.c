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

#include "chains/eth/verifier/patricia.h"
#include "platform_compat.h"
#include "unity.h"
#include "util/bytes.h"
#include "util/json.h"
#include "util/plugin.h"
#include "util/ssz.h"
#include "verifier/verify.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {
  setenv("C4_STATES_DIR", TESTDATA_DIR "/eth_getLogs1", 1);
}

void test_verify_only() {
  verify_ctx_t     verify_ctx = {0};
  storage_plugin_t storage;
  buffer_t         proof_buf = {0};
  c4_get_storage_config(&storage);
  storage.get("proof.ssz", &proof_buf);

  c4_verify_from_bytes(&verify_ctx, proof_buf.data, "eth_getLogs", json_parse("[{\"address\":[\"0xdac17f958d2ee523a2206206994597c13d831ec7\"],\"fromBlock\":\"0x14d7970\",\"toBlock\":\"0x14d7970\"}]"), C4_CHAIN_MAINNET);
  buffer_free(&proof_buf);
}
int main(void) {
  setUp();
  test_verify_only();
}