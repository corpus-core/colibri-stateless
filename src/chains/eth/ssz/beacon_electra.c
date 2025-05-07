#include "beacon_types.h"

// Constants for Electra from spec
#define MAX_ATTESTER_SLASHINGS_ELECTRA 1
#define MAX_ATTESTATIONS_ELECTRA       8

#define MAX_DEPOSIT_REQUESTS_PER_PAYLOAD       8192
#define MAX_WITHDRAWAL_REQUESTS_PER_PAYLOAD    16
#define MAX_CONSOLIDATION_REQUESTS_PER_PAYLOAD 2

// Constants previously in beacon_denep.c (potentially static, or should be in a shared header if not already)
#define MAX_PROPOSER_SLASHINGS         16   // Re-defined here if not accessible via common header
#define MAX_DEPOSITS                   16   // Re-defined here
#define MAX_VOLUNTARY_EXITS            16   // Re-defined here
#define MAX_BLS_TO_EXECUTION_CHANGES   16   // Re-defined here
#define MAX_BLOB_COMMITMENTS_PER_BLOCK 4096 // List capacity for blobKzgCommitments

// --- Helper Type Definitions ---
static const ssz_def_t ELECTRA_KZG_COMMITMENT_TYPE = SSZ_BYTE_VECTOR("KZGCommitment", 48);

// --- Field Arrays for SSZ Structures ---

static const ssz_def_t ELECTRA_CHECKPOINT_FIELDS[] = {
    SSZ_UINT64("epoch"),
    SSZ_BYTES32("root")};

static const ssz_def_t ELECTRA_ATTESTATION_DATA_FIELDS[] = {
    SSZ_UINT64("slot"),
    SSZ_UINT64("index"), // CommitteeIndex
    SSZ_BYTES32("beaconBlockRoot"),
    SSZ_CONTAINER("source", ELECTRA_CHECKPOINT_FIELDS),
    SSZ_CONTAINER("target", ELECTRA_CHECKPOINT_FIELDS)};

static const ssz_def_t ELECTRA_SYNC_AGGREGATE_FIELDS[] = {
    SSZ_BIT_VECTOR("syncCommitteeBits", 512),
    SSZ_BYTE_VECTOR("syncCommitteeSignature", 96) // Was SSZ_BLS_SIGNATURE
};

static const ssz_def_t ELECTRA_ETH1_DATA_FIELDS[] = {
    SSZ_BYTES32("depositRoot"),
    SSZ_UINT64("depositCount"),
    SSZ_BYTES32("blockHash"),
};

// BEACON_BLOCK_HEADER is global in beacon_denep.c (via beacon_denep.h)
static const ssz_def_t ELECTRA_SIGNED_BEACON_BLOCKHEADER_FIELDS[] = {
    SSZ_CONTAINER("message", BEACON_BLOCK_HEADER),
    SSZ_BYTE_VECTOR("signature", 96) // Was SSZ_BLS_SIGNATURE
};

static const ssz_def_t ELECTRA_PROPOSER_SLASHING_FIELDS[] = {
    SSZ_CONTAINER("signedHeader1", ELECTRA_SIGNED_BEACON_BLOCKHEADER_FIELDS),
    SSZ_CONTAINER("signedHeader2", ELECTRA_SIGNED_BEACON_BLOCKHEADER_FIELDS),
};
static const ssz_def_t ELECTRA_PROPOSER_SLASHING_DEF = SSZ_CONTAINER("ProposerSlashing", ELECTRA_PROPOSER_SLASHING_FIELDS);

static const ssz_def_t ELECTRA_DEPOSIT_DATA_FIELDS[] = {
    SSZ_BYTE_VECTOR("pubkey", 48), // Was SSZ_BLS_PUBKEY
    SSZ_BYTES32("withdrawalCredentials"),
    SSZ_UINT64("amount"),
    SSZ_BYTE_VECTOR("signature", 96), // Was SSZ_BLS_SIGNATURE
};

static const ssz_def_t ELECTRA_DEPOSIT_FIELDS[] = {
    SSZ_VECTOR("proof", ssz_bytes32, 33), // Assumes ssz_bytes32 is globally defined (e.g. in ssz.h or beacon_types.h)
    SSZ_CONTAINER("data", ELECTRA_DEPOSIT_DATA_FIELDS),
};
static const ssz_def_t ELECTRA_DEPOSIT_DEF = SSZ_CONTAINER("Deposit", ELECTRA_DEPOSIT_FIELDS);

static const ssz_def_t ELECTRA_VOLUNTARY_EXIT_FIELDS[] = {
    SSZ_UINT64("epoch"),
    SSZ_UINT64("validatorIndex"),
};

static const ssz_def_t ELECTRA_SIGNED_VOLUNTARY_EXIT_FIELDS[] = {
    SSZ_CONTAINER("message", ELECTRA_VOLUNTARY_EXIT_FIELDS),
    SSZ_BYTE_VECTOR("signature", 96), // Was SSZ_BLS_SIGNATURE
};
static const ssz_def_t ELECTRA_SIGNED_VOLUNTARY_EXIT_DEF = SSZ_CONTAINER("SignedVoluntaryExit", ELECTRA_SIGNED_VOLUNTARY_EXIT_FIELDS);

