/*
 * Copyright (c) 2025,2026 corpus.core
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

#ifndef pap_tx_cache_types_h__
#define pap_tx_cache_types_h__

#include "ssz.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * SSZ type definitions for the PAP transaction cache snapshot.
 *
 * The snapshot is a list of blocks, each containing a block number and
 * the keccak-256 hashes of all transactions in that block. This format
 * is used both for the server HTTP response and the client-side storage.
 *
 * ```
 * TxCacheBlock:    Container { block_number: Uint64, tx_hashes: List[Bytes32, 4096] }
 * TxCacheSnapshot: List[TxCacheBlock, 10000]
 * ```
 */

static const ssz_def_t PAP_TX_CACHE_BLOCK_FIELDS[] = {
    SSZ_UINT64("block_number"),
    SSZ_LIST("tx_hashes", ssz_bytes32, 4096),
};

static const ssz_def_t PAP_TX_CACHE_BLOCK    = SSZ_CONTAINER("block", PAP_TX_CACHE_BLOCK_FIELDS);
static const ssz_def_t PAP_TX_CACHE_SNAPSHOT = SSZ_LIST("blocks", PAP_TX_CACHE_BLOCK, 10000);

/**
 * SSZ type definitions for the PAP pending transaction list.
 *
 * Each entry records a transaction hash and the Unix timestamp when it
 * was submitted. Entries older than `PAP_PENDING_TX_TTL_S` are pruned
 * on every load/save cycle. Stored per chain under key
 * `"tx_pending_<chain_id>"`.
 *
 * ```
 * PendingTxEntry: Container { tx_hash: Bytes32, timestamp: Uint64 }
 * PendingTxList:  List[PendingTxEntry, 256]
 * ```
 */

static const ssz_def_t PAP_PENDING_TX_ENTRY_FIELDS[] = {
    SSZ_BYTES32("tx_hash"),
    SSZ_UINT64("timestamp"),
};

static const ssz_def_t PAP_PENDING_TX_ENTRY = SSZ_CONTAINER("pending_tx", PAP_PENDING_TX_ENTRY_FIELDS);
static const ssz_def_t PAP_PENDING_TX_LIST  = SSZ_LIST("pending_txs", PAP_PENDING_TX_ENTRY, 256);

#ifdef __cplusplus
}
#endif

#endif /* pap_tx_cache_types_h__ */
