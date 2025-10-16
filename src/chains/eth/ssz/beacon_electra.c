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

// title: Beacon Types
// description: The  SSZ types for the Beacon chain for the Electra Fork.

#include "beacon_types.h"
#include "ssz.h" // Ensure ssz_uint64_def is available

#define MAX_PROPOSER_SLASHINGS 16
// #define MAX_ATTESTER_SLASHINGS       2 // Deneb Value
// #define MAX_ATTESTATIONS             128 // Deneb Value
#define MAX_DEPOSITS                 16
#define MAX_VOLUNTARY_EXITS          16
#define MAX_BLS_TO_EXECUTION_CHANGES 16

// Electra specific constants
#define MAX_ATTESTER_SLASHINGS_ELECTRA         1
#define MAX_ATTESTATIONS_ELECTRA               8
#define MAX_DEPOSIT_REQUESTS_PER_PAYLOAD       8192
#define MAX_WITHDRAWAL_REQUESTS_PER_PAYLOAD    16
#define MAX_CONSOLIDATION_REQUESTS_PER_PAYLOAD 2

#define MAX_COMMITTEES_PER_SLOT 64
#ifndef MAX_VALIDATORS_PER_COMMITTEE // May be defined elsewhere
#define MAX_VALIDATORS_PER_COMMITTEE 2048
#endif
// #define MAX_AGGREGATION_BITLIST_LENGTH (MAX_VALIDATORS_PER_COMMITTEE * MAX_COMMITTEES_PER_SLOT)
// #define MAX_INDEXED_ATTESTATION_INDICES_LENGTH (MAX_VALIDATORS_PER_COMMITTEE * MAX_COMMITTEES_PER_SLOT)
#define MAX_AGGREGATION_BITLIST_LENGTH         131072 // 2048 * 64
#define MAX_INDEXED_ATTESTATION_INDICES_LENGTH 131072 // 2048 * 64

// New SSZ types for Electra Execution Layer Requests
#ifdef PROOFER

static const ssz_def_t DEPOSIT_REQUEST[] = {
    SSZ_BYTE_VECTOR("pubkey", 48),
    SSZ_BYTES32("withdrawalCredentials"),
    SSZ_UINT64("amount"),
    SSZ_BYTE_VECTOR("signature", 96),
    SSZ_UINT64("index")};
const ssz_def_t ELECTRA_DEPOSIT_REQUEST_CONTAINER = SSZ_CONTAINER("DepositRequest", DEPOSIT_REQUEST);

static const ssz_def_t WITHDRAWAL_REQUEST[] = {
    SSZ_ADDRESS("sourceAddress"),
    SSZ_BYTE_VECTOR("validatorPubkey", 48),
    SSZ_UINT64("amount")};
const ssz_def_t ELECTRA_WITHDRAWAL_REQUEST_CONTAINER = SSZ_CONTAINER("WithdrawalRequest", WITHDRAWAL_REQUEST);

static const ssz_def_t CONSOLIDATION_REQUEST[] = {
    SSZ_ADDRESS("sourceAddress"),
    SSZ_BYTE_VECTOR("sourcePubkey", 48),
    SSZ_BYTE_VECTOR("targetPubkey", 48)};
const ssz_def_t ELECTRA_CONSOLIDATION_REQUEST_CONTAINER = SSZ_CONTAINER("ConsolidationRequest", CONSOLIDATION_REQUEST);

static const ssz_def_t ELECTRA_EXECUTION_REQUESTS[] = {
    SSZ_LIST("deposits", ELECTRA_DEPOSIT_REQUEST_CONTAINER, MAX_DEPOSIT_REQUESTS_PER_PAYLOAD),
    SSZ_LIST("withdrawals", ELECTRA_WITHDRAWAL_REQUEST_CONTAINER, MAX_WITHDRAWAL_REQUESTS_PER_PAYLOAD),
    SSZ_LIST("consolidations", ELECTRA_CONSOLIDATION_REQUEST_CONTAINER, MAX_CONSOLIDATION_REQUESTS_PER_PAYLOAD)};
