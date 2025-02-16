#include "../util/bytes.h"
#include "../util/crypto.h"
#include "../util/ssz.h"
#include "types_beacon.h"
#include <stdio.h>
#include <stdlib.h>

const ssz_def_t ssz_transactions_bytes = SSZ_BYTES("Bytes", 1073741824);

// the block hash proof is used as part of different other types since it contains all relevant
// proofs to validate the blockhash of the execution layer
const ssz_def_t BLOCK_HASH_PROOF[] = {
    SSZ_LIST("blockhash_proof", ssz_bytes32, 256),    // the merkle prooof from the executionPayload.blockhash down to the blockBodyRoot hash
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),     // the header of the beacon block
    SSZ_BIT_VECTOR("sync_committee_bits", 512),       // the bits of the validators that signed the block
    SSZ_BYTE_VECTOR("sync_committee_signature", 96)}; // the signature of the sync committee

// the stateRoot proof is used as part of different other types since it contains all relevant
// proofs to validate the stateRoot of the execution layer
const ssz_def_t ETH_STATE_PROOF[] = {
    SSZ_LIST("state_proof", ssz_bytes32, 256),        // the merkle prooof from the executionPayload.state down to the blockBodyRoot hash
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),     // the header of the beacon block
    SSZ_BIT_VECTOR("sync_committee_bits", 512),       // the bits of the validators that signed the block
    SSZ_BYTE_VECTOR("sync_committee_signature", 96)}; // the signature of the sync committee

const ssz_def_t ssz_bytes_1024 = SSZ_BYTES("Bytes", 1024);

// represents the storage proof of a key
const ssz_def_t ETH_STORAGE_PROOF[] = {
    SSZ_BYTES32("key"),                   // the key to be proven
    SSZ_LIST("proof", ssz_bytes_1024, 5), // Patricia merkle proof
    SSZ_BYTES32("value"),
};

const ssz_def_t ETH_STORAGE_PROOF_CONTAINER = SSZ_CONTAINER("StorageProof", ETH_STORAGE_PROOF);

// Entry in thr access list
const ssz_def_t ETH_ACCESS_LIST_DATA[] = {
    SSZ_ADDRESS("address"),
    SSZ_LIST("storageKeys", ssz_bytes32, 256),
};
const ssz_def_t ETH_ACCESS_LIST_DATA_CONTAINER = SSZ_CONTAINER("AccessListData", ETH_ACCESS_LIST_DATA);

// the transaction data
const ssz_def_t ETH_TX_DATA[] = {
    SSZ_BYTES32("blockHash"),       // the blockHash of the execution block containing the transaction
    SSZ_UINT64("blockNumber"),      // the number of the execution block containing the transaction
    SSZ_BYTES32("hash"),            // the blockHash of the execution block containing the transaction
    SSZ_UINT32("transactionIndex"), // the index of the transaction in the block
    SSZ_UINT8("type"),              // the type of the transaction
    SSZ_UINT64("nonce"),            // the gasPrice of the transaction
    SSZ_BYTES("input", 1073741824), // the raw transaction payload
    SSZ_BYTES32("r"),               // the r value of the transaction
    SSZ_BYTES32("s"),               // the s value of the transaction
    SSZ_UINT32("chainId"),          // the s value of the transaction
    SSZ_UINT8("v"),                 // the v value of the transaction
    SSZ_UINT64("gas"),              // the gas limnit
    SSZ_ADDRESS("from"),            // the sender of the transaction
    SSZ_BYTES("to", 20),            // the target of the transaction
    SSZ_UINT256("value"),           // the value of the transaction
    SSZ_UINT64("gasPrice"),
    SSZ_UINT64("maxFeePerGas"),
    SSZ_UINT64("maxPriorityFeePerGas"),
    SSZ_LIST("accessList", ETH_ACCESS_LIST_DATA_CONTAINER, 256),
    SSZ_LIST("blobVersionedHashes", ssz_bytes32, 16),
    SSZ_UINT8("yParity")}; // the gasPrice of the transaction

