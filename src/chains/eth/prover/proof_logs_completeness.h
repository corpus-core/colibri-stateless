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

#ifndef C4_PROOF_LOGS_COMPLETENESS_H
#define C4_PROOF_LOGS_COMPLETENESS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "prover.h"
#include <stdint.h>

/** Default maximum number of blocks a single completeness proof may cover. */
#define C4_LOGS_COMPLETENESS_DEFAULT_MAX_BLOCKS 1000

/**
 * Sets the maximum number of blocks a single `eth_getLogs` completeness proof may cover.
 *
 * The prover rejects requests whose `[fromBlock, toBlock]` range exceeds this limit.
 * A value of `0` restores the built-in default (`C4_LOGS_COMPLETENESS_DEFAULT_MAX_BLOCKS`).
 *
 * @param max_blocks the new limit, or `0` to reset to the default.
 */
void c4_eth_set_logs_completeness_max_blocks(uint32_t max_blocks);

/**
 * Returns the currently configured maximum number of blocks for a completeness proof.
 *
 * @return the current limit (always non-zero).
 */
uint32_t c4_eth_get_logs_completeness_max_blocks(void);

/**
 * Generates an `eth_getLogs` completeness proof over the requested block range.
 *
 * Resolves `[fromBlock, toBlock]`, builds a parent_root header chain anchored to a
 * single signed beacon header, and serializes for every block either a bloom-negative
 * proof or the full set of receipts. The result is stored in `ctx->proof`.
 *
 * @param ctx the prover context (method must be `eth_getLogs`).
 * @return `C4_SUCCESS` when the proof is ready, `C4_PENDING` while fetching data, `C4_ERROR` on failure.
 */
c4_status_t c4_proof_logs_completeness(prover_ctx_t* ctx);

#ifdef __cplusplus
}
#endif

#endif
