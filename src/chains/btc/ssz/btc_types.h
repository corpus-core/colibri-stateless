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

#ifndef btc_types_h__
#define btc_types_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "ssz.h"

/**
 * Selects a Bitcoin SSZ definition.
 *
 * `BTC_SSZ_VERIFY_REQUEST` is the `C4Request` container (`version`, `data`,
 * `proof`, `sync_data`). Union index 0 of each union is `NONE`. `proof` carries
 * the block or transaction witness. `sync_data` index 1 is `BtcEpochPath`: up to
 * 40 steps, each with a height, an 80-byte header, and up to 64 trailing
 * finality headers. It brings the cached epoch targets forward before the proof
 * is checked. Further variants are appended in `btc_types.c`; existing indices
 * stay frozen.
 */
typedef enum {
  BTC_SSZ_VERIFY_REQUEST = 1,
} btc_ssz_type_t;

/**
 * Returns the SSZ definition for a Bitcoin verification type.
 *
 * @param type verification type to resolve
 * @return the matching definition, or NULL when `type` is unknown
 */
const ssz_def_t* btc_ssz_verification_type(btc_ssz_type_t type);

#ifdef __cplusplus
}
#endif

#endif
