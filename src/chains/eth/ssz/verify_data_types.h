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

// These masks control which optional fields are included in simulation results.
// They are used with the _optmask field in SSZ containers to enable/disable specific fields.

// Log field masks for ETH_SIMULATION_LOG
#define ETH_SIMULATION_LOG_MASK_ANONYMOUS (1 << 1)                    // anonymous field (i=1)
#define ETH_SIMULATION_LOG_MASK_INPUTS    (1 << 2)                    // inputs field (i=2)
#define ETH_SIMULATION_LOG_MASK_NAME      (1 << 3)                    // name field (i=3)
#define ETH_SIMULATION_LOG_MASK_RAW       (1 << 4)                    // raw field (i=4)
#define ETH_SIMULATION_LOG_MASK_ALL       0xFFFF                      // all fields for testing
#define ETH_SIMULATION_LOG_MASK_MINIMAL   ETH_SIMULATION_LOG_MASK_RAW // only raw log data

#define ETH_SIMULATION_TRACE_MASK_DECODED_INPUT  (1 << 1)  // decodedInput field (i=1)
#define ETH_SIMULATION_TRACE_MASK_DECODED_OUTPUT (1 << 2)  // decodedOutput field (i=2)
#define ETH_SIMULATION_TRACE_MASK_FROM           (1 << 3)  // from field (i=3)
#define ETH_SIMULATION_TRACE_MASK_GAS            (1 << 4)  // gas field (i=4)
#define ETH_SIMULATION_TRACE_MASK_GAS_USED       (1 << 5)  // gasUsed field (i=5)
#define ETH_SIMULATION_TRACE_MASK_INPUT          (1 << 6)  // input field (i=6)
#define ETH_SIMULATION_TRACE_MASK_METHOD         (1 << 7)  // method field (i=7)
#define ETH_SIMULATION_TRACE_MASK_OUTPUT         (1 << 8)  // output field (i=8)
#define ETH_SIMULATION_TRACE_MASK_SUBTRACES      (1 << 9)  // subtraces field (i=9)
#define ETH_SIMULATION_TRACE_MASK_TO             (1 << 10) // to field (i=10)
#define ETH_SIMULATION_TRACE_MASK_TRACE_ADDRESS  (1 << 11) // traceAddress field (i=11)
#define ETH_SIMULATION_TRACE_MASK_TYPE           (1 << 12) // type field (i=12)
#define ETH_SIMULATION_TRACE_MASK_VALUE          (1 << 13) // value field (i=13)
#define ETH_SIMULATION_TRACE_MASK_ALL            0xFFFF    // all fields for testing
#define ETH_SIMULATION_TRACE_MASK_MINIMAL        0x0000    // no trace fields (empty)

#define ETH_SIMULATION_RESULT_MASK_BLOCK_NUMBER   (1 << 1) // blockNumber field (i=1)
#define ETH_SIMULATION_RESULT_MASK_CUMULATIVE_GAS (1 << 2) // cumulativeGasUsed field (i=2)
#define ETH_SIMULATION_RESULT_MASK_GAS_USED       (1 << 3) // gasUsed field (i=3)
#define ETH_SIMULATION_RESULT_MASK_LOGS           (1 << 4) // logs field (i=4)
#define ETH_SIMULATION_RESULT_MASK_LOGS_BLOOM     (1 << 5) // logsBloom field (i=5)
#define ETH_SIMULATION_RESULT_MASK_STATUS         (1 << 6) // status field (i=6)
#define ETH_SIMULATION_RESULT_MASK_TRACE          (1 << 7) // trace field (i=7)
#define ETH_SIMULATION_RESULT_MASK_TYPE           (1 << 8) // type field (i=8)
#define ETH_SIMULATION_RESULT_MASK_RETURN_VALUE   (1 << 9) // returnValue field (i=9)
#define ETH_SIMULATION_RESULT_MASK_ALL            0xFFFF   // all fields for testing
#define ETH_SIMULATION_RESULT_MASK_MINIMAL        (ETH_SIMULATION_RESULT_MASK_GAS_USED | \
                                            ETH_SIMULATION_RESULT_MASK_LOGS |            \
                                            ETH_SIMULATION_RESULT_MASK_STATUS |          \
                                            ETH_SIMULATION_RESULT_MASK_RETURN_VALUE) // essential fields only