static const ssz_def_t ELECTRA_BLS_TO_EXECUTION_CHANGE_FIELDS[] = {
    SSZ_UINT64("validatorIndex"),
    SSZ_BYTE_VECTOR("fromBlsPubkey", 48), // Was SSZ_BLS_PUBKEY
    SSZ_ADDRESS("toExecutionAddress"),
};

static const ssz_def_t ELECTRA_SIGNED_BLS_TO_EXECUTION_CHANGE_FIELDS[] = {
    SSZ_CONTAINER("message", ELECTRA_BLS_TO_EXECUTION_CHANGE_FIELDS),
    SSZ_BYTE_VECTOR("signature", 96), // Was SSZ_BLS_SIGNATURE
};
static const ssz_def_t ELECTRA_SIGNED_BLS_TO_EXECUTION_CHANGE_DEF = SSZ_CONTAINER("SignedBlsToExecutionChange", ELECTRA_SIGNED_BLS_TO_EXECUTION_CHANGE_FIELDS);

// --- New Electra Specific Containers ---

static const ssz_def_t ELECTRA_DEPOSIT_REQUEST_FIELDS[] = {
    SSZ_BYTE_VECTOR("pubkey", 48), // Was SSZ_BLS_PUBKEY
    SSZ_BYTES32("withdrawalCredentials"),
    SSZ_UINT64("amount"),             // Gwei
    SSZ_BYTE_VECTOR("signature", 96), // Was SSZ_BLS_SIGNATURE
    SSZ_UINT64("index")};
static const ssz_def_t ELECTRA_DEPOSIT_REQUEST_DEF = SSZ_CONTAINER("DepositRequest", ELECTRA_DEPOSIT_REQUEST_FIELDS);

static const ssz_def_t ELECTRA_WITHDRAWAL_REQUEST_FIELDS[] = {
    SSZ_ADDRESS("sourceAddress"),
    SSZ_BYTE_VECTOR("validatorPubkey", 48), // Was SSZ_BLS_PUBKEY
    SSZ_UINT64("amount")                    // Gwei
};
static const ssz_def_t ELECTRA_WITHDRAWAL_REQUEST_DEF = SSZ_CONTAINER("WithdrawalRequest", ELECTRA_WITHDRAWAL_REQUEST_FIELDS);

static const ssz_def_t ELECTRA_CONSOLIDATION_REQUEST_FIELDS[] = {
    SSZ_ADDRESS("sourceAddress"),
    SSZ_BYTE_VECTOR("sourcePubkey", 48), // Was SSZ_BLS_PUBKEY
    SSZ_BYTE_VECTOR("targetPubkey", 48)  // Was SSZ_BLS_PUBKEY
};
static const ssz_def_t ELECTRA_CONSOLIDATION_REQUEST_DEF = SSZ_CONTAINER("ConsolidationRequest", ELECTRA_CONSOLIDATION_REQUEST_FIELDS);

static const ssz_def_t ELECTRA_EXECUTION_REQUESTS_FIELDS[] = {
    SSZ_LIST("deposits", ELECTRA_DEPOSIT_REQUEST_DEF, MAX_DEPOSIT_REQUESTS_PER_PAYLOAD),
    SSZ_LIST("withdrawals", ELECTRA_WITHDRAWAL_REQUEST_DEF, MAX_WITHDRAWAL_REQUESTS_PER_PAYLOAD),
    SSZ_LIST("consolidations", ELECTRA_CONSOLIDATION_REQUEST_DEF, MAX_CONSOLIDATION_REQUESTS_PER_PAYLOAD)};

// --- Modified Electra Containers (due to EIP-7549) ---

static const ssz_def_t ELECTRA_INDEXED_ATTESTATION_FIELDS[] = {
    SSZ_LIST("attestingIndices", ssz_uint64, 2048), // List of ValidatorIndex (ssz_uint64 assumed for ValidatorIndex)
    SSZ_CONTAINER("data", ELECTRA_ATTESTATION_DATA_FIELDS),
    SSZ_BYTE_VECTOR("signature", 96) // Was SSZ_BLS_SIGNATURE
};
// IndexedAttestation is embedded, but also part of AttesterSlashing which is a list item, so a _DEF is useful.
static const ssz_def_t ELECTRA_INDEXED_ATTESTATION_DEF = SSZ_CONTAINER("IndexedAttestation", ELECTRA_INDEXED_ATTESTATION_FIELDS);

static const ssz_def_t ELECTRA_ATTESTER_SLASHING_FIELDS[] = {
    SSZ_CONTAINER("attestation1", ELECTRA_INDEXED_ATTESTATION_FIELDS), // Embed directly
    SSZ_CONTAINER("attestation2", ELECTRA_INDEXED_ATTESTATION_FIELDS)  // Embed directly
};
static const ssz_def_t ELECTRA_ATTESTER_SLASHING_DEF = SSZ_CONTAINER("AttesterSlashing", ELECTRA_ATTESTER_SLASHING_FIELDS);

