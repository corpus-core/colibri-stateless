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
// The Ethereum Mainnet consists of two interconnected layers: the Execution Layer and the Consensus Layer (Beacon Chain).
// This separation enables verification of execution-layer data through consensus-layer proofs.
//
// Every proof generated for Ethereum includes, at minimum, the BeaconBlockHeader and its BLS aggregate signature from the Beacon Chain, ensuring the consensus validity of the referenced execution block.
// Depending on the requested data, additional Merkle proofs from both the Beacon Chain and the Execution Layer are appended.
//
// These proof structures are designed to enable full verification of data accessible through common Ethereum [RPC-methods](ethereum/supported-rpc-methods.md).
// Supported proof types include:
// * StateRoot Proof
// * Storage Proof
// * Receipt Proof
// * Logs Proof
// * Transaction Proof
// * Account Proof
// * Code Proof
// * Sync Proof
//
// Together, these proofs establish a framework for stateless, verifiable access to all critical Ethereum state components without reliance on trusted RPC endpoints.


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

// A Signature Proof simply contains the BLS signature of the sync committee for the header to verify.
static const ssz_def_t ETH_SIGNATURE_BLOCK_PROOF[] = {
    SSZ_BIT_VECTOR("sync_committee_bits", 512),     // the bits of the validators that signed the header close to head
    SSZ_BYTE_VECTOR("sync_committee_signature", 96) // the signature of the sync committee
};

// Since Clients usually have the public keys of the last sync period and are able to verify blocks, verifying a ollder block gets complicated, because you would need the public keys of the sync committee at that period, which ar hardly available.
// In order to allow the verification of those historic blocks, we can use the the historic summaries of the current state.
//
// 1. **Block Root Inclusion:**  
//    Start with the target `block_root` to verify.  
//    Combine it with all other 8192 block roots from the same period and generate a Merkle proof proving inclusion within that period’s block root list.
//
// 2. **Historical Summary Proof:**  
//    Using the current BeaconState, locate the corresponding **HistoricalSummary**, which holds the summarized root (`summary_root`) of that 8192-block list.  
//    Extend the Merkle proof to show inclusion of this summary in the **historical_summaries** tree.
//
// 3. **State Root Proof:**  
//    Continue the Merkle proof from the `historical_summaries` tree up to the `state_root` of the BeaconState.  
//    This step links the historical proof chain to the current verified state.
//
// 4. **Consensus Verification:**  
//    Finally, use the BeaconBlockHeader associated with the current state.  
//    Match the derived `state_root` with the one referenced in the block header.  
//    Then verify the **BLS signature** of the Sync Committee corresponding to that block header.  
//    This signature confirms the authenticity of the BeaconBlock and thus of the complete historical proof chain.
//
// **Building the historic proof**
//
// In order to build a historic proof, we need data, which can not be provided directly by the standard beacon api. At the time of writing, only lodestar offers an endpoint providing the merkle proof and the full list of historical summaries at [/eth/v1/lodestar/states/{state_id}/historical_summaries](https://github.com/ChainSafe/lodestar/blob/d8bc6b137888ca1114f7db4d5af9afb04fe00d85/packages/api/src/beacon/routes/lodestar.ts#L418).
//
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
// A **Receipt Proof** represents the cryptographic verification of a transaction receipt and its inclusion within the canonical blockchain structure.
//
// 1. **Receipt Merkle Proof:**
//    All transaction receipts of an execution block are serialized into a **Patricia Merkle Trie**.
//    A Merkle proof is generated for the requested receipt, demonstrating its inclusion in the block’s `receiptsRoot`.
// 2. **Transaction–Receipt Association:**
//    The **payload of the transaction** is used to compute its **SSZ hash tree root** derived from the corresponding **BeaconBlock**.
//    This step ensures that the receipt is cryptographically linked to the correct transaction hash.
// 3. **Execution Payload Proof:**
//    An **SSZ multi–Merkle proof** is then created, connecting the `transactions`, `receipts`, `blockNumber`, and `blockHash` fields within the **ExecutionPayload** to the `blockBodyRoot`.
//    The total proof depth for this structure is **29**.
// 4. **Consensus Reference:**
//    The **BeaconBlockHeader** is included in the proof to provide the `slot` information.
//    This slot determines which sync committee is responsible for signing the corresponding block root.
// 5. **Sync Committee Signature:**
//    Finally, the **BLS aggregate signature** from the sync committee of the **following block** is verified.
//    The signature covers the block root as part of the `SignData`, with the signing domain derived from the fork version and the **Genesis Validator Root**.
//    Successful signature verification confirms that the block—and thus the contained receipt—is part of the canonical chain.
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

