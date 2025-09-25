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
static const ssz_def_t ssz_bytes_1024 = SSZ_BYTES("Bytes", 1073741824);

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
//  - 1 : `major` . the major version of the proofer.
//  - 2 : `minor` . the minor version of the proofer.
//  - 3 : `patch` . the patch version of the proofer.
//
// the `data` union can hold different types which represents the final data to be verified.
//
// the `proof` union can hold different types which represents the proof of the data.
//
// the `sync_data` union hold optional data used to update the sync_committee.
// Most of the time this is empty since syncing the pubkey only is used whenever it is needed. But the structure
// allows to include those sync_proofs enabling a fully stateless proof.
//
//

// A List of possible types of data matching the Proofs
const ssz_def_t C4_ETH_REQUEST_DATA_UNION[10] = {
    SSZ_NONE,
    SSZ_BYTES32("hash"),                                       // the blochash  which is used for blockhash proof
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
const ssz_def_t C4_ETH_REQUEST_SYNCDATA_UNION[] = {
    SSZ_NONE,
    SSZ_LIST("DenepLightClientUpdate", DENEP_LIGHT_CLIENT_UPDATE_CONTAINER, 512),      // this light client update can be fetched directly from the beacon chain API
    SSZ_LIST("ElectraLightClientUpdate", ELECTRA_LIGHT_CLIENT_UPDATE_CONTAINER, 512)}; // this light client update can be fetched directly from the beacon chain API

// the main container defining the incoming data processed by the verifier
static const ssz_def_t C4_REQUEST[] = {
    SSZ_BYTE_VECTOR("version", 4),                          // the [domain, major, minor, patch] version of the request, domaon=1 = eth
    SSZ_UNION("data", C4_ETH_REQUEST_DATA_UNION),           // the data to proof
    SSZ_UNION("proof", C4_REQUEST_PROOFS_UNION),            // the proof of the data
    SSZ_UNION("sync_data", C4_ETH_REQUEST_SYNCDATA_UNION)}; // the sync data containing proofs for the transition between the two periods

static const ssz_def_t C4_REQUEST_CONTAINER = SSZ_CONTAINER("C4Request", C4_REQUEST);

static inline size_t array_idx(const ssz_def_t* array, size_t len, const ssz_def_t* target) {
  for (size_t i = 0; i < len; i++) {
    if (array[i].type >= SSZ_TYPE_CONTAINER && array[i].def.container.elements == target) return i;
  }
  return 0;
}
#define ARRAY_IDX(a, target)  array_idx(a, sizeof(a) / sizeof(ssz_def_t), target)
#define ARRAY_TYPE(a, target) a + array_idx(a, sizeof(a) / sizeof(ssz_def_t), target)

const ssz_def_t* eth_get_light_client_update_list(fork_id_t fork) {
  switch (fork) {
    case C4_FORK_DENEB:
      return ARRAY_TYPE(C4_ETH_REQUEST_SYNCDATA_UNION, &DENEP_LIGHT_CLIENT_UPDATE_CONTAINER);
    case C4_FORK_ELECTRA:
      return ARRAY_TYPE(C4_ETH_REQUEST_SYNCDATA_UNION, &ELECTRA_LIGHT_CLIENT_UPDATE_CONTAINER);
    default:
      return NULL;
  }
}
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
