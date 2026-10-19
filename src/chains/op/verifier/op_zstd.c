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

#include "op_zstd.h"
#include <stdlib.h>
#include <zstd.h>

size_t op_zstd_decompress(
    bytes_t compressed_data,
    bytes_t decompressed_data) {
  if (!compressed_data.data || !decompressed_data.data ||
      compressed_data.len == 0 || decompressed_data.len == 0) {
    return 0;
  }

  size_t result = ZSTD_decompress(
      decompressed_data.data, decompressed_data.len,
      compressed_data.data, compressed_data.len);

  // ZSTD_decompress returns the actual decompressed size on success,
  // or an error code (which can be checked with ZSTD_isError)
  if (ZSTD_isError(result)) {
    return 0;
  }

  return result;
}

size_t op_zstd_get_decompressed_size(
    bytes_t compressed_data) {
  if (!compressed_data.data || compressed_data.len == 0) {
    return 0;
  }

  unsigned long long result = ZSTD_getFrameContentSize(compressed_data.data, compressed_data.len);

  if (result == ZSTD_CONTENTSIZE_ERROR || result == ZSTD_CONTENTSIZE_UNKNOWN) {
    // Fallback: Try to decompress with a reasonable buffer size to get the actual size
    // This is a workaround for ZSTD streams that don't store the content size in the header

    // Try with progressively larger buffers until we succeed
    size_t test_sizes[]   = {64 * 1024, 256 * 1024, 1024 * 1024, 4 * 1024 * 1024, 16 * 1024 * 1024};
    size_t num_test_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);

    for (size_t i = 0; i < num_test_sizes; i++) {
      void* test_buffer = malloc(test_sizes[i]);
      if (!test_buffer) continue;

      size_t decompressed_size = ZSTD_decompress(test_buffer, test_sizes[i],
                                                 compressed_data.data, compressed_data.len);
      free(test_buffer);

      if (!ZSTD_isError(decompressed_size)) {
        return decompressed_size;
      }
    }

    return 0; // All attempts failed
  }

  return (size_t) result;
}
