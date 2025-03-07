#include "ssz_types.h"
#include "../util/bytes.h"
#include "../util/crypto.h"
#include "../util/ssz.h"
#include "../verifier/types_beacon.h"
#include "../verifier/types_verify.h"
#include "beacon.h"
#include <stdio.h>
#include <stdlib.h>

#define MAX_PROPOSER_SLASHINGS       16
#define MAX_ATTESTER_SLASHINGS       2
#define MAX_ATTESTATIONS             128
#define MAX_DEPOSITS                 16
#define MAX_VOLUNTARY_EXITS          16
#define MAX_BLS_TO_EXECUTION_CHANGES 16

// a checkpoint is a tuple of epoch and root
const ssz_def_t CHECKPOINT[] = {
    SSZ_UINT64("epoch"), // the epoch of the checkpoint
    SSZ_BYTES32("root")  // the root of the checkpoint
};

// the data of an attestation
const ssz_def_t ATTESTATION_DATA[] = {
    SSZ_UINT64("slot"),                  // the slot of the attestation
    SSZ_UINT64("index"),                 // the index of the attestation
    SSZ_BYTES32("beaconBlockRoot"),      // the root of the beacon block
    SSZ_CONTAINER("source", CHECKPOINT), // the source of the attestation
    SSZ_CONTAINER("target", CHECKPOINT)  // the target of the attestation
};

// an index attestation is a list of attesting indices, a data and a signature
const ssz_def_t INDEX_ATTESTATION[] = {
    SSZ_LIST("attestingIndices", ssz_uint8, 2048), // the list of attesting indices
    SSZ_CONTAINER("data", ATTESTATION_DATA),       // the data of the attestation
    SSZ_BYTE_VECTOR("signature", 96)               // the BLS signature of the attestation
};

// a signed beacon block header is a beacon block header and a signature
const ssz_def_t SIGNED_BEACON_BLOCKHEADER[] = {
    SSZ_CONTAINER("message", BEACON_BLOCK_HEADER), // the beacon block header
    SSZ_BYTE_VECTOR("signature", 96)               // the BLS signature of the beacon block header
};

// a proposer slashing is a list of two signed beacon block headers
const ssz_def_t PROPOSER_SLASHING[] = {
    SSZ_CONTAINER("signedHeader1", SIGNED_BEACON_BLOCKHEADER),
    SSZ_CONTAINER("signedHeader2", SIGNED_BEACON_BLOCKHEADER),
};

// an attester slashing is a list of two index attestations
const ssz_def_t ATTESTER_SLASHING[] = {
    SSZ_CONTAINER("signedHeader1", INDEX_ATTESTATION),
    SSZ_CONTAINER("signedHeader2", INDEX_ATTESTATION),
};

// the eth1 data is a deposit root, a deposit count and a block hash
const ssz_def_t ETH1_DATA[] = {
    SSZ_BYTES32("depositRoot"),
    SSZ_UINT64("depositCount"),
    SSZ_BYTES32("blockHash"),
};

// an attestation is a list of aggregation bits, a data and a signature
const ssz_def_t ATTESTATION[] = {
    SSZ_BIT_LIST("aggregationBits", 2048),
    SSZ_CONTAINER("data", ATTESTATION_DATA),
    SSZ_BYTE_VECTOR("signature", 96),
};

const ssz_def_t DEPOSIT_DATA[] = {
    SSZ_BYTE_VECTOR("pubkey", 48),
    SSZ_BYTES32("withdrawalCredentials"),
    SSZ_UINT64("amount"),
    SSZ_BYTE_VECTOR("signature", 96),
};

const ssz_def_t DEPOSIT[] = {
    SSZ_VECTOR("proof", ssz_bytes32, 33),
    SSZ_CONTAINER("data", DEPOSIT_DATA),
};

const ssz_def_t VOLUNTARY_EXIT[] = {
    SSZ_UINT64("epoch"),
    SSZ_UINT64("validatorIndex"),
};

const ssz_def_t SIGNED_VOLUNTARY_EXIT[] = {
    SSZ_CONTAINER("message", VOLUNTARY_EXIT),
    SSZ_BYTE_VECTOR("signature", 96),
};

const ssz_def_t WITHDRAWAL[] = {
    SSZ_UINT64("index"),
    SSZ_UINT64("validatorIndex"),
    SSZ_ADDRESS("address"),
    SSZ_UINT64("amount"),
};

const ssz_def_t WITHDRAWAL_CONTAINER = SSZ_CONTAINER("withdrawal", WITHDRAWAL);

