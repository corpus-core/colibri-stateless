#include "beacon_types.h"
#include "ssz.h"
#include <stdio.h>
#include <stdlib.h>

// the block hash proof is used as part of different other types since it contains all relevant
// proofs to validate the blockhash of the execution layer
static const ssz_def_t BLOCK_HASH_PROOF[] = {
    SSZ_LIST("blockhash_proof", ssz_bytes32, 256),    // the merkle prooof from the executionPayload.blockhash down to the blockBodyRoot hash
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),     // the header of the beacon block
    SSZ_BIT_VECTOR("sync_committee_bits", 512),       // the bits of the validators that signed the block
    SSZ_BYTE_VECTOR("sync_committee_signature", 96)}; // the signature of the sync committee

// the stateRoot proof is used as part of different other types since it contains all relevant
// proofs to validate the stateRoot of the execution layer
static const ssz_def_t ETH_STATE_PROOF[] = {
    SSZ_LIST("state_proof", ssz_bytes32, 256),        // the merkle prooof from the executionPayload.state down to the blockBodyRoot hash
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),     // the header of the beacon block
    SSZ_BIT_VECTOR("sync_committee_bits", 512),       // the bits of the validators that signed the block
    SSZ_BYTE_VECTOR("sync_committee_signature", 96)}; // the signature of the sync committee

static const ssz_def_t ETH_STATE_PROOF_CONTAINER = SSZ_CONTAINER("StateProof", ETH_STATE_PROOF);

static const ssz_def_t ssz_bytes_1024 = SSZ_BYTES("Bytes", 1073741824);

// represents the storage proof of a key
static const ssz_def_t ETH_STORAGE_PROOF[] = {
    SSZ_BYTES32("key"),                      // the key to be proven
    SSZ_LIST("proof", ssz_bytes_1024, 1024), // Patricia merkle proof
};

static const ssz_def_t ETH_STORAGE_PROOF_CONTAINER = SSZ_CONTAINER("StorageProof", ETH_STORAGE_PROOF);

// Entry in thr access list
static const ssz_def_t ETH_ACCESS_LIST_DATA[] = {
    SSZ_ADDRESS("address"),
    SSZ_LIST("storageKeys", ssz_bytes32, 256),
};
static const ssz_def_t ETH_ACCESS_LIST_DATA_CONTAINER = SSZ_CONTAINER("AccessListData", ETH_ACCESS_LIST_DATA);

// the transaction data
static const ssz_def_t ETH_TX_DATA[] = {
    SSZ_BYTES32("blockHash"),       // the blockHash of the execution block containing the transaction
    SSZ_UINT64("blockNumber"),      // the number of the execution block containing the transaction
    SSZ_BYTES32("hash"),            // the blockHash of the execution block containing the transaction
    SSZ_UINT32("transactionIndex"), // the index of the transaction in the block
    SSZ_UINT8("type"),              // the type of the transaction
    SSZ_UINT64("nonce"),            // the nonce of the transaction
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
    SSZ_UINT8("yParity")};

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

static const ssz_def_t ETH_RECEIPT_PROOF[] = {
    SSZ_BYTES("transaction", 1073741824),             // the raw transaction payload
    SSZ_UINT32("transactionIndex"),                   // the index of the transaction in the block
    SSZ_UINT64("blockNumber"),                        // the number of the execution block containing the transaction
    SSZ_BYTES32("blockHash"),                         // the blockHash of the execution block containing the transaction
    SSZ_LIST("receipt_proof", ssz_bytes_1024, 64),    // the Merklr Patricia Proof of the transaction receipt ending in the receipt root
    SSZ_LIST("block_proof", ssz_bytes32, 64),         // the multi proof of the transaction, receipt_root,blockNumber and blockHash
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),     // the header of the beacon block
    SSZ_BIT_VECTOR("sync_committee_bits", 512),       // the bits of the validators that signed the block
    SSZ_BYTE_VECTOR("sync_committee_signature", 96)}; // the signature of the sync committee

