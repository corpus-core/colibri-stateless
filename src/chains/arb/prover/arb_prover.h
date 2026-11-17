/*
 * Copyright (c) 2025,2026 corpus.core GmbH
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

#ifndef arb_prover_h__
#define arb_prover_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "prover.h"

/**
 * Builds an Arbitrum proof by delegating to the Ethereum prover.
 *
 * Until the sequencer-feed proof exists, every method uses the same builders
 * as Ethereum (`c4_eth_prover_dispatch`). The chain-type gate is here because
 * `eth_prover_execute` rejects anything that is not `C4_CHAIN_TYPE_ETHEREUM`.
 *
 * @param ctx prover context with `method`, `params` and `chain_id` already set
 * @return `false` when `ctx->chain_id` is not Arbitrum, otherwise `true`
 *         (the builder status is stored in `ctx`, not returned here)
 */
bool arb_prover_execute(prover_ctx_t* ctx);

#ifdef __cplusplus
}
#endif

#endif
