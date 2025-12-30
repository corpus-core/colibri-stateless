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

#ifndef __PLUGIN_H__
#define __PLUGIN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "bytes.h"

#ifdef FILE_STORAGE
extern char* state_data_dir;
#endif
// storage plugin

typedef struct {
  bool (*get)(char* key, buffer_t* buffer);
  void (*set)(char* key, bytes_t value);
  void (*del)(char* key);
  uint32_t max_sync_states;
} storage_plugin_t;

void c4_get_storage_config(storage_plugin_t* plugin);
void c4_set_storage_config(storage_plugin_t* plugin);

/**
 * Optional parallel-for hook.
 *
 * This allows embedded targets to parallelize expensive per-item loops (e.g. deserializing 512 BLS pubkeys)
 * without adding platform-specific dependencies to colibri-stateless.
 *
 * The implementation must execute `body(i, ctx)` for all i in [begin, end) and return only after completion.
 * Implementations may execute the work serially.
 *
 * @param i current loop index
 * @param ctx user context pointer
 */
typedef void (*c4_parallel_for_body_fn)(int i, void* ctx);

/**
 * Parallel-for executor.
 *
 * @param begin inclusive begin index
 * @param end exclusive end index
 * @param body loop body callback
 * @param ctx user context pointer passed to body
 */
typedef void (*c4_parallel_for_fn)(int begin, int end, c4_parallel_for_body_fn body, void* ctx);

/**
 * Registers a parallel-for implementation. Pass NULL to disable.
 *
 * @param fn parallel-for implementation or NULL
 */
void c4_set_parallel_for(c4_parallel_for_fn fn);

/**
 * Returns the registered parallel-for implementation, or NULL if none is registered.
 *
 * @return parallel-for implementation or NULL
 */
c4_parallel_for_fn c4_get_parallel_for(void);

#ifdef __cplusplus
}
#endif

#endif