#define ETH_SIMULATION_RESULT_MASK_CLEAN (ETH_SIMULATION_RESULT_MASK_GAS_USED | \
                                          ETH_SIMULATION_RESULT_MASK_LOGS |     \
                                          ETH_SIMULATION_RESULT_MASK_STATUS |   \
                                          ETH_SIMULATION_RESULT_MASK_RETURN_VALUE) // clean output: gasUsed, logs, status, returnValue (no logsBloom, no type)

// : Ethereum

// :: Transaction Proof

// Entry in the access list of a transaction or call.
static const ssz_def_t ETH_ACCESS_LIST_DATA[] = {
    SSZ_ADDRESS("address"),                    // the address in the access list
    SSZ_LIST("storageKeys", ssz_bytes32, 256), // the storage keys accessed at this address
};
// Container type for access list entries
static const ssz_def_t ETH_ACCESS_LIST_DATA_CONTAINER = SSZ_CONTAINER("AccessListData", ETH_ACCESS_LIST_DATA);

// Entry in the authorization list of a transaction or call.
static const ssz_def_t ETH_AUTHORIZATION_LIST_DATA[] = {
    SSZ_ADDRESS("address"), // the codebase to be used for the authorization
    SSZ_UINT32("chainId"),  // the chainId of the transaction
    SSZ_UINT64("nonce"),    // nonce of the transaction
    SSZ_BYTES32("r"),       // the r value of the transaction
    SSZ_BYTES32("s"),       // the s value of the transaction
    SSZ_UINT8("yParity")    // the yParity of the transaction
};
// Container type for authorization list entries (EIP-7702)
static const ssz_def_t ETH_AUTHORIZATION_LIST_DATA_CONTAINER = SSZ_CONTAINER("AuthorizationListData", ETH_AUTHORIZATION_LIST_DATA);

// The transaction data as result of an eth_getTransactionByHash rpc-call.
// Supports all transaction types including Optimism Deposited Transactions (0x7E).
static const ssz_def_t ETH_TX_DATA[] = {
    SSZ_OPT_MASK("_optmask", 4),                                               // the bitmask defining the fields to be included
    SSZ_BYTES32("blockHash"),                                                  // the blockHash of the execution block containing the transaction
    SSZ_UINT64("blockNumber"),                                                 // the number of the execution block containing the transaction
    SSZ_BYTES32("hash"),                                                       // the blockHash of the execution block containing the transaction
    SSZ_UINT32("transactionIndex"),                                            // the index of the transaction in the block
    SSZ_UINT8("type"),                                                         // the type of the transaction
    SSZ_UINT64("nonce"),                                                       // the nonce of the transaction
    SSZ_BYTES("input", 1073741824),                                            // the raw transaction payload
    SSZ_BYTES32("r"),                                                          // the r value of the transaction
    SSZ_BYTES32("s"),                                                          // the s value of the transaction signature
    SSZ_UINT32("chainId"),                                                     // the chain ID of the transaction
    SSZ_UINT8("v"),                                                            // the v value of the transaction signature
    SSZ_UINT64("gas"),                                                         // the gas limit
    SSZ_ADDRESS("from"),                                                       // the sender of the transaction
    SSZ_BYTES("to", 20),                                                       // the target of the transaction
    SSZ_UINT256("value"),                                                      // the value of the transaction
    SSZ_UINT64("gasPrice"),                                                    // the gas price of the transaction
    SSZ_UINT64("maxFeePerGas"),                                                // the maxFeePerGas of the transaction
    SSZ_UINT64("maxPriorityFeePerGas"),                                        // the maxPriorityFeePerGas of the transaction
    SSZ_LIST("accessList", ETH_ACCESS_LIST_DATA_CONTAINER, 256),               // the access list of the transaction
    SSZ_LIST("authorizationList", ETH_AUTHORIZATION_LIST_DATA_CONTAINER, 256), // the authorization list of the transaction (EIP-7702)
    SSZ_LIST("blobVersionedHashes", ssz_bytes32, 16),                          // the blobVersionedHashes of the transaction
    SSZ_UINT8("yParity"),                                                      // the yParity of the transaction
    SSZ_BYTES32("sourceHash"),                                                 // unique identifier for deposit origin (OP Stack only)
    SSZ_UINT256("mint"),                                                       // ETH value to mint on L2 (OP Stack only) - rendered as uint
    SSZ_BOOLEAN("isSystemTx"),                                                 // system transaction flag as bytes (OP Stack only) - rendered as uint
    SSZ_UINT8("depositReceiptVersion")                                         // deposit receipt version (OP Stack only) - rendered as uint
};

