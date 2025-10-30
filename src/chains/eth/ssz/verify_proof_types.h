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

// : Ethereum
//
// The Ethereum Mainnet uses a Execution Layer and and a Consensys Layer (Beacon Chain). This allows us to verify the execution layer data with the beacon chain data.
// So all proofs will contain at least the BeaconBlockHeader and Signature from the BeaconChain. Depending on the requested Data additional Merkle Proofs within the BeaconChain and the ExecutionLayer are added.
// These Proofs aim at verifying all relevant ethereum [RPC-methods](ethereum/supported-rpc-methods.md).
// This includes the stateRoot proof, the storage proof, the receipt proof, the logs proof, the transaction proof, the account proof, the code proof and the sync proof.

// definition of an enum depending on the requested block
static const ssz_def_t ETH_STATE_BLOCK_UNION[] = {
    SSZ_NONE,                 // no block-proof for latest
    SSZ_BYTES32("blockHash"), // proof for the right blockhash
    SSZ_UINT64("blockNumber") // proof for the right blocknumber
};

// :: Header Proof
//
// When creating the proof, we always need the header containing the state_root and the body_root, so we proof against those values. But we also need to verify the
// BeaconBlockHeader.
//
// There are 3 different ways to proof the BeaconBlockHeader

// a Signature Proof simply contains the BLS signature of the sync committee for the header to verify.
static const ssz_def_t ETH_SIGNATURE_BLOCK_PROOF[] = {
    SSZ_BIT_VECTOR("sync_committee_bits", 512),     // the bits of the validators that signed the header close to head
    SSZ_BYTE_VECTOR("sync_committee_signature", 96) // the signature of the sync committee
};

// Since Clients usually have the public keys of the last sync period and are able to verify blocks, verifying a ollder block gets complicated, because you would need the public keys of the sync committee at that period, which ar hardly available.
// In order to allow the verification of those historic blocks, we can use the the historic summaries of the current state.
//
// 1. take the blockroot to verify and together with the all the 8192 blockroots of that period and cretae a merkle proof for this list.
// 2. using the current state, we get the list of all historic summaries holding the summar block_rooots of those lists and continue the merkle proof to the tree root hash of thid list.
// 3. we then continue the merkle proof fomr the has tree root of the historic_summaries down to the state_root.
// 4. with the blockheader of the current block associated with this state and mathching the state_root with the root of the merkler proof, we add the BLS-Signature of the sync_committee which can be easily verified by the client.
//
// **Building the historic proof**
//
// In order to build a historic proof, we need data, which can not be provided directly by the standard beacon api. At the time of writing, only lodestar offers an endpoint providing the merkle proof and the the full list of historical summaries at [/eth/v1/lodestar/states/{state_id}/historical_summaries](https://github.com/ChainSafe/lodestar/blob/d8bc6b137888ca1114f7db4d5af9afb04fe00d85/packages/api/src/beacon/routes/lodestar.ts#L418).
// For the blockroots itself, of course you get each single blockroot for all 8192 blocks of the period so you can build the merkle proof with a lot of requests to the header-endpoint, but this would take very long,
// so fetching them all and caching all blockroots allows to build them fast and efficient. Those blockroots are then stored in the chain_store under `data/{chain_id}/{period}/blocks.ssz`. When starting the prover with the -d option, it will use the fetched data.

// a proof using the historic summaries
static const ssz_def_t ETH_HISTORIC_BLOCK_PROOF[] = {
    SSZ_LIST("proof", ssz_bytes32, 128),            // merkle proof from thr blotroot over the historic_summaries to the state
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),   // the header of the beacon block containing historic_summaries (usually close to head)
    SSZ_UINT64("gindex"),                           // the combined gindex of the proof
    SSZ_BIT_VECTOR("sync_committee_bits", 512),     // the bits of the validators that signed the header containing the historic_summaries
    SSZ_BYTE_VECTOR("sync_committee_signature", 96) // the signature of the sync committee
};

static const ssz_def_t PROOF_HEADER[5];

static const ssz_def_t PROOF_HEADER_CONTAINER = SSZ_CONTAINER("ProofHeader", PROOF_HEADER);

// If the header we want to proof is slightly older than the sync period, where the user has the key, the easiest way to proof it,
// is by providing a chain of header from the header for the data up to a header where the user has the keys of the sync committee.
// Header proof is a proof, using a list of following headers to verify a block in the past with a later header holding a signature.
static const ssz_def_t ETH_HEADERS_BLOCK_PROOF[] = {
    SSZ_LIST("headers", PROOF_HEADER_CONTAINER, 128), // list of headers
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),     // the header of the beacon block containing the signature
    SSZ_BIT_VECTOR("sync_committee_bits", 512),       // the bits of the validators that signed the header close to head
    SSZ_BYTE_VECTOR("sync_committee_signature", 96)   // the signature of the sync committee
};

// a header structures used for a chain of headers in the Header Proof, by representing a header without the parentRoot used.
static const ssz_def_t PROOF_HEADER[5] = {
    SSZ_UINT64("slot"),          // the slot of the block or blocknumber
    SSZ_UINT64("proposerIndex"), // the index of the validator proposing the block
    SSZ_BYTES32("stateRoot"),    // the hash_tree_root of the state at the end of the block
    SSZ_BYTES32("bodyRoot")};    // the hash_tree_root of the block body