static const ssz_def_t ETH_LOGS_TX[] = {
    SSZ_BYTES("transaction", 1073741824),  // the raw transaction payload
    SSZ_UINT32("transactionIndex"),        // the index of the transaction in the block
    SSZ_LIST("proof", ssz_bytes_1024, 64), // the Merklr Patricia Proof of the transaction receipt ending in the receipt root
};
static const ssz_def_t ETH_LOGS_TX_CONTAINER = SSZ_CONTAINER("LogsTx", ETH_LOGS_TX);

static const ssz_def_t ETH_LOGS_BLOCK[] = {
    SSZ_UINT64("blockNumber"),                       // the number of the execution block containing the transaction
    SSZ_BYTES32("blockHash"),                        // the blockHash of the execution block containing the transaction
    SSZ_LIST("proof", ssz_bytes32, 64),              // the multi proof of the transaction, receipt_root,blockNumber and blockHash
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),    // the header of the beacon block
    SSZ_BIT_VECTOR("sync_committee_bits", 512),      // the bits of the validators that signed the block
    SSZ_BYTE_VECTOR("sync_committee_signature", 96), // the signature of the sync committee
    SSZ_LIST("txs", ETH_LOGS_TX_CONTAINER, 256)};    // the transactions of the block

static const ssz_def_t ETH_LOGS_BLOCK_CONTAINER = SSZ_CONTAINER("LogsBlock", ETH_LOGS_BLOCK);

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

