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

// title: C4 ETH Request
// description: The SSZ union type defintions defining datastructure of a proof for eth.

#include "beacon_types.h"
#include "ssz.h"
#include "witness.h"
#include <stdio.h>
#include <stdlib.h>
// Helper type definition for byte arrays with large maximum size (1GB)
static const ssz_def_t ssz_bytes_1024 = SSZ_BYTES("Bytes", 1073741824);
// Forward declaration for C4_ETH_LC_SYNCDATA (defined later after includes)
static const ssz_def_t C4_ETH_LC_SYNCDATA[2];
static const ssz_def_t C4_ETH_ZK_SYNCDATA[6];
#include "verify_data_types.h"
#include "verify_proof_types.h"

// : Ethereum

// :: Ethereum Main Proof Request
//
// The proofs are always wrapped into a ssz-container with the name `C4Request`.
// This Container holds the a version (4 bytes) and unions for different proof types.
//
//  The 4 `Version` Bytes are encoded as `dom, major, minor, patch`.
//  - 0 : `domain` . describe which chain-type is used. 1 =  ethereum
//  - 1 : `major` . the major version of the prover.
//  - 2 : `minor` . the minor version of the prover.
//  - 3 : `patch` . the patch version of the prover.
//
// The `data` union can hold different types which represents the final data to be verified.
//
// The `proof` union can hold different types which represents the proof of the data.
//
// The `sync_data` union holds optional data used to update the sync_committee.
// Most of the time, this is empty since syncing the pubkey only is used whenever it is needed. But the structure
// allows to include those sync_proofs enabling a fully stateless proof.
//
//

// A List of possible types of data matching the Proofs
const ssz_def_t C4_ETH_REQUEST_DATA_UNION[10] = {
    SSZ_NONE,
    SSZ_BYTES32("hash"),                                       // the blockhash  which is used for blockhash proof
    SSZ_BYTES("bytes", 1073741824),                            // the bytes of the data
    SSZ_UINT256("value"),                                      // the balance of an account
    SSZ_CONTAINER("EthTransactionData", ETH_TX_DATA),          // the transaction data
    SSZ_CONTAINER("EthReceiptData", ETH_RECEIPT_DATA),         // the transaction receipt
    SSZ_LIST("EthLogs", ETH_RECEIPT_DATA_LOG_CONTAINER, 1024), // result of eth_getLogs
    SSZ_CONTAINER("EthBlockData", ETH_BLOCK_DATA),             // the block data
    SSZ_CONTAINER("EthProofData", ETH_PROOF_DATA),             // the result of an eth_getProof
    SSZ_CONTAINER("SimulationResult", ETH_SIMULATION_RESULT),  // the result of an colibri_simulateTransaction
};

// A List of possible types of proofs matching the Data
static const ssz_def_t C4_REQUEST_PROOFS_UNION[] = {
    SSZ_NONE,
    SSZ_CONTAINER("AccountProof", ETH_ACCOUNT_PROOF),          // a Proof of an Account like eth_getBalance or eth_getStorageAt
    SSZ_CONTAINER("TransactionProof", ETH_TRANSACTION_PROOF),  // a Proof of a Transaction like eth_getTransactionByHash
    SSZ_CONTAINER("ReceiptProof", ETH_RECEIPT_PROOF),          // a Proof of a TransactionReceipt
    SSZ_LIST("LogsProof", ETH_LOGS_BLOCK_CONTAINER, 256),      // a Proof for multiple Receipts and txs
    SSZ_CONTAINER("CallProof", ETH_CALL_PROOF),                // a Proof of a Call like eth_call
    SSZ_CONTAINER("SyncProof", ETH_SYNC_PROOF),                // Proof as input data for the sync committee transition used by zk
    SSZ_CONTAINER("BlockProof", ETH_BLOCK_PROOF),              // Proof for BlockData
    SSZ_CONTAINER("BlockNumberProof", ETH_BLOCK_NUMBER_PROOF), // Proof for BlockNumber
    SSZ_CONTAINER("WitnessProof", C4_WITNESS_SIGNED)           // Proof for Witness
}; // a Proof for multiple accounts

// A List of possible types of sync data used to update the sync state by verifying the transition from the last period to the required.
static const ssz_def_t C4_ETH_SYNCDATA_BOOTSTRAP_UNION[] = {
    SSZ_NONE,
    SSZ_CONTAINER("DenepLightClientBootstrap", DENEP_LIGHT_CLIENT_BOOTSTRAP),    // Denep Fork Structureed LightClient Bootstrap
    SSZ_CONTAINER("ElectraLightClientBootstrap", ELECTRA_LIGHT_CLIENT_BOOTSTRAP) // Electra Fork Structureed LightClient Bootstrap
};

