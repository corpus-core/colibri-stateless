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
// description: The SSZ types for the Beacon chain for the Gloas Fork (EIP-7688 + EIP-7732 ePBS).

// Gloas SSZ types from `build/consensus-specs/specs/gloas/`. Mainnet / Sepolia /
// Gnosis still have `fork_epochs[C4_FORK_GLOAS-1] == NOT_ASSIGNED_YET`; Platåberget
// activates Gloas at epoch 1536, so these definitions are reachable there.
// See `specs/gloas/beacon-chain.md` and `specs/gloas/light-client/sync-protocol.md`.

#include "beacon_types.h"
#include "ssz.h" // Ensure ssz_uint64_def is available

// -----------------------------------------------------------------------------
// Gloas Light Client types (always compiled, needed by the verifier)
// -----------------------------------------------------------------------------
//
// The Gloas `LightClientHeader` no longer embeds a full `ExecutionPayloadHeader`
// (removed by EIP-7732 since the block body no longer contains a payload).
// Instead it carries only the execution `block_hash` plus a Merkle branch that
// proves this hash against `body.signed_execution_payload_bid.message.parent_block_hash`
// (gindex 2856, depth 11).

const ssz_def_t GLOAS_LIGHT_CLIENT_HEADER[3] = {
    SSZ_CONTAINER("beacon", BEACON_BLOCK_HEADER),    // the header of the beacon block
    SSZ_BYTES32("executionBlockHash"),               // Hash32 of the execution block (replaces full ExecutionPayloadHeader)
    SSZ_VECTOR("executionBranch", ssz_bytes32, 11)}; // Merkle branch proving execution_block_hash against body_root

// Gloas gindices (from specs/gloas/light-client/sync-protocol.md):
//   CURRENT_SYNC_COMMITTEE_GINDEX_GLOAS = 2945 (floorlog2 = 11)
//   NEXT_SYNC_COMMITTEE_GINDEX_GLOAS    = 2946 (floorlog2 = 11)
//   FINALIZED_ROOT_GINDEX_GLOAS         = 735  (floorlog2 = 9)
const ssz_def_t GLOAS_LIGHT_CLIENT_BOOTSTRAP[3] = {
    SSZ_CONTAINER("header", GLOAS_LIGHT_CLIENT_HEADER),
    SSZ_CONTAINER("currentSyncCommittee", SYNC_COMMITTEE),
    SSZ_VECTOR("currentSyncCommitteeBranch", ssz_bytes32, 11)}; // depth 11 in Gloas

const ssz_def_t GLOAS_LIGHT_CLIENT_UPDATE[7] = {
    SSZ_CONTAINER("attestedHeader", GLOAS_LIGHT_CLIENT_HEADER),
    SSZ_CONTAINER("nextSyncCommittee", SYNC_COMMITTEE),
    SSZ_VECTOR("nextSyncCommitteeBranch", ssz_bytes32, 11), // depth 11 in Gloas
    SSZ_CONTAINER("finalizedHeader", GLOAS_LIGHT_CLIENT_HEADER),
    SSZ_VECTOR("finalityBranch", ssz_bytes32, 9), // depth 9 in Gloas
    SSZ_CONTAINER("syncAggregate", SYNC_AGGREGATE),
    SSZ_UINT64("signatureSlot")};

// -----------------------------------------------------------------------------
// Prover-only definitions: full block / bid / envelope parsing
// -----------------------------------------------------------------------------

#ifdef PROVER

// Gloas Presets (specs/gloas/beacon-chain.md, presets/mainnet/gloas.yaml)
#define GLOAS_PTC_SIZE 512 // EIP-7732 payload timeliness committee size

// EIP-7688 Progressive lists / containers no longer carry a capacity limit at
// merkleization time, so we do not need to track the pre-Gloas `MAX_*` values
// (they were only relevant for the fixed-limit Electra `SSZ_LIST` variants).

// --- shared child types (identical to Electra, kept local for readability) ---

