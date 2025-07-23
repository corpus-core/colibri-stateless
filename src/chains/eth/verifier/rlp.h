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

#ifndef rlp_h__
#define rlp_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "bytes.h"
#include "crypto.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
  RLP_SUCCESS      = 0,
  RLP_OUT_OF_RANGE = -1,
  RLP_NOT_FOUND    = -2,
  RLP_ITEM         = 1,
  RLP_LIST         = 2
} rlp_type_t;

rlp_type_t rlp_decode(bytes_t* data, int index, bytes_t* target);
uint64_t   rlp_get_uint64(bytes_t data, int index);

void rlp_add_uint64(buffer_t* buf, uint64_t value);
void rlp_add_uint(buffer_t* buf, bytes_t data);
void rlp_add_item(buffer_t* buf, bytes_t data);
void rlp_add_list(buffer_t* buf, bytes_t data);
void rlp_to_list(buffer_t* buf);

#ifdef __cplusplus
}
#endif

#endif