static const ssz_def_t ETH_HEADER_PROOFS_UNION[] = {
    SSZ_CONTAINER("signature_proof", ETH_SIGNATURE_BLOCK_PROOF), // proof fby provding signature of the sync_committee
    SSZ_CONTAINER("historic_proof", ETH_HISTORIC_BLOCK_PROOF),   // proof for a historic block using the state_root of a current block.
    SSZ_CONTAINER("header_proof", ETH_HEADERS_BLOCK_PROOF)       // proof block giving headers up to a verifyable header.
};

// :: Receipt Proof
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
    SSZ_BYTES("transaction", 1073741824),                // the raw transaction payload
    SSZ_UINT32("transactionIndex"),                      // the index of the transaction in the block
    SSZ_UINT64("blockNumber"),                           // the number of the execution block containing the transaction
    SSZ_BYTES32("blockHash"),                            // the blockHash of the execution block containing the transaction
    SSZ_LIST("receipt_proof", ssz_bytes_1024, 64),       // the Merklr Patricia Proof of the transaction receipt ending in the receipt root
    SSZ_LIST("block_proof", ssz_bytes32, 64),            // the multi proof of the transaction, receipt_root,blockNumber and blockHash
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),        // the header of the beacon block
    SSZ_UNION("header_proof", ETH_HEADER_PROOFS_UNION)}; // the proof for the correctness of the header

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
    SSZ_BYTES("transaction", 1073741824),   // the raw transaction payload
    SSZ_UINT32("transactionIndex"),         // the index of the transaction in the block
    SSZ_LIST("proof", ssz_bytes_1024, 256), // the Merklr Patricia Proof of the transaction receipt ending in the receipt root
};
static const ssz_def_t ETH_LOGS_TX_CONTAINER = SSZ_CONTAINER("LogsTx", ETH_LOGS_TX);

// a single Block with its proof the all the receipts or txs required to proof for the logs.
static const ssz_def_t ETH_LOGS_BLOCK[] = {
    SSZ_UINT64("blockNumber"),                          // the number of the execution block containing the transaction
    SSZ_BYTES32("blockHash"),                           // the blockHash of the execution block containing the transaction
    SSZ_LIST("proof", ssz_bytes32, 1024),               // the multi proof of the transaction, receipt_root,blockNumber and blockHash
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),       // the header of the beacon block
    SSZ_UNION("header_proof", ETH_HEADER_PROOFS_UNION), // the proof for the correctness of the header
    SSZ_LIST("txs", ETH_LOGS_TX_CONTAINER, 256)};       // the transactions of the block

static const ssz_def_t ETH_LOGS_BLOCK_CONTAINER = SSZ_CONTAINER("LogsBlock", ETH_LOGS_BLOCK);

// :: Transaction Proof
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
    SSZ_BYTES("transaction", 1073741824),              // the raw transaction payload
    SSZ_UINT32("transactionIndex"),                    // the index of the transaction in the block
    SSZ_UINT64("blockNumber"),                         // the number of the execution block containing the transaction
    SSZ_BYTES32("blockHash"),                          // the blockHash of the execution block containing the transaction
    SSZ_UINT64("baseFeePerGas"),                       // the baseFeePerGas
    SSZ_LIST("proof", ssz_bytes32, 64),                // the multi proof of the transaction, blockNumber and blockHash
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),      // the header of the beacon block
    SSZ_UNION("header_proof", ETH_HEADER_PROOFS_UNION) // the proof for the correctness of the header
};

// :: Account Proof
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
    SSZ_UNION("block", ETH_STATE_BLOCK_UNION),         // the block to be proven
    SSZ_LIST("proof", ssz_bytes32, 256),               // the merkle prooof from the executionPayload.state down to the blockBodyRoot hash
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),      // the header of the beacon block
    SSZ_UNION("header_proof", ETH_HEADER_PROOFS_UNION) // the proof for the correctness of the header
};
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

// :: Call Proof
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

// :: Sync Proof
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
    SSZ_UINT64("slot"),                            // the slot of the block
    SSZ_UINT64("proposerIndex"),
    SSZ_LIST("proof", ssz_bytes32, 256) // proof merkle proof from the signing root to the pubkeys of the next_synccommittee
};

static const ssz_def_t ETH_EXECUTION_PAYLOAD_UNION[] = {
    SSZ_CONTAINER("DenepExecutionPayload", DENEP_EXECUTION_PAYLOAD),
    SSZ_CONTAINER("GnosisExecutionPayload", GNOSIS_EXECUTION_PAYLOAD),

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
    SSZ_UNION("executionPayload", ETH_EXECUTION_PAYLOAD_UNION), // the merkle prooof from the executionPayload.state down to the blockBodyRoot hash
    SSZ_LIST("proof", ssz_bytes32, 256),                        // the merkle prooof from the executionPayload.state down to the blockBodyRoot hash
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),               // the header of the beacon block
    SSZ_UNION("header_proof", ETH_HEADER_PROOFS_UNION)};        // the proof for the correctness of the header

// for `eth_blockNumber` we need to proof the blocknumber and the timestamp of the latest block.
static const ssz_def_t ETH_BLOCK_NUMBER_PROOF[] = {
    SSZ_UINT64("blockNumber"),                           // the block number of the latest block
    SSZ_UINT64("timestamp"),                             // the timestamp of the latest block
    SSZ_LIST("proof", ssz_bytes32, 256),                 // the multi merkle prooof from the executionPayload.blockNumber and executionPayload.timestamp  down to the blockBodyRoot hash
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),        // the header of the beacon block
    SSZ_UNION("header_proof", ETH_HEADER_PROOFS_UNION)}; // the proof for the correctness of the header