const ssz_def_t ELECTRA_EXECUTION_REQUESTS_CONTAINER = SSZ_CONTAINER("ExecutionRequests", ELECTRA_EXECUTION_REQUESTS);

// a checkpoint is a tuple of epoch and root
static const ssz_def_t CHECKPOINT[] = {
    SSZ_UINT64("epoch"), // the epoch of the checkpoint
    SSZ_BYTES32("root")  // the root of the checkpoint
};

// the data of an attestation
static const ssz_def_t ATTESTATION_DATA[] = {
    SSZ_UINT64("slot"),                  // the slot of the attestation
    SSZ_UINT64("index"),                 // the index of the attestation
    SSZ_BYTES32("beaconBlockRoot"),      // the root of the beacon block
    SSZ_CONTAINER("source", CHECKPOINT), // the source of the attestation
    SSZ_CONTAINER("target", CHECKPOINT)  // the target of the attestation
};

// an index attestation is a list of attesting indices, a data and a signature
static const ssz_def_t INDEX_ATTESTATION[] = {
    SSZ_LIST("attestingIndices", ssz_uint64_def, MAX_INDEXED_ATTESTATION_INDICES_LENGTH), // MODIFIED for Electra (EIP-7549), type changed to ssz_uint64_def
    SSZ_CONTAINER("data", ATTESTATION_DATA),                                              // the data of the attestation
    SSZ_BYTE_VECTOR("signature", 96)                                                      // the BLS signature of the attestation
};

// a signed beacon block header is a beacon block header and a signature
static const ssz_def_t SIGNED_BEACON_BLOCKHEADER[] = {
    SSZ_CONTAINER("message", BEACON_BLOCK_HEADER), // the beacon block header
    SSZ_BYTE_VECTOR("signature", 96)               // the BLS signature of the beacon block header
};

// a proposer slashing is a list of two signed beacon block headers
static const ssz_def_t PROPOSER_SLASHING[] = {
    SSZ_CONTAINER("signedHeader1", SIGNED_BEACON_BLOCKHEADER),
    SSZ_CONTAINER("signedHeader2", SIGNED_BEACON_BLOCKHEADER),
};

// an attester slashing is a list of two index attestations
static const ssz_def_t ATTESTER_SLASHING[] = {
    SSZ_CONTAINER("signedHeader1", INDEX_ATTESTATION), // Kept original C-style field name, type INDEX_ATTESTATION is updated
    SSZ_CONTAINER("signedHeader2", INDEX_ATTESTATION), // Kept original C-style field name, type INDEX_ATTESTATION is updated
};

// the eth1 data is a deposit root, a deposit count and a block hash
static const ssz_def_t ETH1_DATA[] = {
    SSZ_BYTES32("depositRoot"),
    SSZ_UINT64("depositCount"),
    SSZ_BYTES32("blockHash"),
};

// an attestation is a list of aggregation bits, a data and a signature
static const ssz_def_t ATTESTATION[] = {
    SSZ_BIT_LIST("aggregationBits", MAX_AGGREGATION_BITLIST_LENGTH), // MODIFIED for Electra (EIP-7549)
    SSZ_CONTAINER("data", ATTESTATION_DATA),
    SSZ_BYTE_VECTOR("signature", 96),
    SSZ_BIT_VECTOR("committeeBits", MAX_COMMITTEES_PER_SLOT) // NEW for Electra (EIP-7549)
};

static const ssz_def_t DEPOSIT_DATA[] = {
    SSZ_BYTE_VECTOR("pubkey", 48),
    SSZ_BYTES32("withdrawalCredentials"),
    SSZ_UINT64("amount"),
    SSZ_BYTE_VECTOR("signature", 96),
};

static const ssz_def_t DEPOSIT[] = {
    SSZ_VECTOR("proof", ssz_bytes32, 33),
    SSZ_CONTAINER("data", DEPOSIT_DATA),
};

static const ssz_def_t VOLUNTARY_EXIT[] = {
    SSZ_UINT64("epoch"),
    SSZ_UINT64("validatorIndex"),
};