// The main proof data for a receipt.
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
// A **Logs Proof** verifies that specific log entries, returned by `eth_getLogs`, are correctly included within transaction receipts of a verified execution block.
//
// 1. **Transaction Root Calculation:**
//   For each transaction producing a log entry, the **transaction payload** is used to compute its **SSZ hash tree root**.
// 2. **Execution Payload Proof:**
//    An **SSZ Merkle proof** is constructed, linking the `transactions` field within the **ExecutionPayload** to the `blockBodyRoot`.
//    The total proof depth for this structure is **29**.
// 3. **Consensus Reference:**
//    The **BeaconBlockHeader** is included in the proof to provide the `slot` information.
//    This identifies which sync committee is responsible for signing the corresponding block root.
// 4. **Sync Committee Signature:**
//   The **BLS aggregate signature** of the **following block’s** sync committee is verified against the `SignData` that includes the block hash.
//   The signing domain is derived from the fork version and the **Genesis Validator Root**.
//   Successful verification confirms that the block—and therefore all contained receipts and logs—is part of the canonical chain.
//
// Each log proof must reference its corresponding **receipt proof**, ensuring that every verified log entry is linked to a valid transaction and included in a verified execution block.

// Represents one single transaction receipt with the required transaction and receipt-proof.
// The proof contains the raw receipt as part of its last leaf.
static const ssz_def_t ETH_LOGS_TX[] = {
    SSZ_BYTES("transaction", 1073741824),   // the raw transaction payload
    SSZ_UINT32("transactionIndex"),         // the index of the transaction in the block
    SSZ_LIST("proof", ssz_bytes_1024, 256), // the Merkle Patricia Proof of the transaction receipt ending in the receipt root
};
static const ssz_def_t ETH_LOGS_TX_CONTAINER = SSZ_CONTAINER("LogsTx", ETH_LOGS_TX);

// A single Block with its proof containing all the receipts or txs required to proof for the logs.
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
// A Transaction Proof represents the verification of a specific transaction and its inclusion within a verified execution block.
//	1.	Transaction Payload Root:
// The payload of the transaction is used to compute its SSZ hash tree root, establishing a deterministic reference to the transaction within the block.
//	2.	Execution Payload Proof:
// An SSZ Merkle proof links the transactions field of the ExecutionPayload to the blockBodyRoot.
// The total proof depth for this structure is 29.
//	3.	Consensus Reference:
// The BeaconBlockHeader is included in the proof to provide the slot information, which determines the sync committee period responsible for signing the corresponding block root.
//	4.	Sync Committee Signature:
// The BLS aggregate signature from the sync committee of the following block is verified against the SignData containing the block hash.
// The signing domain is derived from the fork version and the Genesis Validator Root, ensuring that the transaction originates from a block that is part of the canonical chain.
//
// The Transaction Proof confirms the inclusion and authenticity of a transaction without requiring full synchronization with the blockchain state.
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