static const ssz_def_t ETH_TRANSACTION_PROOF[] = {
    SSZ_BYTES("transaction", 1073741824),             // the raw transaction payload
    SSZ_UINT32("transactionIndex"),                   // the index of the transaction in the block
    SSZ_UINT64("blockNumber"),                        // the number of the execution block containing the transaction
    SSZ_BYTES32("blockHash"),                         // the blockHash of the execution block containing the transaction
    SSZ_UINT64("baseFeePerGas"),                      // the baseFeePerGas
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

static const ssz_def_t ETH_ACCOUNT_PROOF[] = {
    SSZ_LIST("accountProof", ssz_bytes_1024, 256),              // Patricia merkle proof
    SSZ_ADDRESS("address"),                                     // the address of the account
    SSZ_LIST("storageProof", ETH_STORAGE_PROOF_CONTAINER, 256), // the storage proofs of the selected
    SSZ_CONTAINER("state_proof", ETH_STATE_PROOF)};             // the state proof of the account

static const ssz_def_t ETH_CODE_UNION[] = {
    SSZ_BOOLEAN("code_used"),   // no code delivered
    SSZ_BYTES("code", 4194304), // the code of the contract
};

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

static const ssz_def_t ETH_CALL_ACCOUNT[] = {
    SSZ_LIST("accountProof", ssz_bytes_1024, 256),               // Patricia merkle proof
    SSZ_ADDRESS("address"),                                      // the address of the account
    SSZ_UNION("code", ETH_CODE_UNION),                           // the code of the contract
    SSZ_LIST("storageProof", ETH_STORAGE_PROOF_CONTAINER, 4096), // the storage proofs of the selected
};
static const ssz_def_t ETH_CALL_ACCOUNT_CONTAINER = SSZ_CONTAINER("EthCallAccount", ETH_CALL_ACCOUNT);
static const ssz_def_t ETH_CALL_PROOF[]           = {
    SSZ_LIST("accounts", ETH_CALL_ACCOUNT_CONTAINER, 256), // used accounts
    SSZ_CONTAINER("state_proof", ETH_STATE_PROOF)};        // the state proof of the account

// Proof as input data for the sync committee transition used by zk. This is a very compact proof mostly taken from the light client update.
// the proof itself is a merkle proof using the given gindex to verify from the hash of the pubkey all the way down to the signing root.
//
// The following diagram shows the Structure of the Merkle Tree leading to the SigningRoot:
// ```mermaid
// flowchart BT
//     classDef noBorder fill:none,stroke:none;
//     subgraph "header"
//         Slot
//         proposerIndex
//         parentRoot
//         stateRoot
//         bodyRoot
//     end
//
//    subgraph "SigningData"
//         blockheaderhash
//         Domain
//     end
//
//    subgraph "BeaconState"
//         beacon_mode(" ... ")
//         current_sync_committee
//         next_sync_committee
//         inactivity_scores
//         finalized_checkpoint
//
//
//     end
//     class beacon_mode noBorder
//
//     subgraph "SyncCommittee"
//         pubkeys
//         aggregate_pubkey
//     end
//
//     subgraph "ValidatorPubKeys"
//         Val1["Val 1"]
//         Val1_a["[0..31]"]
//         Val1_b["[32..64]"]
//         Val2["Val 2"]
//         Val2_a["[0..31]"]
//         Val2_b["[32..48]"]
//         val_mode(" ... ")
//     end
//
//     class val_mode noBorder
//
//     blockheaderhash --> SigningRoot
//     Domain --> SigningRoot
//     4{4} --> blockheaderhash
//     5{5} --> blockheaderhash
//     8{8} --> 4
//     9{9} --> 4
//     10{10} --> 5
//     11{11} --> 5
//     Slot --> 8
//     proposerIndex --> 8
//     parentRoot --> 9
//     stateRoot --> 9
//     bodyRoot --> 10
//     21{"zero"} --> 10
//     22{"zero"} --> 11
//     23{"zero"} --> 11
//
//
//     38{38} --> stateRoot
//     39{39} --> stateRoot
//
//
//     76{76} --> 38
//     77{77} --> 38
//     78{78} --> 39
//     79{79} --> 39
//
//     156{156} -->78
//     157{157} -->78
//
//     158("...") --> 79
//
//     314{314} --> 157
//     315{315} --> 157
//
//     finalized_checkpoint --> 314
//     inactivity_scores --> 314
//     current_sync_committee --> 315
//     next_sync_committee --> 315
//
//
//     pubkeys --> next_sync_committee
//     aggregate_pubkey --> next_sync_committee
//
//     2524{2524} --> pubkeys
//     2525{2525} --> pubkeys
//
//
//     5048{5048}  --> 2524
//     5049{5049}  --> 2524
//     10096{10096}  --> 5048
//     10097{10097}  --> 5048
//     20192{20192}  --> 10096
//     20193{20193}  --> 10096
//     40384{40384}  --> 20192
//     40385{40385}  --> 20192
//     80768{80768}  --> 40384
//     80769{80769}  --> 40384
//     161536{161536}  --> 80768
//     161537{161537}  --> 80768
//     323072{323072}  --> 161536
//     323073{323073}  --> 161536
//     Val1  --> 323072
//     Val2  --> 323072
//
//     Val1_a --> Val1
//     Val1_b --> Val1
//     Val2_a --> Val2
//     Val2_b --> Val2
//
//
//     class 158 noBorder
//
// ```
//
// In order to validate, we need to calculate
// - 512 x sha256 for each pubkey
// - 512 x sha256 merkle proof for the pubkeys
// - 2 x sha256 for the SyncCommittee
// - 5 x sha256 for the stateRoot
// - 3 x sha256 for the blockheader hash
// - 1 x for the SigningRoot
//
// So in total, we need to verify 1035 hashes and 1 bls signature.

static const ssz_def_t ETH_SYNC_PROOF[] = {
    SSZ_VECTOR("oldKeys", ssz_bls_pubky, 512),     // the old keys which produced the signature
    SSZ_VECTOR("newKeys", ssz_bls_pubky, 512),     // the new keys to be proven
    SSZ_BIT_VECTOR("syncCommitteeBits", 512),      // the bits of the validators that signed the block
    SSZ_BYTE_VECTOR("syncCommitteeSignature", 96), // the signature of the sync committee
    SSZ_UINT64("gidx"),                            // the general index from the signing root to the pubkeys of the next_synccommittee
    SSZ_VECTOR("proof", ssz_bytes32, 10),          // proof merkle proof from the signing root to the pubkeys of the next_synccommittee
    SSZ_UINT64("slot"),                            // the slot of the block
    SSZ_UINT64("proposerIndex")};

static const ssz_def_t LIGHT_CLIENT_UPDATE_CONTAINER = SSZ_CONTAINER("LightClientUpdate", LIGHT_CLIENT_UPDATE);

static const ssz_def_t ETH_EXECUTION_PAYLOAD_UNION[] = {
    SSZ_CONTAINER("DenepExecutionPayload", DENEP_EXECUTION_PAYLOAD),
};

// the stateRoot proof is used as part of different other types since it contains all relevant
// proofs to validate the stateRoot of the execution layer
static const ssz_def_t ETH_BLOCK_PROOF[] = {
    SSZ_UNION("executionPayload", ETH_EXECUTION_PAYLOAD_UNION), // the merkle prooof from the executionPayload.state down to the blockBodyRoot hash
    SSZ_LIST("proof", ssz_bytes32, 256),                        // the merkle prooof from the executionPayload.state down to the blockBodyRoot hash
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),               // the header of the beacon block
    SSZ_BIT_VECTOR("sync_committee_bits", 512),                 // the bits of the validators that signed the block
    SSZ_BYTE_VECTOR("sync_committee_signature", 96)};           // the signature of the sync committee
