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

#ifndef INTX_C_API_H
#define INTX_C_API_H

#ifdef __cplusplus
extern "C" {
#endif
#ifndef BYTES_T_DEFINED
#include <stdint.h>
typedef struct {
  uint32_t len;
  uint8_t* data;
} bytes_t;
#define BYTES_T_DEFINED
#endif /* Define a struct to hold uint256 values directly (no pointers) */
typedef struct {
  unsigned char bytes[32]; /* 256 bits = 32 bytes */
} intx_uint256_t;
typedef intx_uint256_t uint256_t;

/* Initialization functions */
void intx_init(intx_uint256_t* value);
void intx_init_value(intx_uint256_t* value, unsigned long long val);
int  intx_from_string(intx_uint256_t* value, const char* str, int base);

/* Conversion functions */
void intx_to_string(const intx_uint256_t* value, char* output, int output_len, int base);

/* Basic arithmetic operations */
void intx_add(intx_uint256_t* result, const intx_uint256_t* a, const intx_uint256_t* b);
void intx_sub(intx_uint256_t* result, const intx_uint256_t* a, const intx_uint256_t* b);
void intx_mul(intx_uint256_t* result, const intx_uint256_t* a, const intx_uint256_t* b);
void intx_div(intx_uint256_t* result, const intx_uint256_t* a, const intx_uint256_t* b);
void intx_mod(intx_uint256_t* result, const intx_uint256_t* a, const intx_uint256_t* b);

/* Bitwise operations */
void intx_and(intx_uint256_t* result, const intx_uint256_t* a, const intx_uint256_t* b);
void intx_or(intx_uint256_t* result, const intx_uint256_t* a, const intx_uint256_t* b);
void intx_xor(intx_uint256_t* result, const intx_uint256_t* a, const intx_uint256_t* b);
void intx_not(intx_uint256_t* result, const intx_uint256_t* a);
void intx_shl(intx_uint256_t* result, const intx_uint256_t* a, unsigned int shift);
void intx_shr(intx_uint256_t* result, const intx_uint256_t* a, unsigned int shift);

/* Comparison operations */
int intx_eq(const intx_uint256_t* a, const intx_uint256_t* b);
int intx_lt(const intx_uint256_t* a, const intx_uint256_t* b);
int intx_gt(const intx_uint256_t* a, const intx_uint256_t* b);
int intx_lte(const intx_uint256_t* a, const intx_uint256_t* b);
int intx_gte(const intx_uint256_t* a, const intx_uint256_t* b);

/* Other useful operations */
void intx_exp(intx_uint256_t* result, const intx_uint256_t* base, const intx_uint256_t* exponent);
int  intx_is_zero(const intx_uint256_t* value);

/* New function declaration */
void intx_modexp(intx_uint256_t* result, const intx_uint256_t* base, const intx_uint256_t* exponent, const intx_uint256_t* modulus);

/* Add this declaration to intx_c_api.h */
void intx_from_bytes(intx_uint256_t* result, const bytes_t bytes);

#ifdef __cplusplus
}
#endif

#endif /* INTX_C_API_H */