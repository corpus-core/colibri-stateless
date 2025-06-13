
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
  ssz_add_bytes(&c4_req, "version", bytes(c4_version_bytes, 4));
  set_data(&c4_req, "data", data);
  set_data(&c4_req, "proof", proof);
  set_data(&c4_req, "sync_data", sync_data);

  // set chain_engine
  *c4_req.fixed.data.data = (uint8_t) c4_chain_type(chain_id);
  return ssz_builder_to_bytes(&c4_req).bytes;
}

#ifdef PROOFER_CACHE
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

ssz_builder_t eth_ssz_create_state_proof(proofer_ctx_t* ctx, json_t block_number, beacon_block_t* block) {
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
  ssz_add_bytes(&state_proof, "historic_proof", bytes(&empty_selector, 1)); // TODO add real histtoric proof, for now we just make sure format is correct
  ssz_add_bytes(&state_proof, "sync_committee_bits", ssz_get(&block->sync_aggregate, "syncCommitteeBits").bytes);
  ssz_add_bytes(&state_proof, "sync_committee_signature", ssz_get(&block->sync_aggregate, "syncCommitteeSignature").bytes);

  safe_free(proof.data);
  return state_proof;
}
