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

#ifndef OP_PROVER_H
#define OP_PROVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "prover.h"
#include "ssz.h"

c4_status_t c4_op_proof_block(prover_ctx_t* ctx);
c4_status_t c4_op_proof_transaction(prover_ctx_t* ctx);
c4_status_t c4_op_proof_receipt(prover_ctx_t* ctx);
c4_status_t c4_op_proof_logs(prover_ctx_t* ctx);
c4_status_t c4_op_proof_call(prover_ctx_t* ctx);
c4_status_t c4_op_proof_blocknumber(prover_ctx_t* ctx);
c4_status_t c4_op_proof_account(prover_ctx_t* ctx);

c4_status_t c4_op_create_block_proof(prover_ctx_t* ctx, json_t block_number, ssz_builder_t* block_proof);

/**
 * Add the given preconf block proof builder as a `block_proof` union variant to the parent builder.
 *
 * Inspects the prover's `client_state`. If the client already has the same execution payload
 * cached (status `C4_STATE_SYNC_EXECUTION_PAYLOAD`, matching block reference), the union variant
 * `none` is added instead of the full `preconf` payload to save bandwidth.
 *
 * The cache decision is metadata-only and avoids any extra zstd decompression of the preconf
 * payload. Only specific hex block numbers / hashes can hit the cache; "latest"-style requests
 * always emit the full preconf.
 *
 * The `preconf_proof` builder is consumed (its buffers are freed) regardless of which variant is used.
 *
 * @param ctx prover context (uses ctx->client_state)
 * @param requested the user-supplied JSON block reference (hex number, block hash, or tag)
 * @param parent the parent builder (e.g. the block_proof / receipt_proof / call_proof builder)
 * @param name name of the union field in the parent (typically "block_proof")
 * @param preconf_proof builder previously produced by `c4_op_create_block_proof`
 */
void c4_op_add_block_proof(prover_ctx_t* ctx, json_t requested, ssz_builder_t* parent, const char* name, ssz_builder_t* preconf_proof);
#ifdef __cplusplus
}
#endif

#endif