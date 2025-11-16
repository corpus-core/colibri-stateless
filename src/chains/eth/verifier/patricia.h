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

#ifndef patricia_h__
#define patricia_h__

#include "bytes.h"
#include "ssz.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef struct node node_t;
typedef enum {
  PATRICIA_INVALID      = 0,
  PATRICIA_FOUND        = 1,
  PATRICIA_NOT_EXISTING = 2,
} patricia_result_t;

patricia_result_t patricia_verify(bytes32_t root, bytes_t path, ssz_ob_t proof, bytes_t* last_value);

ssz_ob_t patricia_create_merkle_proof(node_t* root, bytes_t path);
void     patricia_set_value(node_t** root, bytes_t path, bytes_t value);
void     patricia_node_free(node_t* node);
node_t*  patricia_clone_tree(node_t* node);
bytes_t  patricia_get_root(node_t* node);

#ifdef TEST
void patricia_dump(node_t* root);
#endif

#ifdef __cplusplus
}
#endif

#endif
