// title: Beacon Types
// description: The  SSZ types for the Beacon chain for the Denep Fork.

#include "beacon_types.h"

#define MAX_PROPOSER_SLASHINGS       16
#define MAX_ATTESTER_SLASHINGS       2
#define MAX_ATTESTATIONS             128
#define MAX_DEPOSITS                 16
#define MAX_VOLUNTARY_EXITS          16
#define MAX_BLS_TO_EXECUTION_CHANGES 16
#define LIMIT_WITHDRAWELS_MAINNET    16
#define LIMIT_WITHDRAWELS_GNOSIS     8

const ssz_def_t ssz_transactions_bytes = SSZ_BYTES("Bytes", 1073741824);
// the header of a beacon block
const ssz_def_t BEACON_BLOCK_HEADER[5] = {
    SSZ_UINT64("slot"),          // the slot of the block or blocknumber
    SSZ_UINT64("proposerIndex"), // the index of the validator proposing the block
    SSZ_BYTES32("parentRoot"),   // the hash_tree_root of the parent block header
    SSZ_BYTES32("stateRoot"),    // the hash_tree_root of the state at the end of the block
    SSZ_BYTES32("bodyRoot")};    // the hash_tree_root of the block body

// the aggregates signature of the sync committee
const ssz_def_t SYNC_AGGREGATE[2] = {
    SSZ_BIT_VECTOR("syncCommitteeBits", 512),       // the bits of the validators that signed the block (each bit represents a validator)
    SSZ_BYTE_VECTOR("syncCommitteeSignature", 96)}; // the signature of the sync committee

static const ssz_def_t WITHDRAWAL[] = {
    SSZ_UINT64("index"),
    SSZ_UINT64("validatorIndex"),
    SSZ_ADDRESS("address"),
    SSZ_UINT64("amount"),
};

const ssz_def_t DENEP_WITHDRAWAL_CONTAINER = SSZ_CONTAINER("withdrawal", WITHDRAWAL);

// the block header of the execution layer proved within the beacon block
const ssz_def_t DENEP_EXECUTION_PAYLOAD[] = {
    SSZ_BYTES32("parentHash"),                                                      // the hash of the parent block
    SSZ_ADDRESS("feeRecipient"),                                                    // the address of the fee recipient
    SSZ_BYTES32("stateRoot"),                                                       // the merkle root of the state at the end of the block
    SSZ_BYTES32("receiptsRoot"),                                                    // the merkle root of the transactionreceipts
    SSZ_BYTE_VECTOR("logsBloom", 256),                                              // the bloom filter of the logs
    SSZ_BYTES32("prevRandao"),                                                      // the randao of the previous block
    SSZ_UINT64("blockNumber"),                                                      // the block number
    SSZ_UINT64("gasLimit"),                                                         // the gas limit of the block
    SSZ_UINT64("gasUsed"),                                                          // the gas used of the block
    SSZ_UINT64("timestamp"),                                                        // the timestamp of the block
    SSZ_BYTES("extraData", 32),                                                     // the extra data of the block
    SSZ_UINT256("baseFeePerGas"),                                                   // the base fee per gas of the block
    SSZ_BYTES32("blockHash"),                                                       // the hash of the block
    SSZ_LIST("transactions", ssz_transactions_bytes, 1048576),                      // the list of transactions
    SSZ_LIST("withdrawals", DENEP_WITHDRAWAL_CONTAINER, LIMIT_WITHDRAWELS_MAINNET), // the list of withdrawels
    SSZ_UINT64("blobGasUsed"),                                                      // the gas used for the blob transactions
    SSZ_UINT64("excessBlobGas")};                                                   // the excess blob gas of the block