// a log entry in the receipt
const ssz_def_t ETH_RECEIPT_DATA_LOG[] = {
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
const ssz_def_t ETH_RECEIPT_DATA_LOG_CONTAINER = SSZ_CONTAINER("Log", ETH_RECEIPT_DATA_LOG);

// the transaction data
const ssz_def_t ETH_RECEIPT_DATA[] = {
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

// represents the proof for a transaction receipt

// 1. All Receipts of the execution blocks are serialized into a Patricia Merkle Trie and the merkle proof is created for the requested receipt.
// 2. The **payload of the transaction** is used to create its SSZ Hash Tree Root from the BeaconBlock. This is needed in order to verify that the receipt actually belongs to the given transactionhash.
// 3. The **SSZ Multi Merkle Proof** from the Transactions, Receipts, BlockNumber and BlockHash of the ExecutionPayload to the BlockBodyRoot. (Total Depth: 29)
// 4. **BeaconBlockHeader** is passed because also need the slot in order to find out which period and which sync committee is used.
// 5. **Signature of the SyncCommittee** (taken from the following block) is used to verify the SignData where the blockhash is part of the message and the Domain is calculated from the fork and the Genesis Validator Root.
// ```mermaid
// flowchart TB
//     subgraph "ExecutionPayload"
//         transactions
//         receipts
//         blockNumber
//         blockHash
//     end
//     Receipt --PM--> receipts
//     TX --SSZ D:21--> transactions
//     subgraph "BeaconBlockBody"
//         transactions  --SSZ D:5--> executionPayload
//         blockNumber --SSZ D:5--> executionPayload
//         blockHash --SSZ D:5--> executionPayload
//         m[".."]
//     end
//     subgraph "BeaconBlockHeader"
//         slot
//         proposerIndex
//         parentRoot
//         s[stateRoot]
//         executionPayload  --SSZ D:4--> bodyRoot
//     end
// ```

const ssz_def_t ETH_RECEIPT_PROOF[] = {
    SSZ_BYTES("transaction", 1073741824),             // the raw transaction payload
    SSZ_UINT32("transactionIndex"),                   // the index of the transaction in the block
    SSZ_UINT64("blockNumber"),                        // the number of the execution block containing the transaction
    SSZ_BYTES32("blockHash"),                         // the blockHash of the execution block containing the transaction
    SSZ_LIST("receipt_proof", ssz_bytes_1024, 64),    // the Merklr Patricia Proof of the transaction receipt ending in the receipt root
    SSZ_LIST("block_proof", ssz_bytes32, 64),         // the multi proof of the transaction, receipt_root,blockNumber and blockHash
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),     // the header of the beacon block
    SSZ_BIT_VECTOR("sync_committee_bits", 512),       // the bits of the validators that signed the block
    SSZ_BYTE_VECTOR("sync_committee_signature", 96)}; // the signature of the sync committee

const ssz_def_t ETH_LOGS_TX[] = {
    SSZ_BYTES("transaction", 1073741824),  // the raw transaction payload
    SSZ_UINT32("transactionIndex"),        // the index of the transaction in the block
    SSZ_LIST("proof", ssz_bytes_1024, 64), // the Merklr Patricia Proof of the transaction receipt ending in the receipt root
};
const ssz_def_t ETH_LOGS_TX_CONTAINER = SSZ_CONTAINER("LogsTx", ETH_LOGS_TX);

const ssz_def_t ETH_LOGS_BLOCK[] = {
    SSZ_UINT64("blockNumber"),                       // the number of the execution block containing the transaction
    SSZ_BYTES32("blockHash"),                        // the blockHash of the execution block containing the transaction
    SSZ_LIST("proof", ssz_bytes32, 64),              // the multi proof of the transaction, receipt_root,blockNumber and blockHash
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),    // the header of the beacon block
    SSZ_BIT_VECTOR("sync_committee_bits", 512),      // the bits of the validators that signed the block
    SSZ_BYTE_VECTOR("sync_committee_signature", 96), // the signature of the sync committee
    SSZ_LIST("txs", ETH_LOGS_TX_CONTAINER, 256)};    // the transactions of the block

