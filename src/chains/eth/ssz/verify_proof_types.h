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
// Every proof generated for Ethereum includes, at minimum, a verified execution block. The execution
// block is bound to consensus by proving its **block hash** against the `body_root` of a BeaconBlockHeader,
// which is authenticated by the BLS aggregate signature of the sync committee.
//
// Execution-layer values (accounts, storage, transactions, receipts, logs) are then proven against
// the corresponding fields of the RLP-encoded EL header (`stateRoot`, `transactionsRoot`, `receiptsRoot`).
// No SSZ Merkle proof from individual ExecutionPayload fields down to `block_root` is required.
//
// These proof structures are designed to enable full verification of data accessible through common Ethereum [RPC-methods](ethereum/supported-rpc-methods.md).
// Supported proof types include:
// * Header Proof
// * Account Proof
// * Transaction Proof
// * Receipt Proof
// * Logs Proof
// * Call Proof
// * Block Proof
// * Block Receipts Proof
// * Sync Proof
//
// Together, these proofs establish a framework for stateless, verifiable access to all critical Ethereum state components without reliance on trusted RPC endpoints.

// :: Header Proof
//
// Execution-layer data is proven against a verified **execution block hash**, not against
// individual fields of the Beacon `ExecutionPayload`. The shared container for this is
// `EthClBlockProof`:
//
// 1. **EL Header:** The proof carries the RLP-encoded execution-layer header (`elHeader`).
//    `keccak256(elHeader)` is the execution `blockHash`. All header fields used by higher-level
//    proofs (`stateRoot`, `transactionsRoot`, `receiptsRoot`, `blockNumber`, `timestamp`, ...)
//    are taken from this RLP header.
// 2. **Block-hash Merkle Proof:** `blockhashBranch` is an SSZ Merkle proof of that `blockHash`
//    against `clHeader.bodyRoot`. The leaf's generalized index is stored as `gindex` so the
//    same container works across forks:
//    * **Deneb / Electra / Fulu:** `EXECUTION_BLOCK_HASH_GINDEX_DENEB` = **812**
//      (`BeaconBlockBody.execution_payload.block_hash`)
//    * **Gloas (Glamsterdam):** `EXECUTION_BLOCK_HASH_GINDEX_GLOAS` = **2856**
//      (`BeaconBlockBody.signed_execution_payload_bid.message.parent_block_hash`)
//
//    See the [Gloas Light Client Sync Protocol](https://ethereum.github.io/consensus-specs/specs/gloas/light-client/sync-protocol/).
// 3. **Beacon Header:** `clHeader` is the BeaconBlockHeader whose `bodyRoot` is the Merkle root
//    of the proof in step 2.
// 4. **Header Authentication:** `headerProof` authenticates `clHeader` via the sync committee
//    (direct signature, historic summaries, or a short header chain).
//
// If the verifier has already cached this execution header, the proof may use the `blockHash`
// variant of `ETH_BLOCK_PROOF_UNION` instead of a full `EthClBlockProof`.
//
// ```mermaid
// flowchart TB
//     subgraph "Execution Layer"
//         elHeader["RLP EL Header"]
//         blockHash["keccak(elHeader)"]
//         elHeader --> blockHash
//     end
//     subgraph "BeaconBlockBody"
//         blockHash -- "SSZ gindex 812 or 2856" --> bodyRoot
//     end
//     subgraph "BeaconBlockHeader"
//         slot
//         proposerIndex
//         parentRoot
//         s[stateRoot]
//         bodyRoot
//     end
//     subgraph "headerProof"
//         sig["Sync Committee BLS"]
//     end
//     BeaconBlockHeader --> sig
// ```
//
// Independently, the BeaconBlockHeader itself can be proven in 4 ways:

// A Signature Proof simply contains the BLS signature of the sync committee for the header to verify.
static const ssz_def_t ETH_SIGNATURE_BLOCK_PROOF[] = {
    SSZ_BIT_VECTOR("sync_committee_bits", 512),     // the bits of the validators that signed the header close to head
    SSZ_BYTE_VECTOR("sync_committee_signature", 96) // the signature of the sync committee
};