// the block header of the execution layer proved within the beacon block
const ssz_def_t GNOSIS_EXECUTION_PAYLOAD[] = {
    SSZ_BYTES32("parentHash"),                                                     // the hash of the parent block
    SSZ_ADDRESS("feeRecipient"),                                                   // the address of the fee recipient
    SSZ_BYTES32("stateRoot"),                                                      // the merkle root of the state at the end of the block
    SSZ_BYTES32("receiptsRoot"),                                                   // the merkle root of the transactionreceipts
    SSZ_BYTE_VECTOR("logsBloom", 256),                                             // the bloom filter of the logs
    SSZ_BYTES32("prevRandao"),                                                     // the randao of the previous block
    SSZ_UINT64("blockNumber"),                                                     // the block number
    SSZ_UINT64("gasLimit"),                                                        // the gas limit of the block
    SSZ_UINT64("gasUsed"),                                                         // the gas used of the block
    SSZ_UINT64("timestamp"),                                                       // the timestamp of the block
    SSZ_BYTES("extraData", 32),                                                    // the extra data of the block
    SSZ_UINT256("baseFeePerGas"),                                                  // the base fee per gas of the block
    SSZ_BYTES32("blockHash"),                                                      // the hash of the block
    SSZ_LIST("transactions", ssz_transactions_bytes, 1048576),                     // the list of transactions
    SSZ_LIST("withdrawals", DENEP_WITHDRAWAL_CONTAINER, LIMIT_WITHDRAWELS_GNOSIS), // the list of withdrawels
    SSZ_UINT64("blobGasUsed"),                                                     // the gas used for the blob transactions
    SSZ_UINT64("excessBlobGas")};                                                  // the excess blob gas of the block

#ifdef PROOFER
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
    SSZ_LIST("attestingIndices", ssz_uint8, 2048), // the list of attesting indices
    SSZ_CONTAINER("data", ATTESTATION_DATA),       // the data of the attestation
    SSZ_BYTE_VECTOR("signature", 96)               // the BLS signature of the attestation
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
    SSZ_CONTAINER("signedHeader1", INDEX_ATTESTATION),
    SSZ_CONTAINER("signedHeader2", INDEX_ATTESTATION),
};

// the eth1 data is a deposit root, a deposit count and a block hash
static const ssz_def_t ETH1_DATA[] = {
    SSZ_BYTES32("depositRoot"),
    SSZ_UINT64("depositCount"),
    SSZ_BYTES32("blockHash"),
};

// an attestation is a list of aggregation bits, a data and a signature
static const ssz_def_t ATTESTATION[] = {
    SSZ_BIT_LIST("aggregationBits", 2048),
    SSZ_CONTAINER("data", ATTESTATION_DATA),
    SSZ_BYTE_VECTOR("signature", 96),
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
    SSZ_LIST("attesterSlashings", ATTESTER_SLASHING_CONTAINER, MAX_ATTESTER_SLASHINGS),
    SSZ_LIST("attestations", ATTESTATION_CONTAINER, MAX_ATTESTATIONS),
    SSZ_LIST("deposits", DEPOSIT_CONTAINER, MAX_DEPOSITS),
    SSZ_LIST("voluntaryExits", SIGNED_VOLUNTARY_EXIT_CONTAINER, MAX_VOLUNTARY_EXITS),
    SSZ_CONTAINER("syncAggregate", SYNC_AGGREGATE),
    SSZ_CONTAINER("executionPayload", DENEP_EXECUTION_PAYLOAD),
    SSZ_LIST("blsToExecutionChanges", SIGNED_BLS_TO_EXECUTION_CHANGE_CONTAINER, MAX_BLS_TO_EXECUTION_CHANGES),
    SSZ_LIST("blobKzgCommitments", ssz_bls_pubky, 4096),
};

