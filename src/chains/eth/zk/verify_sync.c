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

#include "zk_util.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROOF_OFFSET 49358

typedef struct {
  // public data
  bytes32_t current_keys_root;
  bytes32_t next_keys_root;
  uint64_t  next_period;

  // private data
  bytes_t  current_keys;   // 512 * 48 bytes keys of the current sync_committee
  bytes_t  signature_bits; // 64 bytes bits of the validators that signed the block
  bytes_t  signature;      // 96 bytes signature of the sync committee
  bytes_t  slot_bytes;     // 8 bytes slot of the block
  bytes_t  proposer_bytes;
  bytes_t  proof; //  merkle proof from the new keys with the signing_root as root.
  uint32_t gidx;  // gindex for the merkle proof

} proof_data_t;

// proofs or verifies the net sync_committee keys root hash.
static proof_data_t* read_proof_data(char* sync_data_filename) {
  uint8_t  buffer[1024];
  size_t   read           = 0;
  FILE*    file           = fopen(sync_data_filename, "rb");
  uint8_t* allocated_data = NULL;
  bytes_t  sync_proof     = {0};
  if (!file) {
    fprintf(stderr, "Failed to open file: %s\n", sync_data_filename);
    return NULL;
  }
  // read all
  while ((read = fread(buffer, 1, 1024, file)) > 0) {
    allocated_data  = realloc(allocated_data, sync_proof.len + read + sizeof(proof_data_t));
    sync_proof.data = allocated_data + sizeof(proof_data_t); // we use the first bytes for proof_data_t
    memcpy(sync_proof.data + sync_proof.len, buffer, read);
    sync_proof.len += read;
  }
  fclose(file);
  if (!sync_proof.data) {
    fprintf(stderr, "Failed to read file: %s\n", sync_data_filename);
    return NULL;
  }

  // fill the data
  proof_data_t* proof_data   = (proof_data_t*) allocated_data;
  proof_data->current_keys   = bytes(sync_proof.data + 18, 512 * 48);
  bytes_t new_keys           = bytes(proof_data->current_keys.data + proof_data->current_keys.len, 512 * 48);
  proof_data->signature_bits = bytes(new_keys.data + new_keys.len, 64);
  proof_data->signature      = bytes(proof_data->signature_bits.data + 64, 96);
  proof_data->gidx           = (uint32_t) get_uint64_le(proof_data->signature.data + 96);
  proof_data->slot_bytes     = bytes(proof_data->signature.data + 104, 8);
  proof_data->proposer_bytes = bytes(proof_data->slot_bytes.data + 8, 8);
  proof_data->proof          = bytes(sync_proof.data + PROOF_OFFSET, sync_proof.len - PROOF_OFFSET - 1);
  proof_data->next_period    = (get_uint64_le(proof_data->slot_bytes.data) >> 13) + 1;
  create_root_hash(proof_data->current_keys, proof_data->current_keys_root);
  create_root_hash(new_keys, proof_data->next_keys_root);
  return proof_data;
}

// main verify-method for the sync proof
// it returns the period if successful or 0 for failure
bool verify_proof_data(proof_data_t* proof_data) {
  bytes32_t root;

  // verifiy the calculate the period
  uint64_t period = get_uint64_le(proof_data->slot_bytes.data) >> 13;
  if (period != proof_data->next_period - 1) // check the period is the next period
    return false;

  // verify the slot matches the hash in the merkle proof coming from the header
  // the hash at the merkleproof index 7 is the hash of slot and propoer_index, so we use to verify them
  if (!verify_slot(proof_data->slot_bytes.data, proof_data->proposer_bytes.data, proof_data->proof.data + proof_data->proof.len - 96))
    return false;

  // verify the current keys root hash
  create_root_hash(proof_data->current_keys, root); // calculate hash_tree_root for the old keys
  if (memcmp(root, proof_data->current_keys_root, 32) != 0)
    return false;

  // verify the new key root hash
  memcpy(root, proof_data->next_keys_root, 32);                   // initial leaf value
  verify_merkle_proof(proof_data->proof, proof_data->gidx, root); // run the merkle proof all the way down to the signing root

  // verify the signature
  return blst_verify(root, proof_data->signature.data, proof_data->current_keys.data, proof_data->signature_bits.data);
}

int main(int argc, char** argv) {
  // read the proof data
  proof_data_t* proof_data = read_proof_data(argv[1]);

  bool valid = verify_proof_data(proof_data);

  if (valid)
    fprintf(stdout, "Proof is valid for period %d\n", (uint32_t) proof_data->next_period);
  else
    fprintf(stdout, "Proof is invalid\n");

  free(proof_data);
}