// A List of LightClient Updates as returned from light_client/updates endpoint.
static const ssz_def_t C4_ETH_SYNCDATA_UPDATE_UNION[] = {
    SSZ_CONTAINER("DenepLightClientUpdate", DENEP_LIGHT_CLIENT_UPDATE),    // Denep Fork Structureed LightClient Update
    SSZ_CONTAINER("ElectraLightClientUpdate", ELECTRA_LIGHT_CLIENT_UPDATE) // Electra Fork Structureed LightClient Update
};

// A Union of possible types of sync data used to update the sync state by verifying the transition from the last period to the required.
const ssz_def_t C4_ETH_REQUEST_SYNCDATA_UNION[] = {
    SSZ_NONE,
    SSZ_CONTAINER("LCSyncData", C4_ETH_LC_SYNCDATA), // Light Client Sync Data
    SSZ_CONTAINER("ZKSyncData", C4_ETH_ZK_SYNCDATA), // ZK Proof Sync Data
};

// the main container defining the incoming data processed by the verifier
static const ssz_def_t C4_REQUEST[] = {
    SSZ_BYTE_VECTOR("version", 4),                          // the [domain, major, minor, patch] version of the request, domain=1 = eth
    SSZ_UNION("data", C4_ETH_REQUEST_DATA_UNION),           // the data to proof
    SSZ_UNION("proof", C4_REQUEST_PROOFS_UNION),            // the proof of the data
    SSZ_UNION("sync_data", C4_ETH_REQUEST_SYNCDATA_UNION)}; // the sync data containing proofs for the transition between the two periods

// The main container type definition for C4Request, wrapping all request fields
static const ssz_def_t C4_REQUEST_CONTAINER = SSZ_CONTAINER("C4Request", C4_REQUEST);

// Union type for a single LightClient Update, which can be either Deneb or Electra format
static const ssz_def_t C4_ETH_SYNCDATA_UPDATE = SSZ_UNION("updates", C4_ETH_SYNCDATA_UPDATE_UNION);

// :: SyncCommittee Proof
//
// The Verifier always needs the pubkeys of the sync committee for a given period in order to verify the BLS signature of a Beacon BlockHeader.
//
// If a verifier requests a proof from a remote prover, the verifier may use the c4-property of the RPC-Request to describe it's state of the knpown periods or checkpoint.
// If the verifier only reports a checkpoint, a bootstrap is added proving the current_sync_committee for the given checkpoint.
// If the header requested has a higher period that the bootstrap or the latest period, all required lightClientUpdates will be proved.
//

// LC SyncData contains all the proofs needed to bootstrap and update to the  current period.
static const ssz_def_t C4_ETH_LC_SYNCDATA[2] = {
    SSZ_UNION("bootstrap", C4_ETH_SYNCDATA_BOOTSTRAP_UNION), // optional bootstrap data for the sync committee, which is only accepted by the verifier, if it matches the checkpoint set.
    SSZ_LIST("update", C4_ETH_SYNCDATA_UPDATE, 1024)         // optional update data for the sync committee
};

// ZK SyncData contains the recursive zk proof of the sync committee update
static const ssz_def_t C4_ETH_ZK_SYNCDATA[6] = {
    SSZ_BYTES32("vk_hash"),        // the hash of the vk used to generate the proof
    SSZ_BYTE_VECTOR("proof", 260), // the recursive zk proof of the sync committee update as groth16 proof
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),
    SSZ_VECTOR("pubkeys", ssz_bls_pubky, 512),          // the pubkeys of the sync committee
    SSZ_UNION("checkpoint", ETH_HEADER_PROOFS_UNION),   // the proof from the checkpoint to the header
    SSZ_LIST("signatures", ssz_secp256k1_signature, 16) // the signatures for the checkpoint
};

/**
 * Finds the index of a target definition within an array of SSZ definitions.
 * Searches for a container type whose elements pointer matches the target.
 *
 * @param array Array of SSZ definitions to search
 * @param len Length of the array
 * @param target Target definition to find (compares elements pointer)
 * @return Index of the matching definition, or 0 if not found
 */
static inline size_t array_idx(const ssz_def_t* array, size_t len, const ssz_def_t* target) {
  for (size_t i = 0; i < len; i++) {
    if (array[i].type >= SSZ_TYPE_CONTAINER && array[i].def.container.elements == target) return i;
  }
  return 0;
}
// Macro to get the index of a target definition in an array
#define ARRAY_IDX(a, target) array_idx(a, sizeof(a) / sizeof(ssz_def_t), target)
// Macro to get a pointer to the definition at the index of the target in an array
#define ARRAY_TYPE(a, target) a + array_idx(a, sizeof(a) / sizeof(ssz_def_t), target)

