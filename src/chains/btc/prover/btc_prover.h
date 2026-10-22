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

#ifndef btc_prover_h__
#define btc_prover_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "prover.h"

/**
 * Dispatches Bitcoin proof generation.
 *
 * Returns false for every non-Bitcoin chain so another prover module can run.
 * Mainnet `getblockhash` and raw `getblockheader` (`verbose` false or 0) build a
 * `BtcHeaderProof` from Bitcoin Core RPC. An empty `client_state` starts at the
 * compiled checkpoint. When that epoch is behind the proven block, the epoch path
 * is added to `sync_data`. Other Bitcoin methods and networks fail with an error.
 *
 * @param ctx prover context with `chain_id`, `method`, and `client_state` already set
 * @return true when this module owns the chain, false otherwise
 */
bool btc_prover_execute(prover_ctx_t* ctx);

#ifdef __cplusplus
}
#endif

#endif
