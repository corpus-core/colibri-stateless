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

#ifndef ETH_PROVER_H
#define ETH_PROVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "prover.h"

c4_status_t c4_proof_account(prover_ctx_t* ctx);     // creates an account proof
c4_status_t c4_proof_transaction(prover_ctx_t* ctx); // creates a transaction proof
c4_status_t c4_proof_receipt(prover_ctx_t* ctx);     // creates a receipt proof
c4_status_t c4_proof_logs(prover_ctx_t* ctx);        // creates a logs proof
c4_status_t c4_proof_call(prover_ctx_t* ctx);
c4_status_t c4_proof_sync(prover_ctx_t* ctx);
c4_status_t c4_proof_block(prover_ctx_t* ctx);
c4_status_t c4_proof_block_number(prover_ctx_t* ctx);
c4_status_t c4_proof_witness(prover_ctx_t* ctx);
#ifdef __cplusplus
}
#endif

#endif