// Since Clients usually have the public keys of the last sync period and are able to verify blocks, verifying an older block gets complicated, because you would need the public keys of the sync committee at that period, which are hardly available.
// In order to allow the verification of those historic blocks, we can use the historic summaries of the current state.
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
    SSZ_LIST("proof", ssz_bytes32, 128),            // merkle proof from the blockroot over the historic_summaries to the state
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),   // the header of the beacon block containing historic_summaries (usually close to head)
    SSZ_UINT64("gindex"),                           // the combined gindex of the proof
    SSZ_BIT_VECTOR("sync_committee_bits", 512),     // the bits of the validators that signed the header containing the historic_summaries
    SSZ_BYTE_VECTOR("sync_committee_signature", 96) // the signature of the sync committee
};

static const ssz_def_t PROOF_HEADER[4];
static const ssz_def_t PROOF_HEADER_CONTAINER = SSZ_CONTAINER("ProofHeader", PROOF_HEADER);

// If the header we want to prove is slightly older than the sync period for which the user has the keys, the easiest way to prove it
// is by providing a chain of headers from the header for the data up to a header where the user has the keys of the sync committee.
// Header proof is a proof using a list of subsequent headers to verify a block in the past with a later header holding a signature.
static const ssz_def_t ETH_HEADERS_BLOCK_PROOF[] = {
    SSZ_LIST("headers", PROOF_HEADER_CONTAINER, 128), // list of headers
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),     // the header of the beacon block containing the signature
    SSZ_BIT_VECTOR("sync_committee_bits", 512),       // the bits of the validators that signed the header close to head
    SSZ_BYTE_VECTOR("sync_committee_signature", 96)   // the signature of the sync committee
};

// a header structures used for a chain of headers in the Header Proof, by representing a header without the parentRoot used.
static const ssz_def_t PROOF_HEADER[4] = {
    SSZ_UINT64("slot"),          // the slot of the block or blocknumber
    SSZ_UINT64("proposerIndex"), // the index of the validator proposing the block
    SSZ_BYTES32("stateRoot"),    // the hash_tree_root of the state at the end of the block
    SSZ_BYTES32("bodyRoot")};    // the hash_tree_root of the block body

// A slim checkpoint proof anchoring the sync committee's currentSyncCommittee
// against the state_root of a recent (checkpointz-servable) BeaconBlockHeader.
//
// Used as the WSP anchor for both ZKSyncData (`checkpoint`) and LCSyncData
// (`bootstrap`): the verifier reconstructs the SyncCommittee root from the
// chain-of-trust pubkeys (ZK proof public input or LCU chain end) plus the
// `aggregate_pubkey` packed here, walks `proof` up to the `header.stateRoot`,
// and then anchors `header` itself against checkpointz.
//
// `proof` is a list rather than a vector because the currentSyncCommittee
// branch depth differs between forks (Deneb = 5, Electra/Fulu = 6, Gloas = 11).
//
// `aggregate_pubkey` is included so the verifier can compute the
// SyncCommittee container root as `SHA256(pubkeys_root || aggregate_padded)`
// without re-aggregating the 512 pubkeys (an expensive BLS operation).
static const ssz_def_t ETH_CHECKPOINT_PROOF[] = {
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER), // anchor header (state_root binds the merkle proof, slot+root anchor against checkpointz)
    SSZ_BYTE_VECTOR("aggregate_pubkey", 48),      // SyncCommittee.aggregate_pubkey for sync_committee_root reconstruction
    SSZ_LIST("proof", ssz_bytes32, 16)            // currentSyncCommitteeBranch (depth 5 in Deneb, 6 in Electra/Fulu, 11 in Gloas)
};

static const ssz_def_t ETH_HEADER_PROOFS_UNION[] = {
    SSZ_CONTAINER("signature_proof", ETH_SIGNATURE_BLOCK_PROOF), // proof by providing the signature of the sync committee
    SSZ_CONTAINER("historic_proof", ETH_HISTORIC_BLOCK_PROOF),   // proof for a historic block using the state_root of a current block
    SSZ_CONTAINER("headerProof", ETH_HEADERS_BLOCK_PROOF),       // proof block giving a chain of headers up to a verifiable header
    SSZ_CONTAINER("CheckpointProof", ETH_CHECKPOINT_PROOF)       // WSP anchor via LightClientBootstrap (currentSyncCommittee branch)
};

