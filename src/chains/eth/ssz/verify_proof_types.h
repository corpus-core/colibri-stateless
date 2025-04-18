#include "beacon_types.h"
#include "ssz.h"

// # Ethereum Execution Proofs
//
// The Execution Layer of Ethereum depends on the Beacon Chain.
// The Beacon Chain is the consensus layer of Ethereum. These Proofs aim at providing proofs for the ethereum RPC-API.
// This includes the stateRoot proof, the storage proof, the receipt proof, the logs proof, the transaction proof, the account proof, the code proof and the sync proof.

// definition of an enum depending on the requested block
static const ssz_def_t ETH_STATE_BLOCK_UNION[] = {
    SSZ_NONE,                 // no block-proof for latest
    SSZ_BYTES32("blockHash"), // proof for the right blockhash
    SSZ_UINT64("blockNumber") // proof for the right blocknumber
};

// ## Receipt Proof
//
// represents the proof for a transaction receipt
//
// 1. All Receipts of the execution blocks are serialized into a Patricia Merkle Trie and the merkle proof is created for the requested receipt.
// 2. The **payload of the transaction** is used to create its SSZ Hash Tree Root from the BeaconBlock. This is needed in order to verify that the receipt actually belongs to the given transactionhash.
// 3. The **SSZ Multi Merkle Proof** from the Transactions, Receipts, BlockNumber and BlockHash of the ExecutionPayload to the BlockBodyRoot. (Total Depth: 29)
// 4. **BeaconBlockHeader** is passed because also need the slot in order to find out which period and which sync committee is used.
// 5. **Signature of the SyncCommittee** (taken from the following block) is used to verify the SignData where the blockhash is part of the message and the Domain is calculated from the fork and the Genesis Validator Root.
//
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

// the main proof data for a receipt.
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

// ## Logs Proof
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
    SSZ_BYTES("transaction", 1073741824),  // the raw transaction payload
    SSZ_UINT32("transactionIndex"),        // the index of the transaction in the block
    SSZ_LIST("proof", ssz_bytes_1024, 64), // the Merklr Patricia Proof of the transaction receipt ending in the receipt root
};
static const ssz_def_t ETH_LOGS_TX_CONTAINER = SSZ_CONTAINER("LogsTx", ETH_LOGS_TX);

// a single Block with its proof the all the receipts or txs required to proof for the logs.
static const ssz_def_t ETH_LOGS_BLOCK[] = {
    SSZ_UINT64("blockNumber"),                       // the number of the execution block containing the transaction
    SSZ_BYTES32("blockHash"),                        // the blockHash of the execution block containing the transaction
    SSZ_LIST("proof", ssz_bytes32, 64),              // the multi proof of the transaction, receipt_root,blockNumber and blockHash
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),    // the header of the beacon block
    SSZ_BIT_VECTOR("sync_committee_bits", 512),      // the bits of the validators that signed the block
    SSZ_BYTE_VECTOR("sync_committee_signature", 96), // the signature of the sync committee
    SSZ_LIST("txs", ETH_LOGS_TX_CONTAINER, 256)};    // the transactions of the block

static const ssz_def_t ETH_LOGS_BLOCK_CONTAINER = SSZ_CONTAINER("LogsBlock", ETH_LOGS_BLOCK);

// ## Transaction Proof
//
// represents the account and storage values, including the Merkle proof, of the specified account.
//
// 1. The **payload of the transaction** is used to create its SSZ Hash Tree Root.
// 2. The **SSZ Merkle Proof** from the Transactions of the ExecutionPayload to the BlockBodyRoot. (Total Depth: 29)
// 3. **BeaconBlockHeader** is passed because also need the slot in order to find out which period and which sync committee is used.
// 4. **Signature of the SyncCommittee** (taken from the following block) is used to verify the SignData where the blockhash is part of the message and the Domain is calculated from the fork and the Genesis Validator Root.
//
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

// the main proof data for a single transaction.
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

// ## Account Proof
//
// represents the account and storage values, including the Merkle proof, of the specified account.
//
// 1. **Patricia Merkle Proof** for the Account Object in the execution layer (balance, nonce, codeHash, storageHash) and the storage values with its own Proofs. (using eth_getProof): Result StateRoot
// 2. **State Proof** is a SSZ Merkle Proof from the StateRoot to the ExecutionPayload over the BeaconBlockBody to its root hash which is part of the header.
// 3. **BeaconBlockHeader** is passed because also need the slot in order to find out which period and which sync committee is used.
// 4. **Signature of the SyncCommittee** (taken from the following block) is used to verify the SignData where the blockhash is part of the message and the Domain is calculated from the fork and the Genesis Validator Root.
//
// ```mermaid
// flowchart TB
//     subgraph "ExecutionLayer"
//         subgraph "Account"
//             balance --> account
//             nonce --> account
//             codeHash --> account
//             storageHash --> account
//         end
//
//         subgraph "Storage"
//             key1 --..PM..-->storageHash
//             key2 --..PM..-->storageHash
//             key3 --..PM..-->storageHash
//         end
//     end
//
//     subgraph "ConsensusLayer"
//         subgraph "ExecutionPayload"
//             account --..PM..--> stateRoot
//         end
//
//         subgraph "BeaconBlockBody"
//             stateRoot --SSZ D:5--> executionPayload
//             m[".."]
//         end
//
//         subgraph "BeaconBlockHeader"
//             slot
//             proposerIndex
//             parentRoot
//             s[stateRoot]
//             executionPayload  --SSZ D:4--> bodyRoot
//         end
//
//     end
//     classDef transparentStyle fill:transparent
//     class ExecutionLayer transparentStyle
//     class ConsensusLayer transparentStyle
// ```