static const ssz_def_t CHECKPOINT[] = {
    SSZ_UINT64("epoch"),
    SSZ_BYTES32("root")};

static const ssz_def_t ATTESTATION_DATA[] = {
    SSZ_UINT64("slot"),
    SSZ_UINT64("index"),
    SSZ_BYTES32("beaconBlockRoot"),
    SSZ_CONTAINER("source", CHECKPOINT),
    SSZ_CONTAINER("target", CHECKPOINT)};

static const ssz_def_t ETH1_DATA[] = {
    SSZ_BYTES32("depositRoot"),
    SSZ_UINT64("depositCount"),
    SSZ_BYTES32("blockHash")};

// --- Progressive lists (EIP-7688) shared building blocks ---

// bytes-like transactions payload (progressive byte list per Electra layout)
static const ssz_def_t TRANSACTION_BYTES = SSZ_PROG_BYTES("Transaction");

// --- ExecutionRequests (Gloas): 5 fields incl. builder deposits/exits (EIP-8282) ---

static const ssz_def_t DEPOSIT_REQUEST[] = {
    SSZ_BYTE_VECTOR("pubkey", 48),
    SSZ_BYTES32("withdrawalCredentials"),
    SSZ_UINT64("amount"),
    SSZ_BYTE_VECTOR("signature", 96),
    SSZ_UINT64("index")};
static const ssz_def_t GLOAS_DEPOSIT_REQUEST_CONTAINER = SSZ_CONTAINER("DepositRequest", DEPOSIT_REQUEST);

static const ssz_def_t WITHDRAWAL_REQUEST[] = {
    SSZ_ADDRESS("sourceAddress"),
    SSZ_BYTE_VECTOR("validatorPubkey", 48),
    SSZ_UINT64("amount")};
static const ssz_def_t GLOAS_WITHDRAWAL_REQUEST_CONTAINER = SSZ_CONTAINER("WithdrawalRequest", WITHDRAWAL_REQUEST);

static const ssz_def_t CONSOLIDATION_REQUEST[] = {
    SSZ_ADDRESS("sourceAddress"),
    SSZ_BYTE_VECTOR("sourcePubkey", 48),
    SSZ_BYTE_VECTOR("targetPubkey", 48)};
static const ssz_def_t GLOAS_CONSOLIDATION_REQUEST_CONTAINER = SSZ_CONTAINER("ConsolidationRequest", CONSOLIDATION_REQUEST);

// [New in Gloas:EIP8282]
static const ssz_def_t BUILDER_DEPOSIT_REQUEST[] = {
    SSZ_BYTE_VECTOR("pubkey", 48),
    SSZ_BYTES32("withdrawalCredentials"),
    SSZ_UINT64("amount"),
    SSZ_BYTE_VECTOR("signature", 96)};
static const ssz_def_t GLOAS_BUILDER_DEPOSIT_REQUEST_CONTAINER = SSZ_CONTAINER("BuilderDepositRequest", BUILDER_DEPOSIT_REQUEST);

// [New in Gloas:EIP8282]
static const ssz_def_t BUILDER_EXIT_REQUEST[] = {
    SSZ_ADDRESS("sourceAddress"),
    SSZ_BYTE_VECTOR("pubkey", 48)};
static const ssz_def_t GLOAS_BUILDER_EXIT_REQUEST_CONTAINER = SSZ_CONTAINER("BuilderExitRequest", BUILDER_EXIT_REQUEST);

// Base container for ExecutionRequests (progressive: active_mask = 0b11111 = 5 fields)
static const ssz_def_t EXECUTION_REQUESTS_BASE[] = {
    SSZ_PROG_LIST("deposits", GLOAS_DEPOSIT_REQUEST_CONTAINER),
    SSZ_PROG_LIST("withdrawals", GLOAS_WITHDRAWAL_REQUEST_CONTAINER),
    SSZ_PROG_LIST("consolidations", GLOAS_CONSOLIDATION_REQUEST_CONTAINER),
    SSZ_PROG_LIST("builderDeposits", GLOAS_BUILDER_DEPOSIT_REQUEST_CONTAINER),
    SSZ_PROG_LIST("builderExits", GLOAS_BUILDER_EXIT_REQUEST_CONTAINER)};
