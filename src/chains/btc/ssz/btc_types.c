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

#include "btc_types.h"

// Index 0 is frozen as NONE. Proof and data variants are appended in later steps.
static const ssz_def_t BTC_REQUEST_DATA_UNION[] = {
    SSZ_NONE,
};

static const ssz_def_t BTC_REQUEST_PROOF_UNION[] = {
    SSZ_NONE,
};

static const ssz_def_t BTC_REQUEST_SYNCDATA_UNION[] = {
    SSZ_NONE,
};

static const ssz_def_t BTC_REQUEST[] = {
    SSZ_BYTE_VECTOR("version", 4),
    SSZ_UNION("data", BTC_REQUEST_DATA_UNION),
    SSZ_UNION("proof", BTC_REQUEST_PROOF_UNION),
    SSZ_UNION("sync_data", BTC_REQUEST_SYNCDATA_UNION),
};

static const ssz_def_t BTC_REQUEST_CONTAINER = SSZ_CONTAINER("C4Request", BTC_REQUEST);

const ssz_def_t* btc_ssz_verification_type(btc_ssz_type_t type) {
  switch (type) {
    case BTC_SSZ_VERIFY_REQUEST:
      return &BTC_REQUEST_CONTAINER;
    default:
      return NULL;
  }
}
