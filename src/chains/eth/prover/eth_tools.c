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
#include "eth_tx.h"
#include "version.h"

static void set_data(ssz_builder_t* req, const char* name, ssz_builder_t data) {
  if (data.fixed.data.data || data.dynamic.data.data)
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
#endif

static void ssz_add_block_proof(ssz_builder_t* builder, beacon_block_t* block_data, gindex_t block_index) {
  uint8_t  buffer[33] = {0};
  uint32_t l          = 1;
  if (block_index == GINDEX_BLOCHASH) {
    l         = 33;
    buffer[0] = 1;
    memcpy(buffer + 1, ssz_get(&block_data->execution, "blockHash").bytes.data, 32);
  }
  else if (block_index == GINDEX_BLOCKUMBER) {
    l = 9;
    memcpy(buffer + 1, ssz_get(&block_data->execution, "blockNumber").bytes.data, 8);
    buffer[0] = 2;
  }
  ssz_add_bytes(builder, "block", bytes(buffer, l));
}

ssz_builder_t eth_ssz_create_state_proof(prover_ctx_t* ctx, json_t block_number, beacon_block_t* block, blockroot_proof_t* historic_proof) {
  uint8_t       empty_selector = 0;
  bytes32_t     body_root      = {0};
  ssz_builder_t state_proof    = ssz_builder_for_type(ETH_SSZ_VERIFY_STATE_PROOF);
  gindex_t      block_index    = eth_get_gindex_for_block(c4_chain_fork_id(ctx->chain_id, block->slot >> 5), block_number);
  gindex_t      state_index    = ssz_gindex(block->body.def, 2, "executionPayload", "stateRoot");
  bytes_t       proof          = block_index == 0                                            // if we fetch latest,
                                     ? ssz_create_proof(block->body, body_root, state_index) // we only proof the state root
                                     : ssz_create_multi_proof(block->body, body_root, 2,     // but if a blocknumber or hash is given,
                                                              block_index, state_index);     // we also need to add this to the proof.

  // build the state proof
  ssz_add_block_proof(&state_proof, block, block_index);
  ssz_add_bytes(&state_proof, "proof", proof);
  ssz_add_builders(&state_proof, "header", c4_proof_add_header(block->header, body_root));
  ssz_add_header_proof(&state_proof, block, *historic_proof);

  safe_free(proof.data);
  return state_proof;
}
