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

#ifndef ETH_HYBRID_PROOF_TYPES_H
#define ETH_HYBRID_PROOF_TYPES_H

#include "ssz.h"

extern const ssz_def_t ETH_HYBRID_ACCOUNT_PROOF[];
extern const ssz_def_t ETH_HYBRID_CALL_PROOF[];
extern const ssz_def_t ETH_HYBRID_TRANSACTION_PROOF[];
extern const ssz_def_t ETH_HYBRID_RECEIPT_PROOF[];
extern const ssz_def_t ETH_HYBRID_LOGS_BLOCK[];
extern const ssz_def_t ETH_HYBRID_LOGS_BLOCK_CONTAINER;
extern const ssz_def_t ETH_HYBRID_BLOCK_RECEIPTS_PROOF[];
extern const ssz_def_t ETH_HYBRID_BLOCK_PROOF[];
extern const ssz_def_t ETH_HYBRID_BLOCK_HEADER_PROOF[];

#endif /* ETH_HYBRID_PROOF_TYPES_H */