static const ssz_def_t EXECUTION_REQUESTS_BASE_CONTAINER = SSZ_CONTAINER("ExecutionRequestsBase", EXECUTION_REQUESTS_BASE);
// Kept as an addressable standalone container for future callers (dispatcher /
// dumper). Currently only referenced inline via SSZ_PROG_CONTAINER, so mark it
// as intentionally unused for the linker.
static const ssz_def_t GLOAS_EXECUTION_REQUESTS_CONTAINER C4_UNUSED =
    SSZ_PROG_CONTAINER("ExecutionRequests", EXECUTION_REQUESTS_BASE_CONTAINER, 0x1FULL); // active_mask = [1]*5

// --- Attestation / IndexedAttestation (EIP-7688 progressive containers) ---
//
// EIP-7688 keeps the same field layout as Electra's Attestation but promotes
// the bit list and index list to progressive types and wraps the container.

static const ssz_def_t GLOAS_INDEX_ATTESTATION_BASE[] = {
    SSZ_PROG_LIST("attestingIndices", ssz_uint64_def),
    SSZ_CONTAINER("data", ATTESTATION_DATA),
    SSZ_BYTE_VECTOR("signature", 96)};
static const ssz_def_t                                   GLOAS_INDEX_ATTESTATION_BASE_CONTAINER = SSZ_CONTAINER("IndexedAttestationBase", GLOAS_INDEX_ATTESTATION_BASE);
static const ssz_def_t GLOAS_INDEX_ATTESTATION_CONTAINER C4_UNUSED =
    SSZ_PROG_CONTAINER("IndexedAttestation", GLOAS_INDEX_ATTESTATION_BASE_CONTAINER, 0x7ULL); // [1]*3

static const ssz_def_t GLOAS_ATTESTATION_BASE[] = {
    SSZ_PROG_BIT_LIST("aggregationBits"),
    SSZ_CONTAINER("data", ATTESTATION_DATA),
    SSZ_BYTE_VECTOR("signature", 96),
    SSZ_BIT_VECTOR("committeeBits", 64)}; // MAX_COMMITTEES_PER_SLOT = 64
static const ssz_def_t GLOAS_ATTESTATION_BASE_CONTAINER = SSZ_CONTAINER("AttestationBase", GLOAS_ATTESTATION_BASE);
static const ssz_def_t GLOAS_ATTESTATION_CONTAINER =
    SSZ_PROG_CONTAINER("Attestation", GLOAS_ATTESTATION_BASE_CONTAINER, 0xFULL); // [1]*4

// --- ProposerSlashing / AttesterSlashing (indexed attestations wrapped) ---

static const ssz_def_t SIGNED_BEACON_BLOCKHEADER[] = {
    SSZ_CONTAINER("message", BEACON_BLOCK_HEADER),
    SSZ_BYTE_VECTOR("signature", 96)};

static const ssz_def_t PROPOSER_SLASHING[] = {
    SSZ_CONTAINER("signedHeader1", SIGNED_BEACON_BLOCKHEADER),
    SSZ_CONTAINER("signedHeader2", SIGNED_BEACON_BLOCKHEADER)};
static const ssz_def_t GLOAS_PROPOSER_SLASHING_CONTAINER = SSZ_CONTAINER("proposerSlashing", PROPOSER_SLASHING);

// IndexedAttestation is a ProgressiveContainer in Gloas; embed via SSZ_PROG_CONTAINER.
// Field names follow the consensus spec (`attestation_1`, `attestation_2`).
static const ssz_def_t ATTESTER_SLASHING[] = {
    SSZ_PROG_CONTAINER("attestation1", GLOAS_INDEX_ATTESTATION_BASE_CONTAINER, 0x7ULL),
    SSZ_PROG_CONTAINER("attestation2", GLOAS_INDEX_ATTESTATION_BASE_CONTAINER, 0x7ULL)};
