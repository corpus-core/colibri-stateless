#include "../util/bytes.h"
#include "../util/crypto.h"
#include "../util/ssz.h"
#include <stdio.h>
#include <stdlib.h>

// the header of a beacon block
const ssz_def_t BEACON_BLOCK_HEADER[5] = {
    SSZ_UINT64("slot"),          // the slot of the block or blocknumber
    SSZ_UINT64("proposerIndex"), // the index of the validator proposing the block
    SSZ_BYTES32("parentRoot"),   // the hash_tree_root of the parent block header
    SSZ_BYTES32("stateRoot"),    // the hash_tree_root of the state at the end of the block
    SSZ_BYTES32("bodyRoot")};    // the hash_tree_root of the block body

// the public keys sync committee used within a period ( about 27h)
const ssz_def_t SYNC_COMMITTEE[] = {
    SSZ_VECTOR("pubkeys", ssz_bls_pubky, 512), // the 512 pubkeys (each 48 bytes) of the validators in the sync committee
    SSZ_BYTE_VECTOR("aggregatePubkey", 48)};   // the aggregate pubkey (48 bytes) of the sync committee

// the block header of the execution layer proved within the beacon block
const ssz_def_t EXECUTION_PAYLOAD_HEADER[] = {
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

// the aggregates signature of the sync committee
const ssz_def_t SYNC_AGGREGATE[] = {
    SSZ_BIT_VECTOR("syncCommitteeBits", 512),       // the bits of the validators that signed the block (each bit represents a validator)
    SSZ_BYTE_VECTOR("syncCommitteeSignature", 96)}; // the signature of the sync committee

// the header of the light client update
const ssz_def_t LIGHT_CLIENT_HEADER[] = {
    SSZ_CONTAINER("beacon", BEACON_BLOCK_HEADER),         // the header of the beacon block
    SSZ_CONTAINER("execution", EXECUTION_PAYLOAD_HEADER), // the header of the execution layer proved within the beacon block
    SSZ_VECTOR("executionBranch", ssz_bytes32, 4)};       // the merkle proof of the execution layer proved within the beacon block

// the light client update is used to verify the transition between two periods of the SyncCommittee.
// This data will be fetched directly through the beacon Chain API since it contains all required data.
const ssz_def_t LIGHT_CLIENT_UPDATE[7] = {
    SSZ_CONTAINER("attestedHeader", LIGHT_CLIENT_HEADER), // the header of the beacon block attested by the sync committee
    SSZ_CONTAINER("nextSyncCommittee", SYNC_COMMITTEE),
    SSZ_VECTOR("nextSyncCommitteeBranch", ssz_bytes32, 5), // will be 6 in electra
    SSZ_CONTAINER("finalizedHeader", LIGHT_CLIENT_HEADER), // the header of the finalized beacon block
    SSZ_VECTOR("finalityBranch", ssz_bytes32, 6),          // will be 7 in electra
    SSZ_CONTAINER("syncAggregate", SYNC_AGGREGATE),        // the aggregates signature of the sync committee
    SSZ_UINT64("signatureSlot")};                          // the slot of the signature