static const ssz_def_t ELECTRA_ATTESTATION_FIELDS[] = {
    SSZ_BIT_LIST("aggregationBits", 2048), // MAX_VALIDATORS_PER_COMMITTEE * MAX_COMMITTEES_PER_SLOT
    SSZ_CONTAINER("data", ELECTRA_ATTESTATION_DATA_FIELDS),
    SSZ_BYTE_VECTOR("signature", 96),   // Was SSZ_BLS_SIGNATURE
    SSZ_BIT_VECTOR("committeeBits", 64) // MAX_COMMITTEES_PER_SLOT
};
static const ssz_def_t ELECTRA_ATTESTATION_DEF = SSZ_CONTAINER("Attestation", ELECTRA_ATTESTATION_FIELDS);

// --- Electra BeaconBlockBody ---
// DENEP_EXECUTION_PAYLOAD is global in beacon_denep.c (via beacon_denep.h)
static const ssz_def_t ELECTRA_BEACON_BLOCK_BODY_FIELDS[] = {
    SSZ_BYTE_VECTOR("randaoReveal", 96), // Was SSZ_BLS_SIGNATURE
    SSZ_CONTAINER("eth1Data", ELECTRA_ETH1_DATA_FIELDS),
    SSZ_BYTES32("graffiti"),
    SSZ_LIST("proposerSlashings", ELECTRA_PROPOSER_SLASHING_DEF, MAX_PROPOSER_SLASHINGS),
    SSZ_LIST("attesterSlashings", ELECTRA_ATTESTER_SLASHING_DEF, MAX_ATTESTER_SLASHINGS_ELECTRA),
    SSZ_LIST("attestations", ELECTRA_ATTESTATION_DEF, MAX_ATTESTATIONS_ELECTRA),
    SSZ_LIST("deposits", ELECTRA_DEPOSIT_DEF, MAX_DEPOSITS),
    SSZ_LIST("voluntaryExits", ELECTRA_SIGNED_VOLUNTARY_EXIT_DEF, MAX_VOLUNTARY_EXITS),
    SSZ_CONTAINER("syncAggregate", ELECTRA_SYNC_AGGREGATE_FIELDS),
    SSZ_CONTAINER("executionPayload", DENEP_EXECUTION_PAYLOAD),
    SSZ_LIST("blsToExecutionChanges", ELECTRA_SIGNED_BLS_TO_EXECUTION_CHANGE_DEF, MAX_BLS_TO_EXECUTION_CHANGES),
    SSZ_LIST("blobKzgCommitments", ELECTRA_KZG_COMMITMENT_TYPE, MAX_BLOB_COMMITMENTS_PER_BLOCK),
    SSZ_CONTAINER("executionRequests", ELECTRA_EXECUTION_REQUESTS_FIELDS)};
static const ssz_def_t ELECTRA_BEACON_BLOCK_BODY_DEF = SSZ_CONTAINER("BeaconBlockBody", ELECTRA_BEACON_BLOCK_BODY_FIELDS);

// --- Electra BeaconBlock ---
static const ssz_def_t ELECTRA_BEACON_BLOCK_FIELDS[] = {
    SSZ_UINT64("slot"),
    SSZ_UINT64("proposerIndex"),
    SSZ_BYTES32("parentRoot"),
    SSZ_BYTES32("stateRoot"),
    SSZ_CONTAINER("body", ELECTRA_BEACON_BLOCK_BODY_FIELDS)};

// --- Electra SignedBeaconBlock ---
static const ssz_def_t ELECTRA_SIGNED_BEACON_BLOCK_FIELDS[] = {
    SSZ_CONTAINER("message", ELECTRA_BEACON_BLOCK_FIELDS),
    SSZ_BYTE_VECTOR("signature", 96) // Was SSZ_BLS_SIGNATURE
};
static const ssz_def_t ELECTRA_SIGNED_BEACON_BLOCK_DEF = SSZ_CONTAINER("SignedBeaconBlock", ELECTRA_SIGNED_BEACON_BLOCK_FIELDS);

// --- Main function to get Electra SSZ types ---
const ssz_def_t* eth_ssz_type_for_electra(eth_ssz_type_t type) {
  switch (type) {
    case ETH_SSZ_BEACON_BLOCK_BODY_CONTAINER:
      return &ELECTRA_BEACON_BLOCK_BODY_DEF;
    case ETH_SSZ_SIGNED_BEACON_BLOCK_CONTAINER:
      return &ELECTRA_SIGNED_BEACON_BLOCK_DEF;
    // BlockHeader is unchanged from Deneb as per user request.
    // eth_ssz_type_for_denep will be called for it.
    default:
      // Fallback to Deneb for any other types.
      // This includes ETH_SSZ_BEACON_BLOCK_HEADER etc.
      return eth_ssz_type_for_denep(type);
  }
}