static const ssz_def_t GLOAS_ATTESTER_SLASHING_CONTAINER = SSZ_CONTAINER("attesterSlashing", ATTESTER_SLASHING);

// --- Deposit / VoluntaryExit / BlsToExecutionChange (unchanged from Electra) ---

static const ssz_def_t DEPOSIT_DATA[] = {
    SSZ_BYTE_VECTOR("pubkey", 48),
    SSZ_BYTES32("withdrawalCredentials"),
    SSZ_UINT64("amount"),
    SSZ_BYTE_VECTOR("signature", 96)};

static const ssz_def_t DEPOSIT[] = {
    SSZ_VECTOR("proof", ssz_bytes32, 33),
    SSZ_CONTAINER("data", DEPOSIT_DATA)};
static const ssz_def_t GLOAS_DEPOSIT_CONTAINER = SSZ_CONTAINER("deposit", DEPOSIT);

static const ssz_def_t VOLUNTARY_EXIT[] = {
    SSZ_UINT64("epoch"),
    SSZ_UINT64("validatorIndex")};

static const ssz_def_t SIGNED_VOLUNTARY_EXIT[] = {
    SSZ_CONTAINER("message", VOLUNTARY_EXIT),
    SSZ_BYTE_VECTOR("signature", 96)};
static const ssz_def_t GLOAS_SIGNED_VOLUNTARY_EXIT_CONTAINER = SSZ_CONTAINER("signedVoluntaryExit", SIGNED_VOLUNTARY_EXIT);

static const ssz_def_t BLS_TO_EXECUTION_CHANGE[] = {
    SSZ_UINT64("validatorIndex"),
    SSZ_BYTE_VECTOR("fromBlsPubkey", 48),
    SSZ_ADDRESS("toExecutionAddress")};

static const ssz_def_t SIGNED_BLS_TO_EXECUTION_CHANGE[] = {
    SSZ_CONTAINER("message", BLS_TO_EXECUTION_CHANGE),
    SSZ_BYTE_VECTOR("signature", 96)};
static const ssz_def_t GLOAS_SIGNED_BLS_TO_EXECUTION_CHANGE_CONTAINER =
    SSZ_CONTAINER("signedBlsToExecutionChange", SIGNED_BLS_TO_EXECUTION_CHANGE);

// --- ExecutionPayload (Gloas): progressive container with 19 fields ---
//
// [Modified in Gloas:EIP7688] transactions/withdrawals become progressive lists.
// [New in Gloas:EIP7928] blockAccessList (a progressive byte list).
// [New in Gloas:EIP7843] slotNumber.

static const ssz_def_t WITHDRAWAL[] = {
    SSZ_UINT64("index"),
    SSZ_UINT64("validatorIndex"),
    SSZ_ADDRESS("address"),
    SSZ_UINT64("amount")};
static const ssz_def_t GLOAS_WITHDRAWAL_CONTAINER = SSZ_CONTAINER("withdrawal", WITHDRAWAL);

