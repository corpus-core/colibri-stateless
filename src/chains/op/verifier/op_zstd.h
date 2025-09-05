/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
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