static const ssz_def_t BEACON_BLOCK_BODY_GNOSIS[] = {
    SSZ_BYTE_VECTOR("randaoReveal", 96),
    SSZ_CONTAINER("eth1Data", ETH1_DATA),
    SSZ_BYTES32("graffiti"),
    SSZ_LIST("proposerSlashings", PROPOSER_SLASHING_CONTAINER, MAX_PROPOSER_SLASHINGS),
    SSZ_LIST("attesterSlashings", ATTESTER_SLASHING_CONTAINER, MAX_ATTESTER_SLASHINGS),
    SSZ_LIST("attestations", ATTESTATION_CONTAINER, MAX_ATTESTATIONS),
    SSZ_LIST("deposits", DEPOSIT_CONTAINER, MAX_DEPOSITS),
    SSZ_LIST("voluntaryExits", SIGNED_VOLUNTARY_EXIT_CONTAINER, MAX_VOLUNTARY_EXITS),
    SSZ_CONTAINER("syncAggregate", SYNC_AGGREGATE),
    SSZ_CONTAINER("executionPayload", GNOSIS_EXECUTION_PAYLOAD),
    SSZ_LIST("blsToExecutionChanges", SIGNED_BLS_TO_EXECUTION_CHANGE_CONTAINER, MAX_BLS_TO_EXECUTION_CHANGES),
    SSZ_LIST("blobKzgCommitments", ssz_bls_pubky, 4096),
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

static const ssz_def_t BEACON_BLOCKHEADER_CONTAINER = SSZ_CONTAINER("BeaconBlockHeader", BEACON_BLOCK_HEADER);

// the public keys sync committee used within a period ( about 27h)
const ssz_def_t SYNC_COMMITTEE[] = {
    SSZ_VECTOR("pubkeys", ssz_bls_pubky, 512), // the 512 pubkeys (each 48 bytes) of the validators in the sync committee
    SSZ_BYTE_VECTOR("aggregatePubkey", 48)};   // the aggregate pubkey (48 bytes) of the sync committee

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

// the header of the light client update
const ssz_def_t LIGHT_CLIENT_HEADER[] = {
    SSZ_CONTAINER("beacon", BEACON_BLOCK_HEADER),         // the header of the beacon block
    SSZ_CONTAINER("execution", EXECUTION_PAYLOAD_HEADER), // the header of the execution layer proved within the beacon block
    SSZ_VECTOR("executionBranch", ssz_bytes32, 4)};       // the merkle proof of the execution layer proved within the beacon block

// the light client update is used to verify the transition between two periods of the SyncCommittee.
// This data will be fetched directly through the beacon Chain API since it contains all required data.
const ssz_def_t DENEP_LIGHT_CLIENT_UPDATE[7] = {
    SSZ_CONTAINER("attestedHeader", LIGHT_CLIENT_HEADER), // the header of the beacon block attested by the sync committee
    SSZ_CONTAINER("nextSyncCommittee", SYNC_COMMITTEE),
    SSZ_VECTOR("nextSyncCommitteeBranch", ssz_bytes32, 5), // will be 6 in electra
    SSZ_CONTAINER("finalizedHeader", LIGHT_CLIENT_HEADER), // the header of the finalized beacon block
    SSZ_VECTOR("finalityBranch", ssz_bytes32, 6),          // will be 7 in electra
    SSZ_CONTAINER("syncAggregate", SYNC_AGGREGATE),        // the aggregates signature of the sync committee
    SSZ_UINT64("signatureSlot")};                          // the slot of the signature

const ssz_def_t* eth_ssz_type_for_denep(eth_ssz_type_t type, chain_id_t chain_id) {
  switch (type) {
#ifdef PROOFER
    case ETH_SSZ_SIGNED_BEACON_BLOCK_CONTAINER:
      return is_gnosis_chain(chain_id)
                 ? &SIGNED_BEACON_BLOCK_GNOSIS_CONTAINER
                 : &SIGNED_BEACON_BLOCK_CONTAINER;
    case ETH_SSZ_BEACON_BLOCK_BODY_CONTAINER:
      return is_gnosis_chain(chain_id)
                 ? &BEACON_BLOCK_BODY_GNOSIS_CONTAINER
                 : &BEACON_BLOCK_BODY_CONTAINER;
    case ETH_SSZ_BEACON_BLOCK_HEADER:
      return &BEACON_BLOCKHEADER_CONTAINER;
#endif

    default:
      return eth_ssz_verification_type(type);
  }
}
