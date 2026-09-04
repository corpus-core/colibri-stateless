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

#ifndef ETH_TX_H
#define ETH_TX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "verify.h"

#define TX_BLOCK_HASH               2
#define TX_BLOCK_NUMBER             4
#define TX_HASH                     8
#define TX_TRANSACTION_INDEX        16
#define TX_TYPE                     32
#define TX_NONCE                    64
#define TX_INPUT                    128
#define TX_R                        256
#define TX_S                        512
#define TX_CHAIN_ID                 1024
#define TX_V                        2048
#define TX_GAS                      4096
#define TX_FROM                     8192
#define TX_TO                       16384
#define TX_VALUE                    32768
#define TX_GAS_PRICE                65536
#define TX_MAX_FEE_PER_GAS          131072
#define TX_MAX_PRIORITY_FEE_PER_GAS 262144
#define TX_ACCESS_LIST              524288
#define TX_AUTHORIZATION_LIST       1048576
#define TX_BLOB_VERSIONED_HASHES    2097152
#define TX_Y_PARITY                 4194304
#define TX_SOURCE_HASH              8388608
#define TX_MINT                     16777216
#define TX_IS_SYSTEM_TX             33554432
#define TX_DEPOSIT_RECEIPT_VERSION  67108864
#define TX_MAX_FEE_PER_BLOB_GAS     134217728  // bit 27 — set only for type-3 (blob) transactions
#define TX_BLOCK_TIMESTAMP          268435456  // bit 28 — timestamp of the containing block

// tools for eth tx and receipt handling

bool    c4_tx_create_from_address(verify_ctx_t* ctx, bytes_t raw_tx, uint8_t* address); // using ecrecover
bool    c4_tx_verify_tx_hash(verify_ctx_t* ctx, bytes_t raw);
bool    c4_tx_verify_receipt_data(verify_ctx_t* ctx, ssz_ob_t receipt_data, bytes32_t block_hash, uint64_t block_number, uint32_t tx_index, bytes_t tx_raw, bytes_t receipt_raw);
bool    c4_verify_mpt_proof(verify_ctx_t* ctx, ssz_ob_t receipt_proof, uint32_t tx_index, bytes32_t receipt_root, bytes_t* receipt_raw);
bool    c4_tx_verify_log_data(verify_ctx_t* ctx, ssz_ob_t log, bytes32_t block_hash, uint64_t block_number, uint32_t tx_index, bytes_t tx_raw, bytes_t receipt_raw);
bytes_t c4_eth_create_tx_path(uint32_t tx_index, buffer_t* buf);
bool    c4_write_tx_data_from_raw(verify_ctx_t* ctx, ssz_builder_t* buffer, bytes_t raw_tx,
                                  bytes32_t tx_hash, bytes32_t block_hash, uint64_t block_number, uint32_t transaction_index,
                                  uint64_t base_fee, uint64_t block_timestamp);
/**
 * Build one ETH_RECEIPT_DATA SSZ from RLP receipt and tx.
 *
 * `excess_blob_gas` (from the containing block's EL header) is used to price
 * blob gas for EIP-4844 (type-3) receipts. `block_timestamp` is stamped into
 * each log entry.  Both may be 0 for callers without a full header (the
 * corresponding fields will just be 0 in the output).
 *
 * @return Sets `*out_cumulative_gas` to receipt cumulativeGasUsed and
 *         `*out_log_index` to the next block-level log index.
 */
bool c4_write_receipt_data_from_raw(verify_ctx_t* ctx, ssz_builder_t* buffer, bytes_t tx_raw, bytes_t receipt_raw,
                                    bytes32_t block_hash, uint64_t block_number, uint32_t tx_index,
                                    uint64_t base_fee, uint64_t excess_blob_gas, uint64_t block_timestamp,
                                    uint64_t* out_cumulative_gas,
                                    uint32_t* out_log_index);

#ifdef __cplusplus
}
#endif

#endif