static const ssz_def_t SIGNED_VOLUNTARY_EXIT[] = {
    SSZ_CONTAINER("message", VOLUNTARY_EXIT),
    SSZ_BYTE_VECTOR("signature", 96),
};

static const ssz_def_t BLS_TO_EXECUTION_CHANGE[] = {
    SSZ_UINT64("validatorIndex"),
    SSZ_BYTE_VECTOR("fromBlsPubkey", 48),
    SSZ_ADDRESS("toExecutionAddress"),
};

static const ssz_def_t SIGNED_BLS_TO_EXECUTION_CHANGE[] = {
    SSZ_CONTAINER("message", BLS_TO_EXECUTION_CHANGE),
    SSZ_BYTE_VECTOR("signature", 96),
};

static const ssz_def_t PROPOSER_SLASHING_CONTAINER = SSZ_CONTAINER("proposerSlashing", PROPOSER_SLASHING);
static const ssz_def_t ATTESTER_SLASHING_CONTAINER = SSZ_CONTAINER("attesterSlashing", ATTESTER_SLASHING);

static const ssz_def_t ATTESTATION_CONTAINER                    = SSZ_CONTAINER("attestation", ATTESTATION);
static const ssz_def_t DEPOSIT_CONTAINER                        = SSZ_CONTAINER("deposit", DEPOSIT);
static const ssz_def_t SIGNED_VOLUNTARY_EXIT_CONTAINER          = SSZ_CONTAINER("signedVoluntaryExit", SIGNED_VOLUNTARY_EXIT);
static const ssz_def_t SIGNED_BLS_TO_EXECUTION_CHANGE_CONTAINER = SSZ_CONTAINER("signedBlsToExecutionChange", SIGNED_BLS_TO_EXECUTION_CHANGE);
static const ssz_def_t BEACON_BLOCK_BODY[]                      = {
    SSZ_BYTE_VECTOR("randaoReveal", 96),
    SSZ_CONTAINER("eth1Data", ETH1_DATA),
    SSZ_BYTES32("graffiti"),
    SSZ_LIST("proposerSlashings", PROPOSER_SLASHING_CONTAINER, MAX_PROPOSER_SLASHINGS),
    SSZ_LIST("attesterSlashings", ATTESTER_SLASHING_CONTAINER, MAX_ATTESTER_SLASHINGS_ELECTRA), // MODIFIED for Electra
    SSZ_LIST("attestations", ATTESTATION_CONTAINER, MAX_ATTESTATIONS_ELECTRA),                  // MODIFIED for Electra
    SSZ_LIST("deposits", DEPOSIT_CONTAINER, MAX_DEPOSITS),
    SSZ_LIST("voluntaryExits", SIGNED_VOLUNTARY_EXIT_CONTAINER, MAX_VOLUNTARY_EXITS),
    SSZ_CONTAINER("syncAggregate", SYNC_AGGREGATE),
    SSZ_CONTAINER("executionPayload", DENEP_EXECUTION_PAYLOAD), // MODIFIED for Electra (already using ELECTRA_EXECUTION_PAYLOAD)
    SSZ_LIST("blsToExecutionChanges", SIGNED_BLS_TO_EXECUTION_CHANGE_CONTAINER, MAX_BLS_TO_EXECUTION_CHANGES),
    SSZ_LIST("blobKzgCommitments", ssz_bls_pubky, 4096),           // Max length unchanged, type ssz_bls_pubky (48 bytes) for KZGCommitment
    SSZ_CONTAINER("executionRequests", ELECTRA_EXECUTION_REQUESTS) // NEW for Electra
};
static const ssz_def_t BEACON_BLOCK_BODY_GNOSIS[] = {
    SSZ_BYTE_VECTOR("randaoReveal", 96),
    SSZ_CONTAINER("eth1Data", ETH1_DATA),
    SSZ_BYTES32("graffiti"),
    SSZ_LIST("proposerSlashings", PROPOSER_SLASHING_CONTAINER, MAX_PROPOSER_SLASHINGS),
    SSZ_LIST("attesterSlashings", ATTESTER_SLASHING_CONTAINER, MAX_ATTESTER_SLASHINGS_ELECTRA), // MODIFIED for Electra
    SSZ_LIST("attestations", ATTESTATION_CONTAINER, MAX_ATTESTATIONS_ELECTRA),                  // MODIFIED for Electra
    SSZ_LIST("deposits", DEPOSIT_CONTAINER, MAX_DEPOSITS),
    SSZ_LIST("voluntaryExits", SIGNED_VOLUNTARY_EXIT_CONTAINER, MAX_VOLUNTARY_EXITS),
    SSZ_CONTAINER("syncAggregate", SYNC_AGGREGATE),
    SSZ_CONTAINER("executionPayload", GNOSIS_EXECUTION_PAYLOAD), // MODIFIED for Electra (already using ELECTRA_EXECUTION_PAYLOAD)
    SSZ_LIST("blsToExecutionChanges", SIGNED_BLS_TO_EXECUTION_CHANGE_CONTAINER, MAX_BLS_TO_EXECUTION_CHANGES),
    SSZ_LIST("blobKzgCommitments", ssz_bls_pubky, 4096),           // Max length unchanged, type ssz_bls_pubky (48 bytes) for KZGCommitment
    SSZ_CONTAINER("executionRequests", ELECTRA_EXECUTION_REQUESTS) // NEW for Electra
};

