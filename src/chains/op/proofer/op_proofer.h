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

#ifndef OP_PROOFER_H
#define OP_PROOFER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "proofer.h"
#include "ssz.h"

c4_status_t c4_op_proof_block(proofer_ctx_t* ctx);
c4_status_t c4_op_proof_transaction(proofer_ctx_t* ctx);
c4_status_t c4_op_proof_receipt(proofer_ctx_t* ctx);
c4_status_t c4_op_proof_logs(proofer_ctx_t* ctx);
c4_status_t c4_op_proof_call(proofer_ctx_t* ctx);

c4_status_t c4_op_create_block_proof(proofer_ctx_t* ctx, json_t block_number, ssz_builder_t* block_proof);
#ifdef __cplusplus
}
#endif

#endif