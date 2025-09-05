/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "op_zstd.h"
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
    return 0;
  }

  return (size_t) result;
}
