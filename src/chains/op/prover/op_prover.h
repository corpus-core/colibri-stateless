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

#include "beacon.h"
#include "eth_tools.h"
#include "prover.h"

/**
 * Fills `eth_block_t` from the OP preconf endpoint (or the header cache when
 * the verifier already advertised this block). Registered as the GET_BLOCK hook.
 *
 * @param ctx prover context
 * @param block JSON block identifier
 * @param out output block (`proof_type` NONE or SEQUENCER, EL fields, optional sequencer data)
 * @param with_body if true, `el_body` must contain transactions and withdrawals
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
c4_status_t op_get_el_block(prover_ctx_t* ctx, json_t block, eth_block_t* out, bool with_body);

/**
 * Appends the `sequencerProof` variant of `ETH_BLOCK_PROOF_UNION`.
 *
 * @param ctx prover context
 * @param builder parent proof builder (`block` union field)
 * @param block_data block produced by `op_get_el_block`
 * @param historic unused (no CL proof)
 * @return true if the variant was written
 */
bool op_add_sequencer_proof(prover_ctx_t* ctx, ssz_builder_t* builder, eth_block_t* block_data, blockroot_proof_t* historic);

/**
 * Registers OP prover block-proof hooks. Idempotent.
 */
void op_register_block_proof_prover(void);

#ifdef __cplusplus
}
#endif

#endif