// The main proof data for a single transaction.
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
// An Acccount Proof represents the account and storage values, including the Merkle proof, of the specified account.
//
// 1. **Execution-Layer Proof**  
//    A **Patricia Merkle Proof** is constructed for the account object in the execution layer.  
//    This proof includes the account’s `balance`, `nonce`, `codeHash`, and `storageRoot`, as well as separate proofs for all accessed storage keys.  
//    The resulting root of this proof corresponds to the block’s **stateRoot**.  
//    (Equivalent to the data returned by `eth_getProof`.)
// 
// 2. **State Proof**  
//    An **SSZ Merkle Proof** links the `stateRoot` from the execution layer to the **ExecutionPayload**, and further through the **BeaconBlockBody** to its root hash, which is included in the **BeaconBlockHeader**.
//
// 3. **Consensus Reference**  
//    The **BeaconBlockHeader** is included in the proof to provide the `slot` information, which identifies the sync committee period responsible for signing the corresponding block root.
//
// 4. **Sync Committee Signature**  
//    The **BLS aggregate signature** from the sync committee of the **following block** is verified against the `SignData` containing the block hash.  
//    The signing domain is derived from the fork version and the **Genesis Validator Root**, confirming that the account data originates from a block included in the canonical chain.
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

// The stateRoot proof is used as part of different other types since it contains all relevant
// proofs to validate the stateRoot of the execution layer.
static const ssz_def_t ETH_STATE_PROOF[] = {
    SSZ_UNION("block", ETH_STATE_BLOCK_UNION),         // the block to be proven
    SSZ_LIST("proof", ssz_bytes32, 256),               // the merkle prooof from the executionPayload.state down to the blockBodyRoot hash
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),      // the header of the beacon block
    SSZ_UNION("header_proof", ETH_HEADER_PROOFS_UNION) // the proof for the correctness of the header
};
static const ssz_def_t ETH_STATE_PROOF_CONTAINER = SSZ_CONTAINER("StateProof", ETH_STATE_PROOF);

// Represents the storage proof of a key. The value can be taken from the last entry, which is the leaf of the proof.
static const ssz_def_t ETH_STORAGE_PROOF[] = {
    SSZ_BYTES32("key"),                      // the key to be proven
    SSZ_LIST("proof", ssz_bytes_1024, 1024), // Patricia merkle proof
};

static const ssz_def_t ETH_STORAGE_PROOF_CONTAINER = SSZ_CONTAINER("StorageProof", ETH_STORAGE_PROOF);

// The main proof data for an account.
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
// `eth_call` returns the result of a smart contract call.  
// To verify that this result is correct, every referenced account, contract code, and storage value must be validated
// against the canonical chain state.
//
// 1. **Execution-Layer Proof**  
//    A **Patricia Merkle Proof** is constructed for each involved account and all accessed storage values in the execution layer.  
//    For every account, this includes the `balance`, `nonce`, `codeHash`, and `storageRoot`, as well as the specific storage slots read or modified during the call.  
//    Each of these elements is verified through its corresponding Merkle proof, resulting in a verified **stateRoot** for the execution block.  
//    (Equivalent to the combined data returned by `eth_getProof` for all accounts and storage keys involved.)
// 
// 2. **State Proof**  
//    An **SSZ Merkle Proof** connects the `stateRoot` of the execution layer to the **ExecutionPayload**,  
//    and continues through the **BeaconBlockBody** to its root hash, which is referenced in the **BeaconBlockHeader**.
//
// 3. **Consensus Reference**  
//    The **BeaconBlockHeader** is included in the proof to provide the `slot` information.  
//    This determines which sync committee is responsible for signing the corresponding block root.
//
// 4. **Sync Committee Signature**  
//    The **BLS aggregate signature** from the sync committee of the **following block** is verified  
//    against the `SignData` that includes the block hash.  
//    The signing domain is derived from the fork version and the **Genesis Validator Root**,  
//    confirming that the block and its execution state belong to the canonical chain.
//
// The **Call Proof** provides full verifiability of `eth_call` results by cryptographically proving all involved account and storage states without reliance on any RPC provider.
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

// A proof for a single account.
static const ssz_def_t ETH_CALL_ACCOUNT[] = {
    SSZ_LIST("accountProof", ssz_bytes_1024, 256),               // Patricia merkle proof
    SSZ_ADDRESS("address"),                                      // the address of the account
    SSZ_UNION("code", ETH_CODE_UNION),                           // the code of the contract
    SSZ_LIST("storageProof", ETH_STORAGE_PROOF_CONTAINER, 4096), // the storage proofs of the selected
};
static const ssz_def_t ETH_CALL_ACCOUNT_CONTAINER = SSZ_CONTAINER("EthCallAccount", ETH_CALL_ACCOUNT);

