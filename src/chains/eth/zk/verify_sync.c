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

#define PROOF_OFFSET 49358

// main verify-method for the sync proof
// it returns the period if successful or 0 for failure
uint64_t verify_sync_proof(bytes_t sync_proof) {
  bytes_t  old_keys       = {.data = sync_proof.data + 18, .len = 512 * 48};
  bytes_t  new_keys       = {.data = sync_proof.data + old_keys.len + 18, .len = 512 * 48};
  bytes_t  signature_bits = {.data = new_keys.data + new_keys.len, .len = 64};
  bytes_t  signature      = {.data = signature_bits.data + 64, .len = 96};
  uint64_t gidx           = get_uint64_le(signature.data + 96);
  bytes_t  slot_bytes     = {.data = signature.data + 104, .len = 8};
  bytes_t  proposer_bytes = {.data = slot_bytes.data + 8, .len = 8};
  bytes_t  proof          = {.data = sync_proof.data + PROOF_OFFSET, .len = sync_proof.len - PROOF_OFFSET - 1};

  // the hash at the merkleproof index 7 is the hash of slot and propoer_index, so we use to verify them
  if (!verify_slot(slot_bytes.data, proposer_bytes.data, proof.data + proof.len - 96)) return 0;

  bytes32_t root;
  create_root_hash(new_keys, root);                                            // the root-hash of the next public keys
  verify_merkle_proof(proof, gidx, root);                                      // run the merkle proof all the way down to the signing root
  return blst_verify(root, signature.data, old_keys.data, signature_bits.data) // check the signature
             ? get_uint64_le(slot_bytes.data) >> 13                            // return the verified perios
             : 0;                                                              // or 0 for failure
}

int main(int argc, char** argv) {
  bytes_t sync_proof = {.data = NULL, .len = 0};
  uint8_t buffer[1024];
  size_t  read = 0;
  FILE*   file = fopen(argv[1], "rb");
  if (!file) {
    printf("Failed to open file: %s\n", argv[1]);
    return 1;
  }
  // read all from stdin
  while ((read = fread(buffer, 1, 1024, file)) > 0) {
    sync_proof.data = realloc(sync_proof.data, sync_proof.len + read);
    memcpy(sync_proof.data + sync_proof.len, buffer, read);
    sync_proof.len += read;
  }
  fclose(file);
  uint64_t period = verify_sync_proof(sync_proof);
  if (period)
    printf("Proof is valid for period %d\n", (uint32_t) period);
  else
    printf("Proof is invalid\n");
}
