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

#define GINDEX_RECEIPT_ROOT 803
#define GINDEX_BLOCKUMBER   806
#define GINDEX_BLOCHASH     812
#define GINDEX_TXINDEX_G    1704984576L // gindex of the first tx

/*
   SSZ_BYTES32("blockHash"),                                                  // the blockHash of the execution block containing the transaction
    SSZ_UINT64("blockNumber"),                                                 // the number of the execution block containing the transaction
    SSZ_BYTES32("hash"),                                                       // the blockHash of the execution block containing the transaction
    SSZ_UINT32("transactionIndex"),                                            // the index of the transaction in the block
    SSZ_UINT8("type"),                                                         // the type of the transaction
    SSZ_UINT64("nonce"),                                                       // the nonce of the transaction
    SSZ_BYTES("input", 1073741824),                                            // the raw transaction payload
    SSZ_BYTES32("r"),                                                          // the r value of the transaction
    SSZ_BYTES32("s"),                                                          // the s value of the transaction
    SSZ_UINT32("chainId"),                                                     // the s value of the transaction
    SSZ_UINT8("v"),                                                            // the v value of the transaction
    SSZ_UINT64("gas"),                                                         // the gas limnit
    SSZ_ADDRESS("from"),                                                       // the sender of the transaction
    SSZ_BYTES("to", 20),                                                       // the target of the transaction
    SSZ_UINT256("value"),                                                      // the value of the transaction
    SSZ_UINT64("gasPrice"),                                                    // the gas price of the transaction
    SSZ_UINT64("maxFeePerGas"),                                                // the maxFeePerGas of the transaction
    SSZ_UINT64("maxPriorityFeePerGas"),                                        // the maxPriorityFeePerGas of the transaction
    SSZ_LIST("accessList", ETH_ACCESS_LIST_DATA_CONTAINER, 256),               // the access list of the transaction
    SSZ_LIST("authorizationList", ETH_AUTHORIZATION_LIST_DATA_CONTAINER, 256), // the access list of the transaction
    SSZ_LIST("blobVersionedHashes", ssz_bytes32, 16),                          // the blobVersionedHashes of the transaction
    SSZ_UINT8("yParity"),                                                      // the yParity of the transaction
    SSZ_BYTES32("sourceHash"),                                                 // unique identifier for deposit origin (OP Stack only)
    SSZ_UINT256("mint"),                                                       // ETH value to mint on L2 (OP Stack only) - rendered as uint
    SSZ_BOOLEAN("isSystemTx"),                                                 // system transaction flag as bytes (OP Stack only) - rendered as uint
    SSZ_UINT8("depositReceiptVersion")
*/

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

// tools for eth tx and receipt handling

bool    c4_tx_create_from_address(verify_ctx_t* ctx, bytes_t raw_tx, uint8_t* address); // using ecrecover
bool    c4_tx_verify_tx_hash(verify_ctx_t* ctx, bytes_t raw);
bool    c4_tx_verify_receipt_data(verify_ctx_t* ctx, ssz_ob_t receipt_data, bytes32_t block_hash, uint64_t block_number, uint32_t tx_index, bytes_t tx_raw, bytes_t receipt_raw);
bool    c4_tx_verify_receipt_proof(verify_ctx_t* ctx, ssz_ob_t receipt_proof, uint32_t tx_index, bytes32_t receipt_root, bytes_t* receipt_raw);
bool    c4_tx_verify_log_data(verify_ctx_t* ctx, ssz_ob_t log, bytes32_t block_hash, uint64_t block_number, uint32_t tx_index, bytes_t tx_raw, bytes_t receipt_raw);
bytes_t c4_eth_create_tx_path(uint32_t tx_index, buffer_t* buf);
bool    c4_write_tx_data_from_raw(verify_ctx_t* ctx, ssz_builder_t* buffer, bytes_t raw_tx,
                                  bytes32_t tx_hash, bytes32_t block_hash, uint64_t block_number, uint32_t transaction_index, uint64_t base_fee);

#ifdef __cplusplus
}
#endif

#endif