// Proof that an RLP execution-layer header belongs to a signed BeaconBlock.
// `keccak256(elHeader)` is proven against `clHeader.bodyRoot` at `gindex`
// (`EXECUTION_BLOCK_HASH_GINDEX_DENEB` = 812, or `EXECUTION_BLOCK_HASH_GINDEX_GLOAS` = 2856 after Glamsterdam).
static const ssz_def_t ETH_CL_BLOCK_PROOF[] = {
    SSZ_PROG_BYTES("elHeader"),                       // RLP-serialized execution-layer header
    SSZ_CONTAINER("clHeader", BEACON_BLOCK_HEADER),   // BeaconBlockHeader whose bodyRoot is the Merkle root of blockhashBranch
    SSZ_PROG_LIST("blockhashBranch", ssz_bytes32),    // SSZ Merkle branch from the execution block hash to bodyRoot
    SSZ_UINT64("gindex"),                             // 812 (Deneb/Electra/Fulu: execution_payload.block_hash) or 2856 (Gloas: signed_execution_payload_bid.message.parent_block_hash)
    SSZ_UNION("headerProof", ETH_HEADER_PROOFS_UNION) // authenticates clHeader
};

// Shared block proof used by account, tx, receipt, logs, call and block proofs.
// Either a full consensus-layer proof, or a hash if the verifier already cached that header.
static const ssz_def_t ETH_BLOCK_PROOF_UNION[] = {
    SSZ_BYTES32("blockHash"),                     // cached: verifier already holds this verified EL header
    SSZ_CONTAINER("clProof", ETH_CL_BLOCK_PROOF), // full consensus-layer proof of the EL header
};

// :: Logs Proof
//
// A **Logs Proof** verifies that specific log entries, returned by `eth_getLogs`, are correctly
// included within transaction receipts of a verified execution block.
//
// 1. **Execution Block:** The execution block is verified via `ETH_BLOCK_PROOF_UNION`
//    (`c4_verify_block`). This yields a verified RLP EL header and its keccak `blockHash`.
// 2. **Receipt Inclusion:** For each transaction that produced a matching log, a Patricia Merkle
//    proof against the header's `receiptsRoot` delivers the raw receipt (the leaf).
// 3. **Transaction Binding:** A Patricia Merkle proof against the header's `transactionsRoot`
//    delivers the raw transaction (used for `transactionHash` and sender recovery).
// 4. **Log Matching:** Each returned log is checked against the decoded receipt logs
//    (`transactionIndex`, `logIndex`, topics, data, address) and bound to the verified
//    `blockHash` / `blockNumber`.
//
// Completeness (that no matching log was omitted over a block range) is a separate proof,
// see `LogsCompletenessProof` below.

// Represents one single transaction receipt with the required transaction and receipt-proof.
// The proof contains the raw receipt as part of its last leaf.
static const ssz_def_t ETH_LOGS_TX[] = {
    SSZ_UINT32("logIndex"),         // the logIndex within the block for the first event of the tx (can only be verified with all previous receipts, which is not the case today)
    SSZ_UINT32("transactionIndex"), // the index of the transaction in the block
};
static const ssz_def_t ETH_LOGS_TX_CONTAINER = SSZ_CONTAINER("LogsTx", ETH_LOGS_TX);

// A single Block with its proof containing all the receipts or txs required to prove the logs.
static const ssz_def_t ETH_LOGS_BLOCK[] = {
    SSZ_UINT64("blockNumber"),                         // the execution block number
    SSZ_PROG_LIST("transactionProof", ssz_bytes_list), // the Patricia Merkle Proof of the transaction, the leaf contains the raw transaction.
    SSZ_PROG_LIST("receiptProof", ssz_bytes_1024),     // the Multi Patricia Merkle Proof of the receipt, the leaf contains the raw receipt.
    SSZ_PROG_LIST("txs", ETH_LOGS_TX_CONTAINER),       // the transactions used by the resulting events
    SSZ_UNION("block", ETH_BLOCK_PROOF_UNION)          // the proof for the execution block containing the transaction
};