// :: Logs Proof

// A log entry in the receipt.
static const ssz_def_t ETH_RECEIPT_DATA_LOG[] = {
    SSZ_BYTES32("blockHash"),           // the blockHash of the execution block containing the transaction
    SSZ_UINT64("blockNumber"),          // the number of the execution block containing the transaction
    SSZ_BYTES32("transactionHash"),     // the hash of the transaction
    SSZ_UINT32("transactionIndex"),     // the index of the transaction in the block
    SSZ_ADDRESS("address"),             // the address of the log
    SSZ_UINT32("logIndex"),             // the index of the log in the transaction
    SSZ_BOOLEAN("removed"),             // whether the log was removed
    SSZ_LIST("topics", ssz_bytes32, 8), // the topics of the log
    SSZ_BYTES("data", 1073741824),      // the data of the log
};
// Container type for log entries in transaction receipts
static const ssz_def_t ETH_RECEIPT_DATA_LOG_CONTAINER = SSZ_CONTAINER("Log", ETH_RECEIPT_DATA_LOG);

// :: Receipt Proof

// The transaction receipt data as returned by eth_getTransactionReceipt.
static const ssz_def_t ETH_RECEIPT_DATA[] = {
    SSZ_OPT_MASK("_optmask", 4),
    SSZ_BYTES32("blockHash"),                              // the blockHash of the execution block containing the transaction
    SSZ_UINT64("blockNumber"),                             // the number of the execution block containing the transaction
    SSZ_BYTES32("transactionHash"),                        // the hash of the transaction
    SSZ_UINT32("transactionIndex"),                        // the index of the transaction in the block
    SSZ_UINT8("type"),                                     // the type of the transaction
    SSZ_ADDRESS("from"),                                   // the sender of the transaction
    SSZ_BYTES("to", 20),                                   // the target of the transaction
    SSZ_UINT64("cumulativeGasUsed"),                       // the cumulative gas used
    SSZ_UINT64("gasUsed"),                                 // the gas address of the created contract
    SSZ_LIST("logs", ETH_RECEIPT_DATA_LOG_CONTAINER, 256), // the logs of the transaction
    SSZ_BYTE_VECTOR("logsBloom", 256),                     // the bloom filter of the logs
    SSZ_UINT8("status"),                                   // the status of the transaction
    SSZ_UINT64("effectiveGasPrice"),                       // the effective gas price of the transaction
    SSZ_UINT64("depositNonce"),                            // the deposit nonce of the transaction
    SSZ_UINT32("depositReceiptVersion"),                   // the deposit receipt version of the transaction
}; // the gasPrice of the transaction

// Container type for transaction data
static const ssz_def_t ETH_TX_DATA_CONTAINER              = SSZ_CONTAINER("EthTransactionData", ETH_TX_DATA);
// Union type for block transactions: either as hashes or as full transaction data
static const ssz_def_t ETH_BLOCK_DATA_TRANSACTION_UNION[] = {
    SSZ_LIST("as_hashes", ssz_bytes32, 4096),         // the transactions hashes
    SSZ_LIST("as_data", ETH_TX_DATA_CONTAINER, 4096), // the transactions data
};

// :: Block Proof

