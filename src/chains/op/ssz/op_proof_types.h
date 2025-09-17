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

#include "beacon_types.h"
#include "ssz.h"
static const ssz_def_t ssz_bytes_1024 = SSZ_BYTES("Bytes", 1073741824);
// : OP-Stack
//

// definition of an enum depending on the requested block
static const ssz_def_t ETH_STATE_BLOCK_UNION[] = {
    SSZ_NONE,                 // no block-proof for latest
    SSZ_BYTES32("blockHash"), // proof for the right blockhash
    SSZ_UINT64("blockNumber") // proof for the right blocknumber
};

static const ssz_def_t OP_PRECONF[] = {
    SSZ_BYTES("payload", 1073741824),
    SSZ_BYTE_VECTOR("signature", 65),
};

// definition of an enum depending on the requested block
static const ssz_def_t OP_BLOCKPROOF_UNION[] = {
    SSZ_CONTAINER("preconf", OP_PRECONF), // proof for the right blockhash
};

// :: Receipt Proof
//
// represents the proof for a transaction receipt
//

// the main proof data for a receipt.
static const ssz_def_t ETH_RECEIPT_PROOF[] = {
    SSZ_BYTES("transaction", 1073741824),          // the raw transaction payload
    SSZ_UINT32("transactionIndex"),                // the index of the transaction in the block
    SSZ_LIST("receipt_proof", ssz_bytes_1024, 64), // the Merklr Patricia Proof of the transaction receipt ending in the receipt root
    SSZ_LIST("tx_proof", ssz_bytes_1024, 64),      // the Merklr Patricia Proof of the transaction empty, in case of a preconf since we have the full execution payload
    SSZ_UNION("block_proof", OP_BLOCKPROOF_UNION), // proof for the right blockhash
};

// :: Logs Proof
//
// eth_getLogs returns a list of log entries from different transaction receipts. So the proof must contain the receipt proofs for each transaction.
//
// 1. The **transaction** is used to create its SSZ Hash Tree Root.
// 2. The **SSZ Merkle Proof** from the Transactions of the ExecutionPayload to the BlockBodyRoot. (Total Depth: 29)
// 3. **BeaconBlockHeader** is passed because also need the slot in order to find out which period and which sync committee is used.
// 4. **Signature of the SyncCommittee** (taken from the following block) is used to verify the SignData where the blockhash is part of the message and the Domain is calculated from the fork and the Genesis Validator Root.

// represents one single transaction receipt with the required transaction and receipt-proof.
// the proof contains the raw receipt as part of its last leaf.
static const ssz_def_t ETH_LOGS_TX[] = {
    SSZ_UINT32("transactionIndex"),           // the index of the transaction in the block
    SSZ_LIST("proof", ssz_bytes_1024, 256),   // the Merklr Patricia Proof of the transaction receipt ending in the receipt root
    SSZ_LIST("tx_proof", ssz_bytes_1024, 64), // the Merklr Patricia Proof of the transaction empty, in case of a preconf since we have the full execution payload
};
static const ssz_def_t ETH_LOGS_TX_CONTAINER = SSZ_CONTAINER("LogsTx", ETH_LOGS_TX);

// a single Block with its proof the all the receipts or txs required to proof for the logs.
static const ssz_def_t ETH_LOGS_BLOCK[] = {
    SSZ_UNION("block_proof", OP_BLOCKPROOF_UNION), // proof for the right blockhash
    SSZ_LIST("txs", ETH_LOGS_TX_CONTAINER, 256)};  // the transactions of the block

static const ssz_def_t ETH_LOGS_BLOCK_CONTAINER = SSZ_CONTAINER("LogsBlock", ETH_LOGS_BLOCK);

// :: Transaction Proof
//
// represents the account and storage values, including the Merkle proof, of the specified account.
//

// the main proof data for a single transaction.
static const ssz_def_t ETH_TRANSACTION_PROOF[] = {
    SSZ_LIST("tx_proof", ssz_bytes_1024, 64),     // empty in case of a preconf since we have the full execution payload
    SSZ_UINT32("transactionIndex"),               // the index of the transaction in the block
    SSZ_UNION("block_proof", OP_BLOCKPROOF_UNION) // proof for the right blockhash
};

// :: Account Proof
//
// represents the account and storage values, including the Merkle proof, of the specified account.
//
// represents the storage proof of a key. The value can be taken from the last entry, which is the leaf of the proof.
static const ssz_def_t ETH_STORAGE_PROOF[] = {
    SSZ_BYTES32("key"),                      // the key to be proven
    SSZ_LIST("proof", ssz_bytes_1024, 1024), // Patricia merkle proof
};

static const ssz_def_t ETH_STORAGE_PROOF_CONTAINER = SSZ_CONTAINER("StorageProof", ETH_STORAGE_PROOF);

// the main proof data for an account.
static const ssz_def_t ETH_ACCOUNT_PROOF[] = {
    SSZ_LIST("accountProof", ssz_bytes_1024, 256),              // Patricia merkle proof
    SSZ_ADDRESS("address"),                                     // the address of the account
    SSZ_LIST("storageProof", ETH_STORAGE_PROOF_CONTAINER, 256), // the storage proofs of the selected
    SSZ_UNION("block_proof", OP_BLOCKPROOF_UNION)               // proof for the blockheader
};

static const ssz_def_t ETH_CODE_UNION[] = {
    SSZ_BOOLEAN("code_used"),   // no code delivered
    SSZ_BYTES("code", 4194304), // the code of the contract
};

// :: Call Proof
//
// eth_call returns the result of the call. In order to proof that the result is correct, we need
// to proof every single storage value and account..
//

// a proof for a single account.
static const ssz_def_t ETH_CALL_ACCOUNT[] = {
    SSZ_LIST("accountProof", ssz_bytes_1024, 256),               // Patricia merkle proof
    SSZ_ADDRESS("address"),                                      // the address of the account
    SSZ_UNION("code", ETH_CODE_UNION),                           // the code of the contract
    SSZ_LIST("storageProof", ETH_STORAGE_PROOF_CONTAINER, 4096), // the storage proofs of the selected
};
static const ssz_def_t ETH_CALL_ACCOUNT_CONTAINER = SSZ_CONTAINER("EthCallAccount", ETH_CALL_ACCOUNT);

// the main proof data for a call.
static const ssz_def_t ETH_CALL_PROOF[] = {
    SSZ_LIST("accounts", ETH_CALL_ACCOUNT_CONTAINER, 256), // used accounts
    SSZ_UNION("block_proof", OP_BLOCKPROOF_UNION)          // proof for the blockheader
};

// :: Block Proof
//
// The Block Proof is a proof that the block is valid.
// It is used to verify the block of the execution layer.
//
//
//

// the stateRoot proof is used as part of different other types since it contains all relevant
// proofs to validate the stateRoot of the execution layer
static const ssz_def_t ETH_BLOCK_PROOF[] = {
    SSZ_UNION("block_proof", OP_BLOCKPROOF_UNION) // proof for the blockheader
};

// for `eth_blockNumber` we need to proof the blocknumber and the timestamp of the latest block.
static const ssz_def_t ETH_BLOCK_NUMBER_PROOF[] = {
    SSZ_UNION("block_proof", OP_BLOCKPROOF_UNION) // proof for the blockheader
};