/*
number:"0x1528ddc",
hash:"0x9fbf17a5e8ccde50e33504db280d01188859ca698542030b2e43ffe11a6d7558",
transactions:[],
logsBloom:"0xfffbef5ae97b9ade5e7c62f6b6fbdf27bfbbb1f7cfb2ded75acf8f1a3ffff457bd7fff7ebb1f7dfef573dfffc47fdf7fef3da37dfc93bf617fdbf7ee1cfffbfb57f1e7bb8b6fc9adc96ed35bf2667debde9ddaff5fffdf7f777f3fcfbdfaeeb8bcfeef7eae75e77bbfeb5eeb4f3f6fffaffb7d6f72575cdbe7ffbcfe3fef3d6daef76b6b75d3df785d7fdafceeb2f6662779dcdbb7ffffbabfeb4efe77bdbefeabbdbcefbffbeebfeff36bcf7fbaffcfbd16cdf6f33777ebedffc5ee7cffbdf0eca97d672bfbfaffcf27b69ffe3efb2fafeff77df7fffcfebba90bafeafd6f73cffaaff9f475d7ff7bddd6dab47fd7e27f64e7cb8fff28dd9d6d8dfe395d9eab",
receiptsRoot:"0x21b6d4d5ab8415ccaf69254b8e72cdf23b6fdea6eb3d0a4ab9a8276df94d9a20",
extraData:"0x546974616e2028746974616e6275696c6465722e78797a29",
withdrawalsRoot:"0xd7628c1029f98c74a7ea2dee50cd730b0f1a21896a8148c73dfac69b9f0167d7",
baseFeePerGas:"0x7ce92fe1",
nonce:"0x0000000000000000",
miner:"0x4838b106fce9647bdf1e7877bf73ce8b0bad5f97",
withdrawals:[],
excessBlobGas:"0xd20000",
difficulty:"0x0",
gasLimit:"0x2255100",
gasUsed:"0x146d555",
uncles:[
],
parentBeaconBlockRoot:"0x5145dbdbc87a32b16c83ab68bc2e273f4b3c368beb5b451f8dd7f4455021aacf",
size:"0x15861",
sha3Uncles:"0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347",
transactionsRoot:"0xed9eba655dddbd8b13ac02eb412f248ea78fc0c41aa7ec161adc92d475a9eb03",
stateRoot:"0x0a4450f7682d819abe407243abd3051890b75ed117ccfa1c752a94b7238423da",
mixHash:"0xbea3a39102b3fb6a7bb228e243636ccd7741c79bcced70ccdca05e53719a639e",
parentHash:"0x2c0ba2cbb92345ef9e08c40dde99d33e7550b8c957106bd7edeb2f3abc36c5b2",
blobGasUsed:"0xc0000",
timestamp:"0x67ee5457"
*/
static const ssz_def_t ETH_TX_DATA_CONTAINER               = SSZ_CONTAINER("EthTransactionData", ETH_TX_DATA);
static const ssz_def_t ETH_BLOCK_DATA_TRANSACTION_UNTION[] = {
    SSZ_LIST("transactions", ssz_bytes32, 4096),           // the transactions hashes
    SSZ_LIST("transactions", ETH_TX_DATA_CONTAINER, 4096), // the transactions data
};