// Display the block data, which is based on the execution payload
static const ssz_def_t ETH_BLOCK_DATA[] = {
    SSZ_OPT_MASK("_optmask", 4),
    SSZ_UINT64("number"),                                        // the blocknumber
    SSZ_BYTES32("hash"),                                         // the blockhash
    SSZ_UNION("transactions", ETH_BLOCK_DATA_TRANSACTION_UNION), // the transactions
    SSZ_BYTE_VECTOR("logsBloom", 256),                           // the logsBloom
    SSZ_BYTES32("receiptsRoot"),                                 // the receiptsRoot
    SSZ_BYTES("extraData", 32),                                  // the extraData
    SSZ_BYTES32("withdrawalsRoot"),                              // the withdrawalsRoot
    SSZ_UINT256("baseFeePerGas"),                                // the baseFeePerGas
    SSZ_BYTE_VECTOR("nonce", 8),                                 // the nonce
    SSZ_ADDRESS("miner"),                                        // the miner
    SSZ_LIST("withdrawals", DENEP_WITHDRAWAL_CONTAINER, 4096),   // the withdrawals
    SSZ_UINT64("excessBlobGas"),                                 // the excessBlobGas
    SSZ_UINT64("difficulty"),                                    // the difficulty
    SSZ_UINT64("gasLimit"),                                      // the gasLimit
    SSZ_UINT64("gasUsed"),                                       // the gasUsed
    SSZ_UINT64("timestamp"),                                     // the timestamp
    SSZ_BYTES32("mixHash"),                                      // the mixHash
    SSZ_BYTES32("parentHash"),                                   // the parentHash
    SSZ_LIST("uncles", ssz_bytes32, 4096),                       // the uncles (ommer block hashes)
    SSZ_BYTES32("parentBeaconBlockRoot"),                        // the parentBeaconBlockRoot
    SSZ_BYTES32("sha3Uncles"),                                   // the sha3Uncles of the uncles
    SSZ_BYTES32("transactionsRoot"),                             // the transactionsRoot
    SSZ_BYTES32("stateRoot"),                                    // the stateRoot
    SSZ_UINT64("blobGasUsed"),                                   // the gas used for the blob transactions
    SSZ_BYTES32("requestsHash")                                  // the requestHash ( eip-7685 )

};

// :: Account Proof

// Represents the storage proof of a key. The value can be taken from the last entry, which is the leaf of the proof.
static const ssz_def_t ETH_STORAGE_PROOF_DATA[] = {
    SSZ_BYTES32("key"),                     // the key
    SSZ_BYTES32("value"),                   // the value
    SSZ_LIST("proof", ssz_bytes_list, 1024) // Patricia merkle proof (simplified)
};

// Container type for storage proof data
static const ssz_def_t ETH_STORAGE_PROOF_DATA_CONTAINER = SSZ_CONTAINER("StorageProofData", ETH_STORAGE_PROOF_DATA);

// Account proof data as returned by eth_getProof.
// Contains the account state and Merkle proofs for account and storage values.
static const ssz_def_t ETH_PROOF_DATA[] = {
    SSZ_UINT256("balance"),                                      // the account balance
    SSZ_BYTES32("codeHash"),                                     // the hash of the contract code (empty for EOA)
    SSZ_UINT256("nonce"),                                        // the account nonce
    SSZ_BYTES32("storageHash"),                                 // the root hash of the storage trie
    SSZ_LIST("accountProof", ssz_bytes_list, 256),                   // Patricia Merkle proof for the account (from state root to account)
    SSZ_LIST("storageProof", ETH_STORAGE_PROOF_DATA_CONTAINER, 256), // the storage proofs for requested storage keys
};

// :: Colibri RPC-Methods

// ::: colibri_simulateTransaction

// Decoded input/output parameter for ABI decoding
static const ssz_def_t ETH_SIMULATION_INPUT_PARAM[] = {
    SSZ_STRING("name", 256),   // parameter name (e.g. "src","wad")
    SSZ_STRING("type", 256),   // parameter type (e.g. "address", "uint256")
    SSZ_STRING("value", 1024), // parameter value as string (e.g. "0xe2e2...", "299")
};
// Container type for decoded ABI input/output parameters
static const ssz_def_t ETH_SIMULATION_INPUT_PARAM_CONTAINER = SSZ_CONTAINER("InputParam", ETH_SIMULATION_INPUT_PARAM);

