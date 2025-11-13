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
// Helper type definition for byte arrays with large maximum size (1GB)
static const ssz_def_t ssz_bytes_1024 = SSZ_BYTES("Bytes", 1073741824);
// : OP-Stack
//

// Union type for specifying which block to prove.
// Used to select between proving the latest block, a specific block hash, or a specific block number.
static const ssz_def_t OP_STATE_BLOCK_UNION[] = {
    SSZ_NONE,                 // no block-proof for latest
    SSZ_BYTES32("blockHash"), // proof for the right block hash
    SSZ_UINT64("blockNumber") // proof for the right block number
};

// Union type for preconfirmation payload format.
// Preconfirmations can be stored either compressed (ZSTD) or uncompressed.
static const ssz_def_t OP_PRECONF_PAYLOAD_UNION[] = {
    SSZ_BYTES("compressed_zstd", 1073741824),  // ZSTD-compressed execution payload (domain + payload)
    SSZ_BYTES("uncompressed", 1073741824),     // uncompressed execution payload (domain + payload)
};

// Preconfirmation structure containing a sequencer-signed execution payload.
// Preconfirmations are OP-Stack sequencer commitments that provide early block availability.
static const ssz_def_t OP_PRECONF[] = {
    SSZ_UNION("payload", OP_PRECONF_PAYLOAD_UNION), // the execution payload (compressed or uncompressed)
    SSZ_BYTE_VECTOR("signature", 65),               // the sequencer signature (65 bytes: r, s, v)
};

// Union type for block proof methods in OP-Stack.
// Currently supports preconfirmation-based proofs, which use sequencer-signed execution payloads.
static const ssz_def_t OP_BLOCKPROOF_UNION[] = {
    SSZ_CONTAINER("preconf", OP_PRECONF), // preconfirmation proof (sequencer-signed execution payload)
};

// :: Receipt Proof
//
// Represents the proof for a transaction receipt.
//

// The main proof data for a receipt.
static const ssz_def_t OP_RECEIPT_PROOF[] = {
    SSZ_BYTES("transaction", 1073741824),          // the raw transaction payload
    SSZ_UINT32("transactionIndex"),                // the index of the transaction in the block
    SSZ_LIST("receipt_proof", ssz_bytes_1024, 64), // the Merkle Patricia Proof of the transaction receipt ending in the receipt root
    SSZ_LIST("tx_proof", ssz_bytes_1024, 64),      // the Merkle Patricia Proof of the transaction (may be empty for preconf blocks since the full execution payload is available)
    SSZ_UNION("block_proof", OP_BLOCKPROOF_UNION), // proof for the right block hash
};

// :: Logs Proof
//
// `eth_getLogs` returns a list of log entries from different transaction receipts. So the proof must contain the receipt proofs for each transaction.
//
// 1. The **transaction** is used to create its SSZ Hash Tree Root.
// 2. The **SSZ Merkle Proof** from the Transactions of the ExecutionPayload to the BlockBodyRoot. (Total Depth: 29)
// 3. The **BeaconBlockHeader** is passed because we also need the slot in order to find out which period and which sync committee is used.
// 4. The **Signature of the SyncCommittee** (taken from the following block) is used to verify the SignData where the blockhash is part of the message and the Domain is calculated from the fork and the Genesis Validator Root.
// Note: OP-Stack uses the same consensus layer as Ethereum, so these verification steps apply here as well.

// Represents one single transaction receipt with the required transaction and receipt-proof.
// The proof contains the raw receipt as part of its last leaf.
static const ssz_def_t OP_LOGS_TX[] = {
    SSZ_UINT32("transactionIndex"),           // the index of the transaction in the block
    SSZ_LIST("proof", ssz_bytes_1024, 256),   // the Merkle Patricia Proof of the transaction receipt ending in the receipt root
    SSZ_LIST("tx_proof", ssz_bytes_1024, 64), // the Merkle Patricia Proof of the transaction (empty for preconf blocks since full execution payload is available)
};
// Container type for a single transaction with its receipt and transaction proofs
static const ssz_def_t OP_LOGS_TX_CONTAINER = SSZ_CONTAINER("LogsTx", OP_LOGS_TX);

