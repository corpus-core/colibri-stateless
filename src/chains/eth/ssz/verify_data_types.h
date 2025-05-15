#include "beacon_types.h"
#include "ssz.h"

// : Ethereum

// :: Transaction Proof

// Entry in the access list of a transaction or call.
static const ssz_def_t ETH_ACCESS_LIST_DATA[] = {
    SSZ_ADDRESS("address"),
    SSZ_LIST("storageKeys", ssz_bytes32, 256),
};
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
static const ssz_def_t ETH_AUTHORIZATION_LIST_DATA_CONTAINER = SSZ_CONTAINER("AuthorizationListData", ETH_AUTHORIZATION_LIST_DATA);

// the transaction data as result of an eth_getTransactionByHash rpc-call.
static const ssz_def_t ETH_TX_DATA[] = {
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
    SSZ_UINT8("yParity")};                                                     // the yParity of the transaction

// :: Logs Proof

// a log entry in the receipt
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
static const ssz_def_t ETH_RECEIPT_DATA_LOG_CONTAINER = SSZ_CONTAINER("Log", ETH_RECEIPT_DATA_LOG);

// :: Receipt Proof

// the transaction data
static const ssz_def_t ETH_RECEIPT_DATA[] = {
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
}; // the gasPrice of the transaction

static const ssz_def_t ETH_TX_DATA_CONTAINER              = SSZ_CONTAINER("EthTransactionData", ETH_TX_DATA);
static const ssz_def_t ETH_BLOCK_DATA_TRANSACTION_UNION[] = {
    SSZ_LIST("as_hashes", ssz_bytes32, 4096),         // the transactions hashes
    SSZ_LIST("as_data", ETH_TX_DATA_CONTAINER, 4096), // the transactions data
};

// :: Block Proof

// display the block data , which is based on the execution payload
static const ssz_def_t ETH_BLOCK_DATA[] = {
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
    SSZ_LIST("uncles", ssz_bytes32, 4096),                       // the transactions hashes
    SSZ_BYTES32("parentBeaconBlockRoot"),                        // the parentBeaconBlockRoot
    SSZ_BYTES32("sha3Uncles"),                                   // the sha3Uncles of the uncles
    SSZ_BYTES32("transactionsRoot"),                             // the transactionsRoot
    SSZ_BYTES32("stateRoot"),                                    // the stateRoot
    SSZ_UINT64("blobGasUsed"),                                   // the gas used for the blob transactions
};

// :: Account Proof

// represents the storage proof of a key. The value can be taken from the last entry, which is the leaf of the proof.
static const ssz_def_t ETH_STORAGE_PROOF_DATA[] = {
    SSZ_BYTES32("key"),                     // the key
    SSZ_BYTES32("value"),                   // the value
    SSZ_LIST("proof", ssz_bytes_1024, 1024) // Patricia merkle proof
};

static const ssz_def_t ETH_STORAGE_PROOF_DATA_CONTAINER = SSZ_CONTAINER("StorageProofData", ETH_STORAGE_PROOF_DATA);

static const ssz_def_t ETH_PROOF_DATA[] = {
    SSZ_UINT256("balance"),
    SSZ_BYTES32("codeHash"),
    SSZ_UINT256("nonce"),
    SSZ_BYTES32("storageHash"),
    SSZ_LIST("accountProof", ssz_bytes_1024, 256),                   // Patricia merkle proof
    SSZ_LIST("storageProof", ETH_STORAGE_PROOF_DATA_CONTAINER, 256), // the storage proofs of the selected
};