// Raw log data (same structure as ETH_RECEIPT_DATA_LOG)
static const ssz_def_t ETH_SIMULATION_LOG_RAW[] = {
    SSZ_ADDRESS("address"),             // contract address that emitted the log
    SSZ_BYTES("data", 1073741824),      // event data
    SSZ_LIST("topics", ssz_bytes32, 8), // event topics
};
// Container type for raw log data (without ABI decoding)
static const ssz_def_t ETH_SIMULATION_LOG_RAW_CONTAINER = SSZ_CONTAINER("LogRaw", ETH_SIMULATION_LOG_RAW);

// Enhanced log entry for simulation result (Tenderly format).
static const ssz_def_t ETH_SIMULATION_LOG[] = {
    SSZ_OPT_MASK("_optmask", 2),                                   // optional fields mask for future extensions
    SSZ_BOOLEAN("anonymous"),                                      // whether the event is anonymous (ABI decoding)
    SSZ_LIST("inputs", ETH_SIMULATION_INPUT_PARAM_CONTAINER, 256), // decoded event inputs (ABI decoding)
    SSZ_STRING("name", 256),                                       // event name (ABI decoding)
    SSZ_CONTAINER("raw", ETH_SIMULATION_LOG_RAW),                  // raw log data
};
// Container type for enhanced simulation log entries with ABI decoding
static const ssz_def_t ETH_SIMULATION_LOG_CONTAINER = SSZ_CONTAINER("SimulationLog", ETH_SIMULATION_LOG);

// Trace entry for simulation result (Tenderly format).
static const ssz_def_t ETH_SIMULATION_TRACE[] = {
    SSZ_OPT_MASK("_optmask", 4),                                          // optional fields mask
    SSZ_LIST("decodedInput", ETH_SIMULATION_INPUT_PARAM_CONTAINER, 256),  // decoded input parameters (ABI decoding)
    SSZ_LIST("decodedOutput", ETH_SIMULATION_INPUT_PARAM_CONTAINER, 256), // decoded output parameters (ABI decoding)
    SSZ_ADDRESS("from"),                                                  // caller address
    SSZ_UINT64("gas"),                                                    // gas limit (will be rendered as hex)
    SSZ_UINT64("gasUsed"),                                                // gas used (will be rendered as hex)
    SSZ_BYTES("input", 1073741824),                                       // call input data
    SSZ_STRING("method", 256),                                            // method name (ABI decoding, e.g. "approve")
    SSZ_BYTES("output", 1073741824),                                      // call output data
    SSZ_UINT32("subtraces"),                                              // number of subtraces
    SSZ_ADDRESS("to"),                                                    // target address
    SSZ_LIST("traceAddress", ssz_uint32_def, 256),                        // trace address path (e.g. [0])
    SSZ_STRING("type", 32),                                               // trace type ("CALL", "CREATE", etc.)
    SSZ_UINT256("value"),                                                 // ETH value (will be rendered as hex)
};
// Container type for execution trace entries in simulation results
static const ssz_def_t ETH_SIMULATION_TRACE_CONTAINER = SSZ_CONTAINER("SimulationTrace", ETH_SIMULATION_TRACE);

// Main simulation result structure (based on Tenderly format).
static const ssz_def_t ETH_SIMULATION_RESULT[] = {
    SSZ_OPT_MASK("_optmask", 4),                             // optional fields mask
    SSZ_UINT64("blockNumber"),                               // block number where simulation was executed
    SSZ_UINT64("cumulativeGasUsed"),                         // cumulative gas used (for simulation: same as gasUsed)
    SSZ_UINT64("gasUsed"),                                   // gas used by the transaction
    SSZ_LIST("logs", ETH_SIMULATION_LOG_CONTAINER, 1024),    // emitted logs
    SSZ_BYTE_VECTOR("logsBloom", 256),                       // logs bloom filter (future extension)
    SSZ_UINT8("status"),                                     // transaction status (0x1 = success, 0x0 = revert) - Tenderly format
    SSZ_LIST("trace", ETH_SIMULATION_TRACE_CONTAINER, 4096), // execution trace (future extension)
    SSZ_UINT8("type"),                                       // transaction type
    SSZ_BYTES("returnValue", 1073741824),                    // return value of the call
};
// Container type for the complete simulation result
static const ssz_def_t ETH_SIMULATION_RESULT_CONTAINER = SSZ_CONTAINER("SimulationResult", ETH_SIMULATION_RESULT);