const ssz_def_t ETH_LOGS_BLOCK_CONTAINER = SSZ_CONTAINER("LogsBlock", ETH_LOGS_BLOCK);

// represents the account and storage values, including the Merkle proof, of the specified account.

// 1. The **payload of the transaction** is used to create its SSZ Hash Tree Root.
// 2. The **SSZ Merkle Proof** from the Transactions of the ExecutionPayload to the BlockBodyRoot. (Total Depth: 29)
// 3. **BeaconBlockHeader** is passed because also need the slot in order to find out which period and which sync committee is used.
// 4. **Signature of the SyncCommittee** (taken from the following block) is used to verify the SignData where the blockhash is part of the message and the Domain is calculated from the fork and the Genesis Validator Root.
// ```mermaid
// flowchart TB
//     subgraph "ExecutionPayload"
//         transactions
//         blockNumber
//         blockHash
//     end
//     TX --SSZ D:21--> transactions
//     subgraph "BeaconBlockBody"
//         transactions  --SSZ D:5--> executionPayload
//         blockNumber --SSZ D:5--> executionPayload
//         blockHash --SSZ D:5--> executionPayload
//         m[".."]
//     end
//     subgraph "BeaconBlockHeader"
//         slot
//         proposerIndex
//         parentRoot
//         s[stateRoot]
//         executionPayload  --SSZ D:4--> bodyRoot
//     end
// ```

const ssz_def_t ETH_TRANSACTION_PROOF[] = {
    SSZ_BYTES("transaction", 1073741824),             // the raw transaction payload
    SSZ_UINT32("transactionIndex"),                   // the index of the transaction in the block
    SSZ_UINT64("blockNumber"),                        // the number of the execution block containing the transaction
    SSZ_BYTES32("blockHash"),                         // the blockHash of the execution block containing the transaction
    SSZ_LIST("proof", ssz_bytes32, 64),               // the multi proof of the transaction, blockNumber and blockHash
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),     // the header of the beacon block
    SSZ_BIT_VECTOR("sync_committee_bits", 512),       // the bits of the validators that signed the block
    SSZ_BYTE_VECTOR("sync_committee_signature", 96)}; // the signature of the sync committee

// 1. **Patricia Merkle Proof** for the Account Object in the execution layer (balance, nonce, codeHash, storageHash) and the storage values with its own Proofs. (using eth_getProof): Result StateRoot
// 2. **State Proof** is a SSZ Merkle Proof from the StateRoot to the ExecutionPayload over the BeaconBlockBody to its root hash which is part of the header.
// 3. **BeaconBlockHeader** is passed because also need the slot in order to find out which period and which sync committee is used.
// 4. **Signature of the SyncCommittee** (taken from the following block) is used to verify the SignData where the blockhash is part of the message and the Domain is calculated from the fork and the Genesis Validator Root.

// ```mermaid
// flowchart TB
//     subgraph "ExecutionLayer"
//         class ExecutionLayer transparent

//         subgraph "Account"
//             balance --> account
//             nonce --> account
//             codeHash --> account
//             storageHash --> account
//         end

//         subgraph "Storage"
//             key1 --..PM..-->storageHash
//             key2 --..PM..-->storageHash
//             key3 --..PM..-->storageHash
//         end
//     end

//     subgraph "ConsensusLayer"
//         subgraph "ExecutionPayload"
//             account --..PM..--> stateRoot
//         end

//         subgraph "BeaconBlockBody"
//             stateRoot --SSZ D:5--> executionPayload
//             m[".."]
//         end