// the block header of the execution layer proved within the beacon block
const ssz_def_t EXECUTION_PAYLOAD[] = {
    SSZ_BYTES32("parentHash"),                                 // the hash of the parent block
    SSZ_ADDRESS("feeRecipient"),                               // the address of the fee recipient
    SSZ_BYTES32("stateRoot"),                                  // the merkle root of the state at the end of the block
    SSZ_BYTES32("receiptsRoot"),                               // the merkle root of the transactionreceipts
    SSZ_BYTE_VECTOR("logsBloom", 256),                         // the bloom filter of the logs
    SSZ_BYTES32("prevRandao"),                                 // the randao of the previous block
    SSZ_UINT64("blockNumber"),                                 // the block number
    SSZ_UINT64("gasLimit"),                                    // the gas limit of the block
    SSZ_UINT64("gasUsed"),                                     // the gas used of the block
    SSZ_UINT64("timestamp"),                                   // the timestamp of the block
    SSZ_BYTES("extraData", 32),                                // the extra data of the block
    SSZ_UINT256("baseFeePerGas"),                              // the base fee per gas of the block
    SSZ_BYTES32("blockHash"),                                  // the hash of the block
    SSZ_LIST("transactions", ssz_transactions_bytes, 1048576), // the list of transactions
    SSZ_LIST("withdrawals", WITHDRAWAL_CONTAINER, 16),         // the list of withdrawels
    SSZ_UINT64("blobGasUsed"),                                 // the gas used for the blob transactions
    SSZ_UINT64("excessBlobGas")};                              // the excess blob gas of the block

const ssz_def_t BLS_TO_EXECUTION_CHANGE[] = {
    SSZ_UINT64("validatorIndex"),
    SSZ_BYTE_VECTOR("fromBlsPubkey", 48),
    SSZ_ADDRESS("toExecutionAddress"),
};

const ssz_def_t SIGNED_BLS_TO_EXECUTION_CHANGE[] = {
    SSZ_CONTAINER("message", BLS_TO_EXECUTION_CHANGE),
    SSZ_BYTE_VECTOR("signature", 96),
};

const ssz_def_t PROPOSER_SLASHING_CONTAINER = SSZ_CONTAINER("proposerSlashing", PROPOSER_SLASHING);
const ssz_def_t ATTESTER_SLASHING_CONTAINER = SSZ_CONTAINER("attesterSlashing", ATTESTER_SLASHING);

// const ssz_def_t ssz_bls_pubky[] = {

const ssz_def_t ATTESTATION_CONTAINER                    = SSZ_CONTAINER("attestation", ATTESTATION);
const ssz_def_t DEPOSIT_CONTAINER                        = SSZ_CONTAINER("deposit", DEPOSIT);
const ssz_def_t SIGNED_VOLUNTARY_EXIT_CONTAINER          = SSZ_CONTAINER("signedVoluntaryExit", SIGNED_VOLUNTARY_EXIT);
const ssz_def_t SIGNED_BLS_TO_EXECUTION_CHANGE_CONTAINER = SSZ_CONTAINER("signedBlsToExecutionChange", SIGNED_BLS_TO_EXECUTION_CHANGE);
const ssz_def_t BEACON_BLOCK_BODY[]                      = {
    SSZ_BYTE_VECTOR("randaoReveal", 96),
    SSZ_CONTAINER("eth1Data", ETH1_DATA),
    SSZ_BYTES32("graffiti"),
    SSZ_LIST("proposerSlashings", PROPOSER_SLASHING_CONTAINER, MAX_PROPOSER_SLASHINGS),
    SSZ_LIST("attesterSlashings", ATTESTER_SLASHING_CONTAINER, MAX_ATTESTER_SLASHINGS),
    SSZ_LIST("attestations", ATTESTATION_CONTAINER, MAX_ATTESTATIONS),
    SSZ_LIST("deposits", DEPOSIT_CONTAINER, MAX_DEPOSITS),
    SSZ_LIST("voluntaryExits", SIGNED_VOLUNTARY_EXIT_CONTAINER, MAX_VOLUNTARY_EXITS),
    SSZ_CONTAINER("syncAggregate", SYNC_AGGREGATE),
    SSZ_CONTAINER("executionPayload", EXECUTION_PAYLOAD),
    SSZ_LIST("blsToExecutionChanges", SIGNED_BLS_TO_EXECUTION_CHANGE_CONTAINER, MAX_BLS_TO_EXECUTION_CHANGES),
    SSZ_LIST("blobKzgCommitments", ssz_bls_pubky, 4096),
};

const ssz_def_t BEACON_BLOCK[] = {
    SSZ_UINT64("slot"),          // the slot of the block or blocknumber
    SSZ_UINT64("proposerIndex"), // the index of the validator proposing the block
    SSZ_BYTES32("parentRoot"),   // the hash_tree_root of the parent block header
    SSZ_BYTES32("stateRoot"),    // the hash_tree_root of the state at the end of the block
    SSZ_CONTAINER("body", BEACON_BLOCK_BODY)};

const ssz_def_t SIGNED_BEACON_BLOCK[] = {
    SSZ_CONTAINER("message", BEACON_BLOCK),
    SSZ_BYTE_VECTOR("signature", 96)};

const ssz_def_t BEACON_BLOCK_BODY_CONTAINER   = SSZ_CONTAINER("beaconBlockBody", BEACON_BLOCK_BODY);
const ssz_def_t SIGNED_BEACON_BLOCK_CONTAINER = SSZ_CONTAINER("signedBeaconBlock", SIGNED_BEACON_BLOCK);