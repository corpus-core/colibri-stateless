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

/**
 * Creates an account proof for account-related RPC methods (for example `eth_getBalance`,
 * `eth_getStorageAt`, `eth_getCode`, `eth_getProof`, `eth_getTransactionCount`).
 *
 * @param ctx prover context with `method` and `params` already set
 * @return `C4_PENDING` while external data is needed, `C4_SUCCESS` when `ctx->proof` is ready, or `C4_ERROR` with `ctx->state.error`
 */
c4_status_t c4_proof_account(prover_ctx_t* ctx);

/**
 * Creates a transaction proof for transaction RPC methods (for example `eth_getTransactionByHash`
 * and block-index variants).
 *
 * @param ctx prover context with `method` and `params` already set
 * @return `C4_PENDING`, `C4_SUCCESS`, or `C4_ERROR`
 */
c4_status_t c4_proof_transaction(prover_ctx_t* ctx);

/**
 * Creates a receipt proof for `eth_getTransactionReceipt`.
 *
 * @param ctx prover context with `method` and `params` already set
 * @return `C4_PENDING`, `C4_SUCCESS`, or `C4_ERROR`
 */
c4_status_t c4_proof_receipt(prover_ctx_t* ctx);

/**
 * Creates a logs proof for `eth_getLogs` and related log verification methods.
 *
 * @param ctx prover context with `method` and `params` already set
 * @return `C4_PENDING`, `C4_SUCCESS`, or `C4_ERROR`
 */
c4_status_t c4_proof_logs(prover_ctx_t* ctx);

/**
 * Creates a call proof for `eth_call`, `eth_estimateGas`, and `colibri_simulateTransaction`.
 *
 * @param ctx prover context with `method` and `params` already set
 * @return `C4_PENDING`, `C4_SUCCESS`, or `C4_ERROR`
 */
c4_status_t c4_proof_call(prover_ctx_t* ctx);

/**
 * Creates sync-committee sync data for proofs that require period updates or ZK sync sections.
 *
 * @param ctx prover context with `method` and `params` already set
 * @return `C4_PENDING`, `C4_SUCCESS`, or `C4_ERROR`
 */
c4_status_t c4_proof_sync(prover_ctx_t* ctx);

/**
 * Creates a block proof for block RPC methods (for example `eth_getBlockByNumber`, `eth_getBlockByHash`,
 * `eth_getBlockHeader`). For header-only methods the block body union is `NONE`.
 *
 * @param ctx prover context with `method` and `params` already set
 * @return `C4_PENDING`, `C4_SUCCESS`, or `C4_ERROR`
 */
c4_status_t c4_proof_block(prover_ctx_t* ctx);

/**
 * Creates a block receipts proof for `eth_getBlockReceipts`.
 *
 * @param ctx prover context with `method` and `params` already set
 * @return `C4_PENDING`, `C4_SUCCESS`, or `C4_ERROR`
 */
c4_status_t c4_proof_block_receipts(prover_ctx_t* ctx);

/**
 * Clears ETH in-process prover caches (header tags, tx-index cache).
 *
 * Registered via CMake `RESET_CACHES`. Persistent storage is left untouched.
 */
void c4_eth_reset_prover_caches(void);
#ifdef __cplusplus
}
#endif

#endif