//         subgraph "BeaconBlockHeader"
//             slot
//             proposerIndex
//             parentRoot
//             s[stateRoot]
//             executionPayload  --SSZ D:4--> bodyRoot
//         end

//     end

// ```

const ssz_def_t ETH_ACCOUNT_PROOF[] = {
    SSZ_LIST("accountProof", ssz_bytes_1024, 256),              // Patricia merkle proof
    SSZ_ADDRESS("address"),                                     // the address of the account
    SSZ_BYTES32("balance"),                                     // the balance of the account
    SSZ_BYTES32("codeHash"),                                    // the code hash of the account
    SSZ_BYTES32("nonce"),                                       // the nonce of the account
    SSZ_BYTES32("storageHash"),                                 // the storage hash of the account
    SSZ_LIST("storageProof", ETH_STORAGE_PROOF_CONTAINER, 256), // the storage proofs of the selected
    SSZ_CONTAINER("state_proof", ETH_STATE_PROOF)};             // the state proof of the account

const ssz_def_t ETH_ACCOUNT_PROOF_CONTAINER     = SSZ_CONTAINER("AccountProof", ETH_ACCOUNT_PROOF);
const ssz_def_t ETH_TRANSACTION_PROOF_CONTAINER = SSZ_CONTAINER("TransactionProof", ETH_TRANSACTION_PROOF);
const ssz_def_t LIGHT_CLIENT_UPDATE_CONTAINER   = SSZ_CONTAINER("LightClientUpdate", LIGHT_CLIENT_UPDATE);

// A List of possible types of data matching the Proofs
const ssz_def_t C4_REQUEST_DATA_UNION[] = {
    SSZ_NONE,
    SSZ_BYTES32("blockhash"),                                   // the blochash  which is used for blockhash proof
    SSZ_BYTES32("balance"),                                     // the balance of an account
    SSZ_CONTAINER("EthTransactionData", ETH_TX_DATA),           // the transaction data
    SSZ_CONTAINER("EthReceiptData", ETH_RECEIPT_DATA),          // the transaction receipt
    SSZ_LIST("EthLogs", ETH_RECEIPT_DATA_LOG_CONTAINER, 1024)}; // result of eth_getLogs

// A List of possible types of proofs matching the Data
const ssz_def_t C4_REQUEST_PROOFS_UNION[] = {
    SSZ_NONE,
    SSZ_CONTAINER("BlockHashProof", BLOCK_HASH_PROOF),
    SSZ_CONTAINER("AccountProof", ETH_ACCOUNT_PROOF),
    SSZ_CONTAINER("TransactionProof", ETH_TRANSACTION_PROOF),
    SSZ_CONTAINER("ReceiptProof", ETH_RECEIPT_PROOF),      // a Proof of a TransactionReceipt
    SSZ_LIST("LogsProof", ETH_LOGS_BLOCK_CONTAINER, 256)}; // a Proof for multiple Receipts and txs

// A List of possible types of sync data used to update the sync state by verifying the transition from the last period to the required.
const ssz_def_t C4_REQUEST_SYNCDATA_UNION[] = {
    SSZ_NONE,
    SSZ_LIST("LightClientUpdate", LIGHT_CLIENT_UPDATE_CONTAINER, 512)}; // this light client update can be fetched directly from the beacon chain API

// the main container defining the incoming data processed by the verifier
const ssz_def_t C4_REQUEST[] = {
    SSZ_BYTE_VECTOR("version", 4),                      // the [domain, major, minor, patch] version of the request, domaon=1 = eth
    SSZ_UNION("data", C4_REQUEST_DATA_UNION),           // the data to proof
    SSZ_UNION("proof", C4_REQUEST_PROOFS_UNION),        // the proof of the data
    SSZ_UNION("sync_data", C4_REQUEST_SYNCDATA_UNION)}; // the sync data containing proofs for the transition between the two periods

const ssz_def_t C4_REQUEST_CONTAINER = SSZ_CONTAINER("C4Request", C4_REQUEST);