static const ssz_def_t ETH_LOGS_BLOCK_CONTAINER = SSZ_CONTAINER("LogsBlock", ETH_LOGS_BLOCK);

// A **Logs Completeness Proof** proves for a contiguous range of execution blocks
// `[fromBlock, toBlock]` that **no** matching log for the `eth_getLogs` filter was
// omitted. The normal Logs Proof only proves inclusion of the returned logs; this
// proof additionally guarantees completeness.
//
// **Continuity.** The newest block (`toBlock`) is proven via the shared
// `ETH_BLOCK_PROOF_UNION` (`c4_verify_block`), which yields a verified RLP execution
// header and its keccak `blockHash`. Every older block in the range is a raw RLP
// header chained by `parentHash`: `keccak(headers[i]) == parentHash(headers[i+1])`
// (and `keccak(headers[last]) == parentHash(anchor)`), together with a gap-free
// `blockNumber` sequence `fromBlock..toBlock`. No execution-payload SSZ proof is
// required; `logsBloom`, `receiptsRoot`, `transactionsRoot`, `blockNumber` and
// `timestamp` are fields of the EL header.
//
// **Per block** one of two variants (covers the three scenarios of issue #128):
//   - `NONE`: the header's `logsBloom` is proven by the parentHash chain. The
//     verifier computes the query bloom(s) from the filter and asserts that none
//     is a bit-subset of the block's `logsBloom`, hence no matching log can exist.
//   - `FullReceipts`: all RLP receipts are delivered so the verifier rebuilds the
//     receipts trie and compares it to the header's `receiptsRoot`, then filters
//     the logs locally. Matching transactions are bound via Patricia proofs against
//     the header's `transactionsRoot` (the leaf is the raw tx, used for `transactionHash`).
//
// The claim (the requested block range) comes from the RPC request, so the range
// endpoints are NOT carried in the proof; the verifier derives fromBlock/toBlock
// from the proven EL headers and binds them to the request. For an open-ended
// `toBlock` (`latest`) the freshness gate reads the anchor header's `timestamp`.
//
// CompletenessTx is LogsTx without `receiptProof`: receipts are delivered in full.
// The Patricia leaf of `transactionProof` is the raw transaction (for transactionHash).

// Full-receipts block: all receipts are delivered so the verifier rebuilds the receipts trie.
static const ssz_def_t ETH_COMPLETENESS_FULL_RECEIPTS[] = {
    SSZ_PROG_LIST("receipts", ssz_bytes_list),         // all RLP-serialized receipts of the block
    SSZ_PROG_LIST("transactionProof", ssz_bytes_list), // Multi Patricia Merkle Proof which contains all nodes of the tries used by all the txs.
    SSZ_PROG_LIST("txs", ssz_uint32_def)               // index of the matching txs, bound via Multi Patricia proofs against transactionsRoot
};

// Per-block union: a block is either proven bloom-negative (header only) or delivered with all receipts.
static const ssz_def_t ETH_COMPLETENESS_BLOCK_UNION[] = {
    SSZ_NONE,                                                      // 0: no matching log possible (bloom check against the EL header)
    SSZ_CONTAINER("FullReceipts", ETH_COMPLETENESS_FULL_RECEIPTS), // 1: all receipts delivered, filtered locally
};
static const ssz_def_t ETH_COMPLETENESS_BLOCK = SSZ_UNION("block", ETH_COMPLETENESS_BLOCK_UNION);