static const ssz_def_t BEACON_BLOCK[] = {
    SSZ_UINT64("slot"),          // the slot of the block or blocknumber
    SSZ_UINT64("proposerIndex"), // the index of the validator proposing the block
    SSZ_BYTES32("parentRoot"),   // the hash_tree_root of the parent block header
    SSZ_BYTES32("stateRoot"),    // the hash_tree_root of the state at the end of the block
    SSZ_CONTAINER("body", BEACON_BLOCK_BODY)};

static const ssz_def_t BEACON_BLOCK_GNOSIS[] = {
    SSZ_UINT64("slot"),          // the slot of the block or blocknumber
    SSZ_UINT64("proposerIndex"), // the index of the validator proposing the block
    SSZ_BYTES32("parentRoot"),   // the hash_tree_root of the parent block header
    SSZ_BYTES32("stateRoot"),    // the hash_tree_root of the state at the end of the block
    SSZ_CONTAINER("body", BEACON_BLOCK_BODY_GNOSIS)};

static const ssz_def_t SIGNED_BEACON_BLOCK[] = {
    SSZ_CONTAINER("message", BEACON_BLOCK),
    SSZ_BYTE_VECTOR("signature", 96)};

static const ssz_def_t SIGNED_BEACON_BLOCK_GNOSIS[] = {
    SSZ_CONTAINER("message", BEACON_BLOCK_GNOSIS),
    SSZ_BYTE_VECTOR("signature", 96)};

static const ssz_def_t BEACON_BLOCK_BODY_CONTAINER          = SSZ_CONTAINER("beaconBlockBody", BEACON_BLOCK_BODY);
static const ssz_def_t BEACON_BLOCK_BODY_GNOSIS_CONTAINER   = SSZ_CONTAINER("beaconBlockBodyGnosis", BEACON_BLOCK_BODY_GNOSIS);
static const ssz_def_t SIGNED_BEACON_BLOCK_CONTAINER        = SSZ_CONTAINER("signedBeaconBlock", SIGNED_BEACON_BLOCK);
static const ssz_def_t SIGNED_BEACON_BLOCK_GNOSIS_CONTAINER = SSZ_CONTAINER("signedBeaconBlockGnosis", SIGNED_BEACON_BLOCK_GNOSIS);

#endif

// the light client update is used to verify the transition between two periods of the SyncCommittee.
// This data will be fetched directly through the beacon Chain API since it contains all required data.
const ssz_def_t ELECTRA_LIGHT_CLIENT_UPDATE[7] = {
    SSZ_CONTAINER("attestedHeader", LIGHT_CLIENT_HEADER), // the header of the beacon block attested by the sync committee
    SSZ_CONTAINER("nextSyncCommittee", SYNC_COMMITTEE),
    SSZ_VECTOR("nextSyncCommitteeBranch", ssz_bytes32, 6), // will be 6 in electra
    SSZ_CONTAINER("finalizedHeader", LIGHT_CLIENT_HEADER), // the header of the finalized beacon block
    SSZ_VECTOR("finalityBranch", ssz_bytes32, 7),          // will be 7 in electra
    SSZ_CONTAINER("syncAggregate", SYNC_AGGREGATE),        // the aggregates signature of the sync committee
    SSZ_UINT64("signatureSlot")};                          // the slot of the signature