// A single block with its proof containing all the receipts or txs required to prove the logs.
static const ssz_def_t OP_LOGS_BLOCK[] = {
    SSZ_UNION("block_proof", OP_BLOCKPROOF_UNION), // proof for the block (preconfirmation)
    SSZ_LIST("txs", OP_LOGS_TX_CONTAINER, 256)   // the transactions of the block with their proofs
};
        
// Container type for a block containing multiple transaction log proofs
static const ssz_def_t OP_LOGS_BLOCK_CONTAINER = SSZ_CONTAINER("LogsBlock", OP_LOGS_BLOCK);

// :: Transaction Proof
//
// Represents the proof for a single transaction, verifying its inclusion in a block.
// For preconfirmation blocks, the transaction proof may be empty since the full execution payload is available.
//

// The main proof data for a single transaction.
static const ssz_def_t OP_TRANSACTION_PROOF[] = {
    SSZ_LIST("tx_proof", ssz_bytes_1024, 64),     // the Merkle Patricia Proof of the transaction (empty for preconf blocks since full execution payload is available)
    SSZ_UINT32("transactionIndex"),               // the index of the transaction in the block
    SSZ_UNION("block_proof", OP_BLOCKPROOF_UNION) // proof for the right block hash
};

// :: Account Proof
//
// Represents the account and storage values, including the Merkle proof, of the specified account.
//

// Represents the storage proof of a key. The value can be taken from the last entry, which is the leaf of the proof.
static const ssz_def_t OP_STORAGE_PROOF[] = {
    SSZ_BYTES32("key"),                      // the key to be proven
    SSZ_LIST("proof", ssz_bytes_1024, 1024), // Patricia merkle proof
};
// Container type for storage proof data
static const ssz_def_t OP_STORAGE_PROOF_CONTAINER = SSZ_CONTAINER("StorageProof", OP_STORAGE_PROOF);

// The main proof data for an account.
static const ssz_def_t OP_ACCOUNT_PROOF[] = {
    SSZ_LIST("accountProof", ssz_bytes_1024, 256),             // Patricia Merkle proof
    SSZ_ADDRESS("address"),                                    // the address of the account
    SSZ_LIST("storageProof", OP_STORAGE_PROOF_CONTAINER, 256), // the storage proofs of the selected storage keys
    SSZ_UNION("block_proof", OP_BLOCKPROOF_UNION)              // proof for the block header
};

// Union type for contract code.
// Code can be omitted (if not needed) or included as raw bytes.
static const ssz_def_t OP_CODE_UNION[] = {
    SSZ_BOOLEAN("code_used"),   // flag indicating whether code is provided (false = no code, true = code follows)
    SSZ_BYTES("code", 4194304), // the contract bytecode (max 4MB)
};

// :: Call Proof
//
// `eth_call` returns the result of the call. In order to prove that the result is correct, we need
// to prove every single storage value and account.
//

// A proof for a single account.
static const ssz_def_t OP_CALL_ACCOUNT[] = {
    SSZ_LIST("accountProof", ssz_bytes_1024, 256),              // Patricia Merkle proof
    SSZ_ADDRESS("address"),                                     // the address of the account
    SSZ_UNION("code", OP_CODE_UNION),                           // the code of the contract
    SSZ_LIST("storageProof", OP_STORAGE_PROOF_CONTAINER, 4096), // the storage proofs for requested storage keys
};
// Container type for account data in call proofs
static const ssz_def_t OP_CALL_ACCOUNT_CONTAINER = SSZ_CONTAINER("EthCallAccount", OP_CALL_ACCOUNT);

// The main proof data for a call.
static const ssz_def_t OP_CALL_PROOF[] = {
    SSZ_LIST("accounts", OP_CALL_ACCOUNT_CONTAINER, 256), // used accounts
    SSZ_UNION("block_proof", OP_BLOCKPROOF_UNION)         // proof for the block header
};

// :: Block Proof
//
// The Block Proof verifies that a block is valid and part of the OP-Stack chain.
// In OP-Stack, blocks are verified using preconfirmations (sequencer-signed execution payloads)
// rather than consensus layer signatures like in Ethereum mainnet.
//

// The block proof structure used by other proof types (receipt, transaction, account, etc.)
// to validate that the block is part of the OP-Stack chain. Contains the preconfirmation proof
// which verifies the block's validity through the sequencer signature.
static const ssz_def_t OP_BLOCK_PROOF[] = {
    SSZ_UNION("block_proof", OP_BLOCKPROOF_UNION) // proof for the block (preconfirmation-based)
};