// The main proof data for a logs completeness proof over a contiguous block range.
static const ssz_def_t ETH_LOGS_COMPLETENESS_PROOF[] = {
    SSZ_UNION("block", ETH_BLOCK_PROOF_UNION),       // proof for the newest execution block (toBlock / anchor)
    SSZ_PROG_LIST("headers", ssz_bytes_list),        // RLP EL headers ascending fromBlock .. toBlock-1 (parentHash chain)
    SSZ_PROG_LIST("blocks", ETH_COMPLETENESS_BLOCK), // per-block payload ascending fromBlock..toBlock (NONE or FullReceipts)
};
static const ssz_def_t ETH_LOGS_COMPLETENESS_PROOF_CONTAINER = SSZ_CONTAINER("LogsCompletenessProof", ETH_LOGS_COMPLETENESS_PROOF);

// :: Transaction Proof
//
// A Transaction Proof verifies that a specific transaction is included in a verified execution block.
//
// 1. **Execution Block:** The execution block is verified via `ETH_BLOCK_PROOF_UNION`
//    (`c4_verify_block`). This yields a verified RLP EL header and its keccak `blockHash`.
// 2. **Transaction Inclusion:** A Patricia Merkle proof against the header's `transactionsRoot`
//    delivers the raw transaction as the leaf.
// 3. **Request Binding:** The raw transaction (and the header's `blockNumber` / `blockHash`)
//    is matched against the RPC arguments (`eth_getTransactionByHash`,
//    `eth_getTransactionByBlockHashAndIndex`, or `eth_getTransactionByBlockNumberAndIndex`).
//
// ```mermaid
// flowchart TB
//     subgraph "Execution Layer"
//         TX -- "Patricia Merkle" --> transactionsRoot
//         subgraph "RLP EL Header"
//             transactionsRoot
//             stateRoot
//             receiptsRoot
//         end
//         elHeader["RLP EL Header"] --> blockHash["keccak(elHeader)"]
//     end
//     subgraph "Consensus Layer"
//         blockHash -- "SSZ gindex 812 or 2856" --> bodyRoot
//         subgraph "BeaconBlockHeader"
//             slot
//             proposerIndex
//             parentRoot
//             s[stateRoot]
//             bodyRoot
//         end
//     end
// ```

// The main proof data for a single transaction.
static const ssz_def_t ETH_TRANSACTION_PROOF[] = {
    SSZ_UINT32("transactionIndex"),                    // the index of the transaction in the block
    SSZ_PROG_LIST("transactionProof", ssz_bytes_list), // the Patricia Merkle Proof of the transaction, the leaf contains the raw transaction.
    SSZ_UNION("block", ETH_BLOCK_PROOF_UNION),         // the proof for the execution block containing the transaction
};

// :: Receipt Proof
//
// A **Receipt Proof** verifies a transaction receipt and its inclusion in a verified execution block.
//
// 1. **Execution Block:** The execution block is verified via `ETH_BLOCK_PROOF_UNION`
//    (`c4_verify_block`). This yields a verified RLP EL header and its keccak `blockHash`.
// 2. **Receipt Inclusion:** A Patricia Merkle proof against the header's `receiptsRoot`
//    delivers the raw receipt as the leaf.
// 3. **Transaction Binding:** A Patricia Merkle proof against the header's `transactionsRoot`
//    delivers the raw transaction (used for `transactionHash` and sender recovery).
//
// ```mermaid
// flowchart TB
//     subgraph "Execution Layer"
//         Receipt -- "Patricia Merkle" --> receiptsRoot
//         TX -- "Patricia Merkle" --> transactionsRoot
//         subgraph "RLP EL Header"
//             receiptsRoot
//             transactionsRoot
//             stateRoot
//         end
//         elHeader["RLP EL Header"] --> blockHash["keccak(elHeader)"]
//     end
//     subgraph "Consensus Layer"
//         blockHash -- "SSZ gindex 812 or 2856" --> bodyRoot
//         subgraph "BeaconBlockHeader"
//             slot
//             proposerIndex
//             parentRoot
//             s[stateRoot]
//             bodyRoot
//         end
//     end
// ```

