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

#include "op_types.h"
#include "ssz.h"

// Helper type definition for byte arrays with large maximum size (1GB)
static const ssz_def_t ssz_bytes_1024 = SSZ_BYTES("Bytes", 1073741824);

// title: C4 OP Request
// description: The SSZ union type definitions defining datastructure of a proof for OP-Stack.

#include "op_proof_types.h"

// : OP-Stack

// :: Main Proof Request
//
// The proofs are always wrapped into a ssz-container with the name `C4Request`.
// This Container holds the a version (4 bytes) and unions for different proof types.
//
//  The 4 `Version` Bytes are encoded as `dom, major, minor, patch`.
//  - 0 : `domain` . describe which chain-type is used. 6 = OP-Stack
//  - 1 : `major` . the major version of the prover.
//  - 2 : `minor` . the minor version of the prover.
//  - 3 : `patch` . the patch version of the prover.
//
// The `data` union can hold different types which represents the final data to be verified.
//
// The `proof` union can hold different types which represents the proof of the data.
//
// The `sync_data` union holds optional data used to update the sync_committee.
// Most of the time this is empty since syncing the pubkey only is used whenever it is needed. But the structure
// allows to include those sync_proofs enabling a fully stateless proof.
// Note: OP-Stack uses the same sync_data structure as Ethereum since it shares the consensus layer.
//
//

// A List of possible types of proofs matching the Data
static const ssz_def_t C4_OP_REQUEST_PROOFS_UNION[] = {
    SSZ_NONE,
    SSZ_CONTAINER("AccountProof", OP_ACCOUNT_PROOF),         // a Proof of an Account like eth_getBalance or eth_getStorageAt
    SSZ_CONTAINER("TransactionProof", OP_TRANSACTION_PROOF), // a Proof of a Transaction like eth_getTransactionByHash
    SSZ_CONTAINER("ReceiptProof", OP_RECEIPT_PROOF),         // a Proof of a TransactionReceipt
    SSZ_LIST("LogsProof", OP_LOGS_BLOCK_CONTAINER, 256),     // a Proof for multiple Receipts and txs
    SSZ_CONTAINER("CallProof", OP_CALL_PROOF),               // a Proof of a Call like eth_call
    SSZ_CONTAINER("BlockProof", OP_BLOCK_PROOF),             // Proof for BlockData
}; // a Proof for multiple accounts

// the main container defining the incoming data processed by the verifier
static const ssz_def_t C4_OP_REQUEST[] = {
    SSZ_BYTE_VECTOR("version", 4),                          // the [domain, major, minor, patch] version of the request, domain=6 = OP-Stack
    SSZ_UNION("data", C4_ETH_REQUEST_DATA_UNION),           // the data to prove (OP-Stack reuses Ethereum's data union for compatibility)
    SSZ_UNION("proof", C4_OP_REQUEST_PROOFS_UNION),         // the proof of the data (OP-Stack specific proof types)
    SSZ_UNION("sync_data", C4_ETH_REQUEST_SYNCDATA_UNION)}; // the sync data containing proofs for the transition between the two periods (OP-Stack reuses Ethereum's sync_data union)

// The main container type definition for C4Request, wrapping all request fields for OP-Stack
static const ssz_def_t C4_REQUEST_CONTAINER = SSZ_CONTAINER("C4Request", C4_OP_REQUEST);

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
#define ARRAY_IDX(a, target)  array_idx(a, sizeof(a) / sizeof(ssz_def_t), target)
// Macro to get a pointer to the definition at the index of the target in an array
#define ARRAY_TYPE(a, target) a + array_idx(a, sizeof(a) / sizeof(ssz_def_t), target)

/**
 * Returns the SSZ type definition for a given OP-Stack verification type enum.
 * Maps op_ssz_type_t enum values to their corresponding SSZ definition pointers.
 * Used to retrieve the correct type definition for parsing and validating SSZ-encoded proof data.
 *
 * @param type The verification type enum value
 * @return Pointer to the corresponding SSZ definition, or NULL for invalid types
 */
const ssz_def_t* op_ssz_verification_type(op_ssz_type_t type) {
  switch (type) {
    case OP_SSZ_VERIFY_REQUEST:
      return &C4_REQUEST_CONTAINER;
    case OP_SSZ_VERIFY_ACCOUNT_PROOF:
      return ARRAY_TYPE(C4_OP_REQUEST_PROOFS_UNION, OP_ACCOUNT_PROOF);
    case OP_SSZ_VERIFY_TRANSACTION_PROOF:
      return ARRAY_TYPE(C4_OP_REQUEST_PROOFS_UNION, OP_TRANSACTION_PROOF);
    case OP_SSZ_VERIFY_RECEIPT_PROOF:
      return ARRAY_TYPE(C4_OP_REQUEST_PROOFS_UNION, OP_RECEIPT_PROOF);
    case OP_SSZ_VERIFY_LOGS_PROOF:
      return ARRAY_TYPE(C4_OP_REQUEST_PROOFS_UNION, &OP_LOGS_BLOCK_CONTAINER);
    case OP_SSZ_VERIFY_CALL_PROOF:
      return ARRAY_TYPE(C4_OP_REQUEST_PROOFS_UNION, OP_CALL_PROOF);
    case OP_SSZ_VERIFY_BLOCK_PROOF:
      return ARRAY_TYPE(C4_OP_REQUEST_PROOFS_UNION, OP_BLOCK_PROOF);
    case OP_SSZ_VERIFY_PRECONF_PROOF:
      // OP_BLOCKPROOF_UNION is defined in op_proof_types.h and contains preconfirmation proofs
      return ARRAY_TYPE(OP_BLOCKPROOF_UNION, OP_PRECONF);
    case OP_SSZ_VERIFY_L1_ANCHORED_PROOF:
      // L1-anchored proof via L2OutputOracle on Ethereum L1
      return ARRAY_TYPE(OP_BLOCKPROOF_UNION, OP_L1_ANCHORED_PROOF);
    default: return NULL;
  }
}
