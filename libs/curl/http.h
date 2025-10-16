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

#ifndef __C4_CURL_H__
#define __C4_CURL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "../../src/proofer/proofer.h"

typedef struct {
  json_t checkpointz;
  json_t beacon_api;
  json_t eth_rpc;
  json_t proofer;
  json_t chain_store;
} curl_nodes_t;

void curl_fetch(data_request_t* req);
void curl_fetch_all(c4_state_t* state);
void curl_set_config(json_t config);
void curl_set_chain_store(const char* dir);
#ifdef TEST
char* curl_set_test_dir(const char* dir);
void  curl_set_cache_dir(const char* dir);
#endif

#ifdef __cplusplus
}
#endif

#endif