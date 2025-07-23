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

#include "bytes.h"
#include "crypto.h"
#include "json.h"
#include "precompiles.h"
#include "ripemd160.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef INTX
#include "intx_c_api.h"
#endif

typedef pre_result_t (*precompile_func_t)(bytes_t input, buffer_t* output, uint64_t* gas_used);

#define PRECOMPILE_FN_COUNT 7
#define data_word_size(x)   ((x + 31) / 32)

#include "precompiles_ec.c"

static pre_result_t pre_ecrecover(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  if (input.len != 128) return PRE_INVALID_INPUT;
  bytes_t hash       = bytes_slice(input, 0, 32);
  uint8_t v          = input.data[63];
  uint8_t sig[65]    = {0};
  uint8_t pubkey[64] = {0};
  memcpy(sig, input.data + 64, 64); // copy r s
  sig[64] = v > 28 ? (v % 2 ? 27 : 28) : v;

  if (!secp256k1_recover(hash.data, bytes(sig, 65), pubkey)) return PRE_INVALID_INPUT;

  keccak(bytes(pubkey, 64), sig);
  memset(sig, 0, 12);
  output->data.len = 0;
  buffer_append(output, bytes(sig, 32));
  *gas_used = 3000;
  return PRE_SUCCESS;
}

static pre_result_t pre_sha256(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  output->data.len = 0;
  buffer_grow(output, 32);
  sha256(input, output->data.data);
  *gas_used = 60 + 12 * data_word_size(input.len);
  return PRE_SUCCESS;
}
#ifdef PRECOMPILED_RIPEMD160
static pre_result_t pre_ripemd160(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  output->data.len = 0;
  buffer_grow(output, 20);
  ripemd160(input.data, input.len, output->data.data);
  *gas_used = 600 + 120 * data_word_size(input.len);
  return PRE_SUCCESS;
}
#endif
static pre_result_t pre_identity(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  output->data.len = 0;
  buffer_grow(output, input.len);
  memcpy(output->data.data, input.data, input.len);
  *gas_used = 15 + 3 * data_word_size(input.len);
  return PRE_SUCCESS;
}

#ifdef INTX

static uint64_t calculate_gas_for_modexp(uint32_t l_base, uint32_t l_exp, uint32_t l_mod, bytes_t b_exp) {

  uint64_t max_len                   = l_base > l_mod ? l_base : l_mod;
  uint32_t words                     = (max_len + 7) / 8;
  uint32_t multiplication_complexity = words * words;
  uint32_t iteration_count           = 0;

  if (l_exp <= 32 && bytes_all_zero(b_exp)) {
    iteration_count = 0;
  }
  else if (l_exp <= 32) {
    // Calculate bit_length() - 1 of the exponent
    uint32_t bit_length = 0;
    for (int i = 0; i < b_exp.len; i++) {
      if (b_exp.data[i] != 0) {
        uint8_t byte = b_exp.data[i];
        for (int j = 7; j >= 0; j--) {
          if ((byte >> j) & 1) {
            bit_length = i * 8 + j + 1;
            break;
          }
        }
        if (bit_length > 0) break;
      }
    }
    iteration_count = bit_length > 0 ? bit_length - 1 : 0;
  }
  else if (l_exp > 32) {
    // 8 * (Esize - 32) + bit_length of lower 256 bits - 1
    uint32_t base_count = 8 * (l_exp - 32);

    // Calculate bit_length of lower 256 bits
    uint32_t bit_length     = 0;
    uint32_t bytes_to_check = l_exp > 64 ? 32 : l_exp - 32; // Only check up to 256 bits (32 bytes)
    for (int i = 0; i < bytes_to_check; i++) {
      if (b_exp.data[i] != 0) {
        uint8_t byte = b_exp.data[i];
        for (int j = 7; j >= 0; j--) {
          if ((byte >> j) & 1) {
            bit_length = i * 8 + j + 1;
            break;
          }
        }
        if (bit_length > 0) break;
      }
    }

    iteration_count = base_count + (bit_length > 0 ? bit_length - 1 : 0);
  }

  // Ensure iteration_count is at least 1
  uint32_t calculate_iteration_count = iteration_count > 0 ? iteration_count : 1;

  // Calculate final gas cost
  uint64_t dynamic_gas = multiplication_complexity * calculate_iteration_count / 3;
  if (dynamic_gas < 200) dynamic_gas = 200;

  return dynamic_gas;
}

static pre_result_t pre_modexp(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  uint32_t l_base = (uint32_t) bytes_as_be(bytes_slice(input, 24, 8));
  uint32_t l_exp  = (uint32_t) bytes_as_be(bytes_slice(input, 32 + 24, 8));
  uint32_t l_mod  = (uint32_t) bytes_as_be(bytes_slice(input, 64 + 24, 8));

  if (input.len < 96 + l_base + l_exp + l_mod) return PRE_INVALID_INPUT;

  bytes_t b_base = bytes(input.data + 96, l_base);
  bytes_t b_exp  = bytes(input.data + 96 + l_base, l_exp);
  bytes_t b_mod  = bytes(input.data + 96 + l_base + l_exp, l_mod);

  *gas_used = calculate_gas_for_modexp(l_base, l_exp, l_mod, b_exp);

  // Initialize intx variables
  intx_uint256_t base, exp, mod, result;
  intx_init(&result);

  // Convert input bytes to intx
  intx_from_bytes(&base, b_base);
  intx_from_bytes(&exp, b_exp);
  intx_from_bytes(&mod, b_mod);

  // Perform modular exponentiation
  intx_modexp(&result, &base, &exp, &mod);

  // Find first non-zero byte
  int start_idx = 0;
  while (start_idx < 32 && result.bytes[start_idx] == 0) {
    start_idx++;
  }

  // If all zeros, output a single zero byte
  size_t result_len = (start_idx == 32) ? 1 : 32 - start_idx;

  // Set output buffer
  output->data.len = 0;
  buffer_grow(output, result_len);
  buffer_append(output, bytes(result.bytes + ((start_idx == 32) ? 31 : start_idx), result_len));

  return PRE_SUCCESS;
}
#endif

const precompile_func_t precompile_fn[] = {
    pre_ecrecover,
    pre_sha256,
#ifdef PRECOMPILED_RIPEMD160
    pre_ripemd160,
#else
    NULL,
#endif
    pre_identity,
#ifdef INTX
    pre_modexp,
    pre_ec_add,
    pre_ec_mul,
#else
    NULL,
    NULL,
    NULL,
#endif
};

pre_result_t eth_execute_precompile(const uint8_t* address, const bytes_t input, buffer_t* output, uint64_t* gas_used) {
  if (!bytes_all_zero(bytes(address, 19)) || address[19] >= PRECOMPILE_FN_COUNT) return PRE_INVALID_ADDRESS;
  precompile_func_t fn = precompile_fn[address[19] - 1];
  if (fn == NULL) return PRE_NOT_SUPPORTED;
  return fn(input, output, gas_used);
}