static const ssz_def_t GLOAS_EXECUTION_PAYLOAD_BASE[] = {
    SSZ_BYTES32("parentHash"),
    SSZ_ADDRESS("feeRecipient"),
    SSZ_BYTES32("stateRoot"),
    SSZ_BYTES32("receiptsRoot"),
    SSZ_BYTE_VECTOR("logsBloom", 256),
    SSZ_BYTES32("prevRandao"),
    SSZ_UINT64("blockNumber"),
    SSZ_UINT64("gasLimit"),
    SSZ_UINT64("gasUsed"),
    SSZ_UINT64("timestamp"),
    SSZ_BYTES("extraData", 32), // ExtraData = ByteList[32] (unchanged since Bellatrix)
    SSZ_UINT256("baseFeePerGas"),
    SSZ_BYTES32("blockHash"),
    SSZ_PROG_LIST("transactions", TRANSACTION_BYTES),         // [Modified in Gloas:EIP7688]
    SSZ_PROG_LIST("withdrawals", GLOAS_WITHDRAWAL_CONTAINER), // [Modified in Gloas:EIP7688]
    SSZ_UINT64("blobGasUsed"),
    SSZ_UINT64("excessBlobGas"),
    SSZ_PROG_BYTES("blockAccessList"), // [New in Gloas:EIP7928] BlockAccessList = ProgressiveByteList
    SSZ_UINT64("slotNumber")};         // [New in Gloas:EIP7843]
static const ssz_def_t GLOAS_EXECUTION_PAYLOAD_BASE_CONTAINER =
    SSZ_CONTAINER("ExecutionPayloadBase", GLOAS_EXECUTION_PAYLOAD_BASE);
// Standalone container for future dispatchers/dumpers; the actual body/envelope
// embeddings use `SSZ_PROG_CONTAINER` inline against the same base container.
static const ssz_def_t GLOAS_EXECUTION_PAYLOAD_CONTAINER C4_UNUSED =
    SSZ_PROG_CONTAINER("ExecutionPayload", GLOAS_EXECUTION_PAYLOAD_BASE_CONTAINER, 0x7FFFFULL); // [1]*19

// --- ExecutionPayloadBid / SignedExecutionPayloadBid ---

static const ssz_def_t GLOAS_EXECUTION_PAYLOAD_BID_BASE[] = {
    SSZ_BYTES32("parentBlockHash"),
    SSZ_BYTES32("parentBlockRoot"),
    SSZ_BYTES32("blockHash"),
    SSZ_BYTES32("prevRandao"),
    SSZ_ADDRESS("feeRecipient"),
    SSZ_UINT64("gasLimit"),
    SSZ_UINT64("builderIndex"),
    SSZ_UINT64("slot"),
    SSZ_UINT64("value"),
    SSZ_UINT64("executionPayment"),
    SSZ_PROG_LIST("blobKzgCommitments", ssz_bls_pubky), // [Modified in Gloas:EIP7688]
    SSZ_BYTES32("executionRequestsRoot")};
static const ssz_def_t GLOAS_EXECUTION_PAYLOAD_BID_BASE_CONTAINER =
    SSZ_CONTAINER("ExecutionPayloadBidBase", GLOAS_EXECUTION_PAYLOAD_BID_BASE);
static const ssz_def_t GLOAS_EXECUTION_PAYLOAD_BID_CONTAINER =
    SSZ_PROG_CONTAINER("ExecutionPayloadBid", GLOAS_EXECUTION_PAYLOAD_BID_BASE_CONTAINER, 0xFFFULL); // [1]*12

static const ssz_def_t GLOAS_SIGNED_EXECUTION_PAYLOAD_BID[] = {
    SSZ_PROG_CONTAINER("message", GLOAS_EXECUTION_PAYLOAD_BID_BASE_CONTAINER, 0xFFFULL), // [1]*12
    SSZ_BYTE_VECTOR("signature", 96)};

// --- PayloadAttestation ---

static const ssz_def_t GLOAS_PAYLOAD_ATTESTATION_DATA[] = {
    SSZ_BYTES32("beaconBlockRoot"),
    SSZ_UINT64("slot"),
    SSZ_BOOLEAN("payloadPresent"),
    SSZ_BOOLEAN("blobDataAvailable")};

static const ssz_def_t GLOAS_PAYLOAD_ATTESTATION_BASE[] = {
    SSZ_BIT_VECTOR("aggregationBits", GLOAS_PTC_SIZE), // PTCBits = BitVector[PTC_SIZE=512]
    SSZ_CONTAINER("data", GLOAS_PAYLOAD_ATTESTATION_DATA),
    SSZ_BYTE_VECTOR("signature", 96)};
