/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
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