static const ssz_def_t BEACON_BLOCKHEADER_CONTAINER = SSZ_CONTAINER("BeaconBlockHeader", BEACON_BLOCK_HEADER);

// the light client bootstrap is used for initial sync from a trusted checkpoint
const ssz_def_t ELECTRA_LIGHT_CLIENT_BOOTSTRAP[3] = {
    SSZ_CONTAINER("header", LIGHT_CLIENT_HEADER),              // header matching the requested beacon block root
    SSZ_CONTAINER("currentSyncCommittee", SYNC_COMMITTEE),     // current sync committee corresponding to header.beacon.state_root
    SSZ_VECTOR("currentSyncCommitteeBranch", ssz_bytes32, 6)}; // merkle proof for current sync committee (depth 6 in Electra)

// the block header of the execution layer proved within the beacon block
static const ssz_def_t EXECUTION_PAYLOAD_HEADER[] = {
    SSZ_BYTES32("parentHash"),         // the hash of the parent block
    SSZ_ADDRESS("feeRecipient"),       // the address of the fee recipient
    SSZ_BYTES32("stateRoot"),          // the merkle root of the state at the end of the block
    SSZ_BYTES32("receiptsRoot"),       // the merkle root of the transactionreceipts
    SSZ_BYTE_VECTOR("logsBloom", 256), // the bloom filter of the logs
    SSZ_BYTES32("prevRandao"),         // the randao of the previous block
    SSZ_UINT64("blockNumber"),         // the block number
    SSZ_UINT64("gasLimit"),            // the gas limit of the block
    SSZ_UINT64("gasUsed"),             // the gas used of the block
    SSZ_UINT64("timestamp"),           // the timestamp of the block
    SSZ_BYTES("extraData", 32),        // the extra data of the block
    SSZ_UINT256("baseFeePerGas"),      // the base fee per gas of the block
    SSZ_BYTES32("blockHash"),          // the hash of the block
    SSZ_BYTES32("transactionsRoot"),   // the merkle root of the transactions
    SSZ_BYTES32("withdrawalsRoot"),    // the merkle root of the withdrawals
    SSZ_UINT64("blobGasUsed"),         // the gas used for the blob transactions
    SSZ_UINT64("excessBlobGas")};      // the excess blob gas of the block

// --- Main function to get Electra SSZ types ---
const ssz_def_t* eth_ssz_type_for_electra(eth_ssz_type_t type, chain_id_t chain_id) {
  switch (type) {
#ifdef PROOFER
    case ETH_SSZ_BEACON_BLOCK_BODY_CONTAINER:
      return is_gnosis_chain(chain_id)
                 ? &BEACON_BLOCK_BODY_GNOSIS_CONTAINER
                 : &BEACON_BLOCK_BODY_CONTAINER;
    case ETH_SSZ_SIGNED_BEACON_BLOCK_CONTAINER:
      return is_gnosis_chain(chain_id)
                 ? &SIGNED_BEACON_BLOCK_GNOSIS_CONTAINER
                 : &SIGNED_BEACON_BLOCK_CONTAINER;
#endif
    // BlockHeader is unchanged from Deneb as per user request.
    // eth_ssz_type_for_denep will be called for it.
    // Explicitly handle BEACON_BLOCK_HEADER if it should use Deneb's version
    case ETH_SSZ_BEACON_BLOCK_HEADER: // Assuming this should fall through to Deneb
    default:
      // Fallback to Deneb for any other types.
      // This includes ETH_SSZ_BEACON_BLOCK_HEADER etc.
      return eth_ssz_type_for_denep(type, chain_id);
  }
}