static const ssz_def_t GLOAS_PAYLOAD_ATTESTATION_BASE_CONTAINER =
    SSZ_CONTAINER("PayloadAttestationBase", GLOAS_PAYLOAD_ATTESTATION_BASE);
static const ssz_def_t GLOAS_PAYLOAD_ATTESTATION_CONTAINER =
    SSZ_PROG_CONTAINER("PayloadAttestation", GLOAS_PAYLOAD_ATTESTATION_BASE_CONTAINER, 0x7ULL); // [1]*3

// --- ExecutionPayloadEnvelope / SignedExecutionPayloadEnvelope ---
//
// Embedded progressive containers use the SSZ_PROG_CONTAINER macro inline so
// the parent field list is a valid `ssz_def_t[]` (SSZ_CONTAINER expects a
// fields array as its second argument).

static const ssz_def_t GLOAS_EXECUTION_PAYLOAD_ENVELOPE_BASE[] = {
    SSZ_PROG_CONTAINER("payload", GLOAS_EXECUTION_PAYLOAD_BASE_CONTAINER, 0x7FFFFULL),
    SSZ_PROG_CONTAINER("executionRequests", EXECUTION_REQUESTS_BASE_CONTAINER, 0x1FULL),
    SSZ_UINT64("builderIndex"),
    SSZ_BYTES32("beaconBlockRoot"),
    SSZ_BYTES32("parentBeaconBlockRoot")};
static const ssz_def_t GLOAS_EXECUTION_PAYLOAD_ENVELOPE_BASE_CONTAINER =
    SSZ_CONTAINER("ExecutionPayloadEnvelopeBase", GLOAS_EXECUTION_PAYLOAD_ENVELOPE_BASE);
static const ssz_def_t GLOAS_EXECUTION_PAYLOAD_ENVELOPE_CONTAINER C4_UNUSED =
    SSZ_PROG_CONTAINER("ExecutionPayloadEnvelope", GLOAS_EXECUTION_PAYLOAD_ENVELOPE_BASE_CONTAINER, 0x1FULL); // [1]*5

static const ssz_def_t GLOAS_SIGNED_EXECUTION_PAYLOAD_ENVELOPE[] C4_UNUSED = {
    SSZ_PROG_CONTAINER("message", GLOAS_EXECUTION_PAYLOAD_ENVELOPE_BASE_CONTAINER, 0x1FULL),
    SSZ_BYTE_VECTOR("signature", 96)};

// --- BeaconBlockBody (Gloas): progressive container with 13 fields ---
//
// EIP-7732 removed execution_payload, blob_kzg_commitments and execution_requests
// from the body; they now live in the (separate) ExecutionPayloadEnvelope, while
// the body carries `signed_execution_payload_bid`, `payload_attestations` and
// `parent_execution_requests` instead.

static const ssz_def_t GLOAS_BEACON_BLOCK_BODY_BASE[] = {
    SSZ_BYTE_VECTOR("randaoReveal", 96),
    SSZ_CONTAINER("eth1Data", ETH1_DATA),
    SSZ_BYTES32("graffiti"),
    SSZ_PROG_LIST("proposerSlashings", GLOAS_PROPOSER_SLASHING_CONTAINER),
    SSZ_PROG_LIST("attesterSlashings", GLOAS_ATTESTER_SLASHING_CONTAINER),
    SSZ_PROG_LIST("attestations", GLOAS_ATTESTATION_CONTAINER),
    SSZ_PROG_LIST("deposits", GLOAS_DEPOSIT_CONTAINER),
    SSZ_PROG_LIST("voluntaryExits", GLOAS_SIGNED_VOLUNTARY_EXIT_CONTAINER),
    SSZ_CONTAINER("syncAggregate", SYNC_AGGREGATE),
    SSZ_PROG_LIST("blsToExecutionChanges", GLOAS_SIGNED_BLS_TO_EXECUTION_CHANGE_CONTAINER),
    // SignedExecutionPayloadBid is a regular Container whose `message` is the
    // ProgressiveContainer Bid; SSZ_CONTAINER on the fields array is correct.
    SSZ_CONTAINER("signedExecutionPayloadBid", GLOAS_SIGNED_EXECUTION_PAYLOAD_BID),           // [New in Gloas:EIP7732]
    SSZ_PROG_LIST("payloadAttestations", GLOAS_PAYLOAD_ATTESTATION_CONTAINER),                // [New in Gloas:EIP7732]
    SSZ_PROG_CONTAINER("parentExecutionRequests", EXECUTION_REQUESTS_BASE_CONTAINER, 0x1FULL) // [New in Gloas:EIP7732]
};
static const ssz_def_t GLOAS_BEACON_BLOCK_BODY_BASE_CONTAINER =
    SSZ_CONTAINER("BeaconBlockBodyBase", GLOAS_BEACON_BLOCK_BODY_BASE);
