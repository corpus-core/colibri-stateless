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

#ifndef OP_SSZ_TYPES_H
#define OP_SSZ_TYPES_H

#include "chains.h"
#include "ssz.h"

typedef enum {

  OP_SSZ_VERIFY_REQUEST            = 4,
  OP_SSZ_VERIFY_BLOCK_HASH_PROOF   = 5,
  OP_SSZ_VERIFY_ACCOUNT_PROOF      = 6,
  OP_SSZ_VERIFY_TRANSACTION_PROOF  = 7,
  OP_SSZ_VERIFY_RECEIPT_PROOF      = 8,
  OP_SSZ_VERIFY_LOGS_PROOF         = 9,
  OP_SSZ_VERIFY_STATE_PROOF        = 12,
  OP_SSZ_VERIFY_CALL_PROOF         = 13,
  OP_SSZ_VERIFY_SYNC_PROOF         = 14,
  OP_SSZ_VERIFY_BLOCK_PROOF        = 15,
  OP_SSZ_VERIFY_BLOCK_NUMBER_PROOF = 16,
  OP_SSZ_VERIFY_WITNESS_PROOF      = 17,
  OP_SSZ_VERIFY_PRECONF_PROOF      = 18,

} op_ssz_type_t;

//  c4 specific
const ssz_def_t* op_ssz_verification_type(op_ssz_type_t type);

#define ssz_builder_for_op_type(typename) \
  {.def = op_ssz_verification_type(typename), .dynamic = {0}, .fixed = {0}}

#endif