static const ssz_def_t ETH_BLOCK_DATA[] = {
    SSZ_UINT64("number"),                                         // the blocknumber
    SSZ_BYTES32("hash"),                                          // the blockhash
    SSZ_UNION("transactions", ETH_BLOCK_DATA_TRANSACTION_UNTION), // the transactions
    SSZ_BYTE_VECTOR("logsBloom", 256),                            // the logsBloom
    SSZ_BYTES32("receiptsRoot"),                                  // the receiptsRoot
    SSZ_BYTES("extraData", 32),                                   // the extraData
    SSZ_BYTES32("withdrawalsRoot"),                               // the withdrawalsRoot
    SSZ_UINT256("baseFeePerGas"),                                 // the baseFeePerGas
    SSZ_BYTE_VECTOR("nonce", 8),                                  // the nonce
    SSZ_ADDRESS("miner"),                                         // the miner
    SSZ_LIST("withdrawals", DENEP_WITHDRAWAL_CONTAINER, 4096),    // the withdrawals
    SSZ_UINT64("excessBlobGas"),                                  // the excessBlobGas
    SSZ_UINT64("difficulty"),                                     // the difficulty
    SSZ_UINT64("gasLimit"),                                       // the gasLimit
    SSZ_UINT64("gasUsed"),                                        // the gasUsed
    SSZ_UINT64("timestamp"),                                      // the timestamp
    SSZ_BYTES32("mixHash"),                                       // the mixHash
    SSZ_BYTES32("parentHash"),                                    // the parentHash
    SSZ_LIST("uncles", ssz_bytes32, 4096),                        // the transactions hashes
    SSZ_BYTES32("parentBeaconBlockRoot"),                         // the parentBeaconBlockRoot
    SSZ_BYTES32("sha3Uncles"),                                    // the sha3Uncles of the uncles
    SSZ_BYTES32("transactionsRoot"),                              // the transactionsRoot
    SSZ_BYTES32("stateRoot"),                                     // the stateRoot
    SSZ_UINT64("blobGasUsed"),                                    // the gas used for the blob transactions
};

// A List of possible types of data matching the Proofs
static const ssz_def_t C4_REQUEST_DATA_UNION[] = {
    SSZ_NONE,
    SSZ_BYTES32("hash"),                                       // the blochash  which is used for blockhash proof
    SSZ_BYTES("bytes", 1073741824),                            // the bytes of the data
    SSZ_UINT256("value"),                                      // the balance of an account
    SSZ_CONTAINER("EthTransactionData", ETH_TX_DATA),          // the transaction data
    SSZ_CONTAINER("EthReceiptData", ETH_RECEIPT_DATA),         // the transaction receipt
    SSZ_LIST("EthLogs", ETH_RECEIPT_DATA_LOG_CONTAINER, 1024), // result of eth_getLogs
    SSZ_CONTAINER("EthBlockData", ETH_BLOCK_DATA)};            // the block data
// A List of possible types of proofs matching the Data
static const ssz_def_t C4_REQUEST_PROOFS_UNION[] = {
    SSZ_NONE,
    SSZ_CONTAINER("BlockHashProof", BLOCK_HASH_PROOF),
    SSZ_CONTAINER("AccountProof", ETH_ACCOUNT_PROOF),
    SSZ_CONTAINER("TransactionProof", ETH_TRANSACTION_PROOF),
    SSZ_CONTAINER("ReceiptProof", ETH_RECEIPT_PROOF),     // a Proof of a TransactionReceipt
    SSZ_LIST("LogsProof", ETH_LOGS_BLOCK_CONTAINER, 256), // a Proof for multiple Receipts and txs
    SSZ_CONTAINER("CallProof", ETH_CALL_PROOF),
    SSZ_CONTAINER("SyncProof", ETH_SYNC_PROOF),   // Proof as input data for the sync committee transition used by zk
    SSZ_CONTAINER("BlockProof", ETH_BLOCK_PROOF), // Proof for BlockData
}; // a Proof for multiple accounts

// A List of possible types of sync data used to update the sync state by verifying the transition from the last period to the required.
static const ssz_def_t C4_REQUEST_SYNCDATA_UNION[] = {
    SSZ_NONE,
    SSZ_LIST("LightClientUpdate", LIGHT_CLIENT_UPDATE_CONTAINER, 512)}; // this light client update can be fetched directly from the beacon chain API