static const ssz_def_t GLOAS_BEACON_BLOCK_BODY_CONTAINER =
    SSZ_PROG_CONTAINER("beaconBlockBody", GLOAS_BEACON_BLOCK_BODY_BASE_CONTAINER, 0x1FFFULL); // [1]*13

static const ssz_def_t GLOAS_BEACON_BLOCK[] = {
    SSZ_UINT64("slot"),
    SSZ_UINT64("proposerIndex"),
    SSZ_BYTES32("parentRoot"),
    SSZ_BYTES32("stateRoot"),
    SSZ_PROG_CONTAINER("body", GLOAS_BEACON_BLOCK_BODY_BASE_CONTAINER, 0x1FFFULL)};

static const ssz_def_t GLOAS_SIGNED_BEACON_BLOCK[] = {
    SSZ_CONTAINER("message", GLOAS_BEACON_BLOCK),
    SSZ_BYTE_VECTOR("signature", 96)};
static const ssz_def_t GLOAS_SIGNED_BEACON_BLOCK_CONTAINER =
    SSZ_CONTAINER("signedBeaconBlock", GLOAS_SIGNED_BEACON_BLOCK);

// Note on Gnosis chains: Gloas removes the withdrawals capacity limit
// (progressive list), so the DENEP/ELECTRA `_GNOSIS` variants (which only
// differed by `LIMIT_WITHDRAWELS_GNOSIS=8` in the Deneb payload) collapse into
// one container. PTC_SIZE for Gnosis remains an open item (assumed 512, same as
// mainnet) and would need adjustment via a chain-specific preset once Gnosis
// publishes its Gloas preset.

#endif // PROVER

// -----------------------------------------------------------------------------
// Public dispatcher
// -----------------------------------------------------------------------------

const ssz_def_t* eth_ssz_type_for_gloas(eth_ssz_type_t type, chain_id_t chain_id) {
  switch (type) {
#ifdef PROVER
    case ETH_SSZ_BEACON_BLOCK_BODY_CONTAINER:
      return &GLOAS_BEACON_BLOCK_BODY_CONTAINER;
    case ETH_SSZ_SIGNED_BEACON_BLOCK_CONTAINER:
      return &GLOAS_SIGNED_BEACON_BLOCK_CONTAINER;
    case ETH_SSZ_EXECUTION_PAYLOAD_CONTAINER:
      // In Gloas the ExecutionPayload no longer lives inside the beacon body;
      // it is delivered through the separate `ExecutionPayloadEnvelope`. Callers
      // that ask for the execution payload container therefore get the payload
      // field of the envelope (index 0 of the base container).
      return &GLOAS_EXECUTION_PAYLOAD_ENVELOPE_BASE[0];
#else
    (void) chain_id;
#endif
    case ETH_SSZ_BEACON_BLOCK_HEADER:
    default:
      // Fall back to Electra for header-only and non-block types.
      return eth_ssz_type_for_electra(type, chain_id);
  }
}
