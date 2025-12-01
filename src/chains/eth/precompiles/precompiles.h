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

#ifndef PRECOMPILES_H
#define PRECOMPILES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "bytes.h"
#include "crypto.h"
#include "json.h"
#include "state.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
  PRE_SUCCESS         = 0, ///< Execution successful
  PRE_ERROR           = 1, ///< General error (e.g., internal failure)
  PRE_OUT_OF_BOUNDS   = 2, ///< Input data too short or invalid format
  PRE_INVALID_INPUT   = 3, ///< Mathematical invalidity (e.g., point not on curve)
  PRE_INVALID_ADDRESS = 4, ///< Address not a supported precompile
  PRE_NOT_SUPPORTED   = 5, ///< Precompile not implemented
} pre_result_t;

/**
 * @brief Executes an Ethereum precompile contract.
 * 
 * @param address The address of the precompile (20 bytes). Usually only the last byte is checked (e.g. 0x01, 0x08).
 * @param input The input data for the precompile call.
 * @param output Pointer to a buffer where the output will be written. The buffer data will be allocated/resized.
 * @param gas_used Pointer to a uint64_t where the consumed gas cost will be written.
 * @return PRE_SUCCESS on success, or an error code indicating the failure reason.
 */
pre_result_t eth_execute_precompile(const uint8_t* address, const bytes_t input, buffer_t* output, uint64_t* gas_used);

/**
 * Inject the trusted-setup G2^tau point (compressed, 96 bytes) for the KZG precompile.
 * Allows runtime provisioning (e.g., in WASM) when not embedded at build time.
 * @param comp96 96-byte compressed G2^tau (tau^1) in big-endian format
 * @return true on success
 */
bool precompiles_kzg_set_trusted_setup_g2_tau(const uint8_t* comp96);

#ifdef __cplusplus
}
#endif

#endif