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

#ifndef OP_ZSTD_H
#define OP_ZSTD_H

#include "bytes.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decompress ZSTD-compressed data
 *
 * @param compressed_data Pointer to compressed data
 * @param compressed_size Size of compressed data
 * @param decompressed_data Pointer to output buffer (must be pre-allocated)
 * @param decompressed_size Size of output buffer
 * @return Size of decompressed data on success, 0 on error
 */
size_t op_zstd_decompress(
    bytes_t compressed_data,
    bytes_t decompressed_data);

/**
 * @brief Get the decompressed size of ZSTD-compressed data
 *
 * @param compressed_data Pointer to compressed data
 * @param compressed_size Size of compressed data
 * @return Expected decompressed size, or 0 if error/unknown
 */
size_t op_zstd_get_decompressed_size(
    bytes_t compressed_data);

#ifdef __cplusplus
}
#endif

#endif // OP_ZSTD_H