// the stateRoot proof is used as part of different other types since it contains all relevant
// proofs to validate the stateRoot of the execution layer
static const ssz_def_t ETH_STATE_PROOF[] = {
    SSZ_UNION("block", ETH_STATE_BLOCK_UNION),        // the block to be proven
    SSZ_LIST("proof", ssz_bytes32, 256),              // the merkle prooof from the executionPayload.state down to the blockBodyRoot hash
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),     // the header of the beacon block
    SSZ_BIT_VECTOR("sync_committee_bits", 512),       // the bits of the validators that signed the block
    SSZ_BYTE_VECTOR("sync_committee_signature", 96)}; // the signature of the sync committee

static const ssz_def_t ETH_STATE_PROOF_CONTAINER = SSZ_CONTAINER("StateProof", ETH_STATE_PROOF);

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
    SSZ_CONTAINER("state_proof", ETH_STATE_PROOF)};             // the state proof of the account

static const ssz_def_t ETH_CODE_UNION[] = {
    SSZ_BOOLEAN("code_used"),   // no code delivered
    SSZ_BYTES("code", 4194304), // the code of the contract
};

// ## Call Proof
//
// eth_call returns the result of the call. In order to proof that the result is correct, we need
// to proof every single storage value and account..
//
// 1. **Patricia Merkle Proof** for the Account Object in the execution layer (balance, nonce, codeHash, storageHash) and the storage values with its own Proofs. (using eth_getProof): Result StateRoot
// 2. **State Proof** is a SSZ Merkle Proof from the StateRoot to the ExecutionPayload over the BeaconBlockBody to its root hash which is part of the header.
// 3. **BeaconBlockHeader** is passed because also need the slot in order to find out which period and which sync committee is used.
// 4. **Signature of the SyncCommittee** (taken from the following block) is used to verify the SignData where the blockhash is part of the message and the Domain is calculated from the fork and the Genesis Validator Root.
//
// ```mermaid
// flowchart TB
//     subgraph "ExecutionLayer"
//         class ExecutionLayer transparent
//
//         subgraph "Account"
//             balance --> account
//             nonce --> account
//             codeHash --> account
//             storageHash --> account
//         end
//
//         subgraph "Storage"
//             key1 --..PM..-->storageHash
//             key2 --..PM..-->storageHash
//             key3 --..PM..-->storageHash
//         end
//     end
//
//     subgraph "ConsensusLayer"
//         subgraph "ExecutionPayload"
//             account --..PM..--> stateRoot
//         end
//
//         subgraph "BeaconBlockBody"
//             stateRoot --SSZ D:5--> executionPayload
//             m[".."]
//         end
//
//         subgraph "BeaconBlockHeader"
//             slot
//             proposerIndex
//             parentRoot
//             s[stateRoot]
//             executionPayload  --SSZ D:4--> bodyRoot
//         end
//
//     end
//     classDef transparentStyle fill:transparent
//     class ExecutionLayer transparentStyle
//     class ConsensusLayer transparentStyle
//
// ```

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
    SSZ_CONTAINER("state_proof", ETH_STATE_PROOF)};        // the state proof of the account

// ## Sync Proof
//
//
// Proof as input data for the sync committee transition used by zk. This is a very compact proof mostly taken from the light client update.
// the proof itself is a merkle proof using the given gindex to verify from the hash of the pubkey all the way down to the signing root.
//
// The following diagram shows the Structure of the Merkle Tree leading to the SigningRoot:
//
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
//     blockheaderhash ==> SigningRoot
//     Domain --> SigningRoot
//     4{4} ==> blockheaderhash
//     5{5} --> blockheaderhash
//     8{8} --> 4
//     9{9} ==> 4
//     10{10} -.-> 5
//     11{11} -.-> 5
//     Slot -.-> 8
//     proposerIndex -.-> 8
//     parentRoot --> 9
//     stateRoot ==> 9
//     bodyRoot -.-> 10
//     21{"zero"} -.-> 10
//     22{"zero"} -.-> 11
//     23{"zero"} -.-> 11
//
//
//     38{38} --> stateRoot
//     39{39} ==> stateRoot
//
//
//     76{76} -.-> 38
//     77{77} -.-> 38
//     78{78} ==> 39
//     79{79} --> 39
//
//     156{156} -->78
//     157{157} ==>78
//
//     158("...") --> 79
//
//     314{314} --> 157
//     315{315} ==> 157
//
//     finalized_checkpoint -.-> 314
//     inactivity_scores -.-> 314
//     current_sync_committee --> 315
//     next_sync_committee ==> 315
//
//
//     pubkeys ==> next_sync_committee
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

// ## Block Proof
//
// The Block Proof is a proof that the block is valid.
// It is used to verify the block of the execution layer.
//
//
//

// the stateRoot proof is used as part of different other types since it contains all relevant
// proofs to validate the stateRoot of the execution layer
static const ssz_def_t ETH_BLOCK_PROOF[] = {
    SSZ_UNION("executionPayload", ETH_EXECUTION_PAYLOAD_UNION), // the merkle prooof from the executionPayload.state down to the blockBodyRoot hash
    SSZ_LIST("proof", ssz_bytes32, 256),                        // the merkle prooof from the executionPayload.state down to the blockBodyRoot hash
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),               // the header of the beacon block
    SSZ_BIT_VECTOR("sync_committee_bits", 512),                 // the bits of the validators that signed the block
    SSZ_BYTE_VECTOR("sync_committee_signature", 96)};           // the signature of the sync committee