// The main proof data for a receipt.
static const ssz_def_t ETH_RECEIPT_PROOF[] = {
    SSZ_UINT32("transactionIndex"),                    // the index of the transaction in the block
    SSZ_PROG_LIST("transactionProof", ssz_bytes_list), // the Patricia Merkle Proof of the transaction, the leaf contains the raw transaction.
    SSZ_LIST("receiptProof", ssz_bytes_1024, 64),      // the Patricia Merkle Proof of the receipt, the leaf contains the raw receipt.
    SSZ_UNION("block", ETH_BLOCK_PROOF_UNION),         // the proof for the execution block containing the transaction
};

// :: Account Proof
//
// An Account Proof represents the account and storage values, including the Merkle proof, of the specified account.
//
// 1. **Execution Block:** The execution block is verified via `ETH_BLOCK_PROOF_UNION`
//    (`c4_verify_block`). This yields a verified RLP EL header and its keccak `blockHash`.
// 2. **Account Inclusion:** A Patricia Merkle proof against the header's `stateRoot` delivers
//    the account object (`nonce`, `balance`, `storageRoot`, `codeHash`). Equivalent to the
//    data returned by `eth_getProof`.
// 3. **Storage Inclusion:** Each requested storage key has its own Patricia Merkle proof
//    against the account's `storageRoot`. The value is the leaf of that proof.
//
// ```mermaid
// flowchart TB
//     subgraph "Execution Layer"
//         subgraph "Account"
//             balance --> account
//             nonce --> account
//             codeHash --> account
//             storageHash --> account
//         end
//         subgraph "Storage"
//             key1 -- "..PM.." --> storageHash
//             key2 -- "..PM.." --> storageHash
//             key3 -- "..PM.." --> storageHash
//         end
//         account -- "..PM.." --> stateRoot
//         subgraph "RLP EL Header"
//             stateRoot
//         end
//         elHeader["RLP EL Header"] --> blockHash["keccak(elHeader)"]
//     end
//     subgraph "Consensus Layer"
//         blockHash -- "SSZ gindex 812 or 2856" --> bodyRoot
//         subgraph "BeaconBlockHeader"
//             slot
//             proposerIndex
//             parentRoot
//             s[stateRoot]
//             bodyRoot
//         end
//     end
// ```

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
    SSZ_UNION("block", ETH_BLOCK_PROOF_UNION)};                 // the block proof of the account

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
// 1. **Execution Block:** The execution block is verified via `ETH_BLOCK_PROOF_UNION`
//    (`c4_verify_block`). This yields a verified RLP EL header. EVM block context
//    (`blockNumber`, `timestamp`, `coinbase`, `prevRandao`, `baseFeePerGas`, `blockHash`,
//    `gasLimit`, `excessBlobGas`) is read from that header.
// 2. **Account and Storage Proofs:** A Patricia Merkle proof is constructed for each involved
//    account and all accessed storage values. Each account proof reconstructs the header's
//    `stateRoot`. Contract code is included when needed and checked against `codeHash`.
// 3. **Stateless Execution:** The EVM is executed against the proven accounts and storage.
//    The returned output must match the proven call result.
//
// ```mermaid
// flowchart TB
//     subgraph "Execution Layer"
//         subgraph "Account"
//             balance --> account
//             nonce --> account
//             codeHash --> account
//             storageHash --> account
//         end
//         subgraph "Storage"
//             key1 -- "..PM.." --> storageHash
//             key2 -- "..PM.." --> storageHash
//             key3 -- "..PM.." --> storageHash
//         end
//         account -- "..PM.." --> stateRoot
//         subgraph "RLP EL Header"
//             stateRoot
//         end
//         elHeader["RLP EL Header"] --> blockHash["keccak(elHeader)"]
//     end
//     subgraph "Consensus Layer"
//         blockHash -- "SSZ gindex 812 or 2856" --> bodyRoot
//         subgraph "BeaconBlockHeader"
//             slot
//             proposerIndex
//             parentRoot
//             s[stateRoot]
//             bodyRoot
//         end
//     end
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
    SSZ_UNION("block", ETH_BLOCK_PROOF_UNION)};            // the block proof of the accounts

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
    SSZ_LIST("proof", ssz_bytes32, 256) // the merkle proof from the signing root to the pubkeys of the next sync committee
};