/**
 * Returns the SSZ definition for a LightClient Update based on the fork ID.
 * Maps fork identifiers to the corresponding update type in the union array.
 *
 * @param fork Fork identifier (C4_FORK_DENEB, C4_FORK_ELECTRA, C4_FORK_FULU)
 * @return Pointer to the SSZ definition for the update type, or NULL for unsupported forks
 */
const ssz_def_t* eth_get_light_client_update(fork_id_t fork) {
  switch (fork) {
    case C4_FORK_DENEB:
      return C4_ETH_SYNCDATA_UPDATE_UNION;
    case C4_FORK_ELECTRA:
    case C4_FORK_FULU:
      return C4_ETH_SYNCDATA_UPDATE_UNION + 1;
    default:
      return NULL;
  }
}

/**
 * Returns the SSZ type definition for a given verification type enum.
 * Maps eth_ssz_type_t enum values to their corresponding SSZ definition pointers.
 * Used to retrieve the correct type definition for parsing and validating SSZ-encoded proof data.
 *
 * @param type The verification type enum value
 * @return Pointer to the corresponding SSZ definition, or NULL for invalid types
 */
const ssz_def_t* eth_ssz_verification_type(eth_ssz_type_t type) {
  switch (type) {
    case ETH_SSZ_VERIFY_REQUEST:
      return &C4_REQUEST_CONTAINER;
    case ETH_SSZ_VERIFY_ACCOUNT_PROOF:
      return ARRAY_TYPE(C4_REQUEST_PROOFS_UNION, ETH_ACCOUNT_PROOF);
    case ETH_SSZ_VERIFY_TRANSACTION_PROOF:
      return ARRAY_TYPE(C4_REQUEST_PROOFS_UNION, ETH_TRANSACTION_PROOF);
    case ETH_SSZ_VERIFY_RECEIPT_PROOF:
      return ARRAY_TYPE(C4_REQUEST_PROOFS_UNION, ETH_RECEIPT_PROOF);
    case ETH_SSZ_VERIFY_LOGS_PROOF:
      return ARRAY_TYPE(C4_REQUEST_PROOFS_UNION, &ETH_LOGS_BLOCK_CONTAINER);
    case ETH_SSZ_VERIFY_CALL_PROOF:
      return ARRAY_TYPE(C4_REQUEST_PROOFS_UNION, ETH_CALL_PROOF);
    case ETH_SSZ_VERIFY_SYNC_PROOF:
      return ARRAY_TYPE(C4_REQUEST_PROOFS_UNION, ETH_SYNC_PROOF);
    case ETH_SSZ_VERIFY_BLOCK_PROOF:
      return ARRAY_TYPE(C4_REQUEST_PROOFS_UNION, ETH_BLOCK_PROOF);
    case ETH_SSZ_VERIFY_BLOCK_NUMBER_PROOF:
      return ARRAY_TYPE(C4_REQUEST_PROOFS_UNION, ETH_BLOCK_NUMBER_PROOF);
    case ETH_SSZ_VERIFY_WITNESS_PROOF:
      return ARRAY_TYPE(C4_REQUEST_PROOFS_UNION, C4_WITNESS_SIGNED);
    case ETH_SSZ_VERIFY_STATE_PROOF:
      return &ETH_STATE_PROOF_CONTAINER;
    case ETH_SSZ_DATA_NONE:
      return C4_ETH_REQUEST_DATA_UNION;
    case ETH_SSZ_DATA_HASH32:
      return C4_ETH_REQUEST_DATA_UNION + 1;
    case ETH_SSZ_DATA_BYTES:
      return C4_ETH_REQUEST_DATA_UNION + 2;
    case ETH_SSZ_DATA_UINT256:
      return C4_ETH_REQUEST_DATA_UNION + 3;
    case ETH_SSZ_DATA_TX:
      return C4_ETH_REQUEST_DATA_UNION + 4;
    case ETH_SSZ_DATA_RECEIPT:
      return C4_ETH_REQUEST_DATA_UNION + 5;
    case ETH_SSZ_DATA_LOGS:
      return C4_ETH_REQUEST_DATA_UNION + 6;
    case ETH_SSZ_DATA_BLOCK:
      return C4_ETH_REQUEST_DATA_UNION + 7;
    case ETH_SSZ_DATA_PROOF:
      return C4_ETH_REQUEST_DATA_UNION + 8;
    case ETH_SSZ_DATA_SIMULATION:
      return C4_ETH_REQUEST_DATA_UNION + 9; // Use proper SimulationResult structure
    default: return NULL;
  }
}