// The main proof data for a call.
static const ssz_def_t ETH_CALL_PROOF[] = {
    SSZ_LIST("accounts", ETH_CALL_ACCOUNT_CONTAINER, 256), // used accounts
    SSZ_CONTAINER("state_proof", ETH_STATE_PROOF)};        // the state proof of the account

// :: Sync Proof
//
// The **Sync Proof** serves as input data for verifying a sync committee transition,  
// typically used within zero-knowledge proof systems (zk).  
// It is a compact representation derived from the **Light Client Update** structure.
//
// The proof is constructed as a **Merkle proof** using a given `gindex` (generalized index).  
// It verifies inclusion starting from the hash of a validator’s public key all the way up to the **signing root**.  
// This ensures that the participating validator’s public key is part of the sync committee that signed a specific block.
//
// The following diagram illustrates the structure of the Merkle tree leading to the **SigningRoot**:
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
//
// The **Sync Proof** allows cryptographic verification of validator membership in the active sync committee  
// without requiring the entire committee set, reducing proof size and improving zk-efficiency.

// The **Sync Proof** is a compact representation of the **Light Client Update** structure.
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
// The **Block Proof** verifies that a specific block in the execution layer is valid  
// and correctly referenced by the consensus layer (Beacon Chain).
//
// 1. **Execution Block Proof**  
//    A Merkle proof is generated for the block’s core fields (`blockNumber`, `blockHash`, `transactionsRoot`, `stateRoot`, `receiptsRoot`)  
//    within the **ExecutionPayload**. This ensures that all block data is included and consistent with the execution layer’s state.
//
// 2. **Payload–Header Link**  
//    An **SSZ Merkle Proof** connects the **ExecutionPayload** to the `blockBodyRoot`,  
//    and continues through the **BeaconBlockHeader**, proving that the execution block is part of the verified beacon block.
//
// 3. **Consensus Reference**  
//    The **BeaconBlockHeader** provides the `slot` context used to identify the correct sync committee for signature verification.
//
// 4. **Sync Committee Signature**  
//    The **BLS aggregate signature** from the sync committee of the **following block** is verified  
//    against the `SignData` that includes the beacon block root.  
//    The signing domain is derived from the fork version and the **Genesis Validator Root**,  
//    confirming that the block and its associated execution payload belong to the canonical chain.
//
// The **Block Proof** thus establishes full trustless verification of an execution-layer block  
// by cryptographically linking it to the verified consensus layer.

// The stateRoot proof is used as part of different other types since it contains all relevant
// proofs to validate the stateRoot of the execution layer
static const ssz_def_t ETH_BLOCK_PROOF[] = {
    SSZ_UNION("executionPayload", ETH_EXECUTION_PAYLOAD_UNION), // the merkle prooof from the executionPayload.state down to the blockBodyRoot hash
    SSZ_LIST("proof", ssz_bytes32, 256),                        // the merkle prooof from the executionPayload.state down to the blockBodyRoot hash
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),               // the header of the beacon block
    SSZ_UNION("header_proof", ETH_HEADER_PROOFS_UNION)};        // the proof for the correctness of the header

// Tor `eth_blockNumber` we need to proof the blocknumber and the timestamp of the latest block.
static const ssz_def_t ETH_BLOCK_NUMBER_PROOF[] = {
    SSZ_UINT64("blockNumber"),                           // the block number of the latest block
    SSZ_UINT64("timestamp"),                             // the timestamp of the latest block
    SSZ_LIST("proof", ssz_bytes32, 256),                 // the multi merkle prooof from the executionPayload.blockNumber and executionPayload.timestamp  down to the blockBodyRoot hash
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),        // the header of the beacon block
    SSZ_UNION("header_proof", ETH_HEADER_PROOFS_UNION)}; // the proof for the correctness of the header