// the main container defining the incoming data processed by the verifier
static const ssz_def_t C4_REQUEST[] = {
    SSZ_BYTE_VECTOR("version", 4),                      // the [domain, major, minor, patch] version of the request, domaon=1 = eth
    SSZ_UNION("data", C4_REQUEST_DATA_UNION),           // the data to proof
    SSZ_UNION("proof", C4_REQUEST_PROOFS_UNION),        // the proof of the data
    SSZ_UNION("sync_data", C4_REQUEST_SYNCDATA_UNION)}; // the sync data containing proofs for the transition between the two periods

static const ssz_def_t C4_REQUEST_CONTAINER = SSZ_CONTAINER("C4Request", C4_REQUEST);

static inline size_t array_idx(const ssz_def_t* array, size_t len, const ssz_def_t* target) {
  for (size_t i = 0; i < len; i++) {
    if (array[i].type >= SSZ_TYPE_CONTAINER && array[i].def.container.elements == target) return i;
  }
  return 0;
}
#define ARRAY_IDX(a, target)  array_idx(a, sizeof(a) / sizeof(ssz_def_t), target)
#define ARRAY_TYPE(a, target) a + array_idx(a, sizeof(a) / sizeof(ssz_def_t), target)

const ssz_def_t* eth_ssz_verification_type(eth_ssz_type_t type) {
  switch (type) {
    case ETH_SSZ_VERIFY_LIGHT_CLIENT_UPDATE_LIST:
      return ARRAY_TYPE(C4_REQUEST_SYNCDATA_UNION, &LIGHT_CLIENT_UPDATE_CONTAINER);
    case ETH_SSZ_VERIFY_LIGHT_CLIENT_UPDATE:
      return &LIGHT_CLIENT_UPDATE_CONTAINER;
    case ETH_SSZ_VERIFY_REQUEST:
      return &C4_REQUEST_CONTAINER;
    case ETH_SSZ_VERIFY_BLOCK_HASH_PROOF:
      return ARRAY_TYPE(C4_REQUEST_PROOFS_UNION, BLOCK_HASH_PROOF);
    case ETH_SSZ_VERIFY_ACCOUNT_PROOF:
      return ARRAY_TYPE(C4_REQUEST_PROOFS_UNION, ETH_ACCOUNT_PROOF);
    case ETH_SSZ_VERIFY_TRANSACTION_PROOF:
      return ARRAY_TYPE(C4_REQUEST_PROOFS_UNION, ETH_TRANSACTION_PROOF);
    case ETH_SSZ_VERIFY_RECEIPT_PROOF:
      return ARRAY_TYPE(C4_REQUEST_PROOFS_UNION, ETH_RECEIPT_PROOF);
    case ETH_SSZ_VERIFY_LOGS_PROOF:
      return ARRAY_TYPE(C4_REQUEST_PROOFS_UNION, &ETH_LOGS_BLOCK_CONTAINER);
    case ETH_SSZ_VERIFY_CALL_PROOF:
      return ARRAY_TYPE(C4_REQUEST_PROOFS_UNION, ETH_CALL_PROOF);
    case ETH_SSZ_VERIFY_SYNC_PROOF:
      return ARRAY_TYPE(C4_REQUEST_PROOFS_UNION, ETH_SYNC_PROOF);
    case ETH_SSZ_VERIFY_BLOCK_PROOF:
      return ARRAY_TYPE(C4_REQUEST_PROOFS_UNION, ETH_BLOCK_PROOF);
    case ETH_SSZ_VERIFY_STATE_PROOF:
      return &ETH_STATE_PROOF_CONTAINER;
    case ETH_SSZ_DATA_NONE:
      return C4_REQUEST_DATA_UNION;
    case ETH_SSZ_DATA_HASH32:
      return C4_REQUEST_DATA_UNION + 1;
    case ETH_SSZ_DATA_BYTES:
      return C4_REQUEST_DATA_UNION + 2;
    case ETH_SSZ_DATA_UINT256:
      return C4_REQUEST_DATA_UNION + 3;
    case ETH_SSZ_DATA_TX:
      return C4_REQUEST_DATA_UNION + 4;
    case ETH_SSZ_DATA_RECEIPT:
      return C4_REQUEST_DATA_UNION + 5;
    case ETH_SSZ_DATA_LOGS:
      return C4_REQUEST_DATA_UNION + 6;
    case ETH_SSZ_DATA_BLOCK:
      return C4_REQUEST_DATA_UNION + 7;
    default: return NULL;
  }
}
