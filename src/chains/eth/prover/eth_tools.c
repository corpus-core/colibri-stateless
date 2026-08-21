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

#include "eth_tools.h"
#include "beacon.h"
#include "beacon_types.h"
#include "bytes.h"
#include "eth_account.h"
#include "eth_compute_units.h"
#include "eth_tx.h"
#include "prover.h"
#include "version.h"

static void set_data(ssz_builder_t* req, const char* name, ssz_builder_t data) {
  if (data.def)
    ssz_add_builders(req, name, data);
  else
    ssz_add_bytes(req, name, bytes(NULL, 1));
}

bytes_t eth_create_proof_request(chain_id_t chain_id, ssz_builder_t data, ssz_builder_t proof, ssz_builder_t sync_data) {
  ssz_builder_t c4_req = ssz_builder_for_type(ETH_SSZ_VERIFY_REQUEST);

  // build the request
  ssz_add_bytes(&c4_req, "version", bytes(c4_protocol_version_bytes, 4));
  set_data(&c4_req, "data", data);
  set_data(&c4_req, "proof", proof);
  set_data(&c4_req, "sync_data", sync_data);

  // set chain_engine
  *c4_req.fixed.data.data = (uint8_t) c4_chain_type(chain_id);
  return ssz_builder_to_bytes(&c4_req).bytes;
}

#ifdef PROVER_CACHE
uint8_t* c4_eth_receipt_cachekey(bytes32_t target, bytes32_t blockhash) {
  if (target != blockhash) memcpy(target, blockhash, 32);
  target[0] = 'R';
  target[1] = 'T';
  return target;
}
uint8_t* c4_eth_tx_cachekey(bytes32_t target, bytes32_t blockhash) {
  if (target != blockhash) memcpy(target, blockhash, 32);
  target[0] = 'T';
  target[1] = 'T';
  return target;
}
#endif

void eth_add_block_proof(prover_ctx_t* ctx, ssz_builder_t* builder, beacon_block_t* block_data, blockroot_proof_t* historic_block_proof) {
  // the blockHash-only variant is safe whenever the verifier already holds the verified
  // header: either the client advertised it as its last verified block (remote mode) or
  // prover and verifier run in the same process and share the header cache (hybrid mode).
  bool verifier_has_header = !bytes_all_zero(bytes(block_data->el_block_hash, 32)) &&
                             memcmp(ctx->last_block_hash, block_data->el_block_hash, 32) == 0;
#ifdef EL_HEADER_CACHE
  if (!verifier_has_header && (ctx->flags & C4_PROVER_FLAG_HYBRID) && !bytes_all_zero(bytes(block_data->el_block_hash, 32)))
    // the lookup also LRU-touches the entry, protecting it from eviction until the
    // proof is verified locally right after.
    verifier_has_header = c4_header_cache_has_el_header(ctx->chain_id, block_data->el_block_hash);
#endif

  if (verifier_has_header) {
    // union variant 0 (blockHash): selector byte + 32-byte hash
    uint8_t block_hash_union[33] = {0};
    memcpy(block_hash_union + 1, block_data->el_block_hash, 32);
    ssz_add_bytes(builder, "block", bytes(block_hash_union, sizeof(block_hash_union)));
  }
  else {
    ssz_builder_t block_proof = ssz_builder_for_type(ETH_SSZ_CL_BLOCK_PROOF);
    ssz_add_bytes(&block_proof, "elHeader", block_data->el_header);
    ssz_add_builders(&block_proof, "clHeader", c4_proof_add_header(block_data->header, block_data->body_root));
    ssz_add_bytes(&block_proof, "blockhashBranch", block_data->block_hash_branch);
    ssz_add_uint64(&block_proof, block_data->block_hash_branch_gindex);
    ssz_add_header_proof(&block_proof, block_data, *historic_block_proof);
    ssz_add_builders(builder, "block", block_proof);
  }
}