// :: Block Proof
//
// The **Block Proof** verifies that a specific execution-layer block is valid
// and correctly referenced by the consensus layer (Beacon Chain).
//
// 1. **Execution Block:** The execution block is verified via `ETH_BLOCK_PROOF_UNION`
//    (`c4_verify_block`). This yields a verified RLP EL header and its keccak `blockHash`.
//    Header-only RPC methods (`eth_getBlockHeader`, `eth_blockNumber`, `eth_blobBaseFee`,
//    `eth_maxPriorityFeePerGas`) use the `NONE` body variant and reconstruct the result
//    from the RLP header alone.
// 2. **Optional Body:** For `eth_getBlockByNumber` / `eth_getBlockByHash` the proof may
//    include the raw transactions and withdrawals. The verifier rebuilds `transactionsRoot`
//    and `withdrawalsRoot` and compares them to the corresponding fields of the EL header.
//
// ```mermaid
// flowchart TB
//     subgraph "Execution Layer"
//         txs["raw transactions"] -- "keccak MPT" --> transactionsRoot
//         wd["withdrawals"] -- "SSZ hash_tree_root" --> withdrawalsRoot
//         subgraph "RLP EL Header"
//             transactionsRoot
//             withdrawalsRoot
//             stateRoot
//             receiptsRoot
//         end
//         elHeader["RLP EL Header"] --> blockHash["keccak(elHeader)"]
//     end
//     subgraph "Consensus Layer"
//         blockHash -- "SSZ gindex 812 or 2856" --> bodyRoot
//         subgraph "BeaconBlockHeader"
//             slot
//             proposerIndex
//             parentRoot
//             s[stateRoot]
//             bodyRoot
//         end
//     end
// ```

// The content of an execution block body (only present for full-block RPC methods).
static const ssz_def_t ETH_BLOCK_BODY_CONTENT[] = {
    SSZ_PROG_LIST("transactions", ssz_transactions_bytes),     // the raw transactions of the block
    SSZ_PROG_LIST("withdrawals", DENEP_WITHDRAWAL_CONTAINER)}; // the list of withdrawals

// Optional body: header-only methods use NONE; full-block methods include transactions and withdrawals.
static const ssz_def_t ETH_BLOCK_BODY_UNION[] = {
    SSZ_NONE,                                          // no body, just header
    SSZ_CONTAINER("content", ETH_BLOCK_BODY_CONTENT)}; // the Block Body Content

// The Block Proof: a verified EL header (via ETH_BLOCK_PROOF_UNION) plus an optional body.
static const ssz_def_t ETH_BLOCK_PROOF[] = {
    SSZ_UNION("body", ETH_BLOCK_BODY_UNION),
    SSZ_UNION("block", ETH_BLOCK_PROOF_UNION)}; // the proof for the execution block

// :: Block Receipts Proof
//
// A **Block Receipts Proof** verifies all transaction receipts of a given block.
// Instead of proving a single receipt via Patricia Merkle Proof, the proof includes
// **all** raw serialized receipts and the full transactions list.
//
// 1. **Execution Block:** The execution block is verified via `ETH_BLOCK_PROOF_UNION`
//    (`c4_verify_block`). This yields a verified RLP EL header and its keccak `blockHash`.
// 2. **Receipt Trie Verification:** The verifier builds the complete Patricia Merkle Trie
//    from all serialized receipts and compares the root to the header's `receiptsRoot`.
// 3. **Transactions Verification:** The raw transactions list is included so the verifier
//    can rebuild the transactions trie, compare it to the header's `transactionsRoot`,
//    and compute each `transactionHash` / `transactionIndex`.

// The main proof data for all receipts of a block.
static const ssz_def_t ETH_BLOCK_RECEIPTS_PROOF[] = {
    SSZ_PROG_LIST("transactions", ssz_transactions_bytes), // all raw transactions of the block
    SSZ_PROG_LIST("receipts", ssz_bytes_list),             // all RLP-serialized receipts of the block
    SSZ_UNION("block", ETH_BLOCK_PROOF_UNION)};            // the proof for the execution block containing the transaction
