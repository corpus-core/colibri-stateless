#include "beacon.h"
#include "beacon_types.h"
#include "eth_req.h"
#include "eth_tools.h"
#include "historic_proof.h"
#include "json.h"
#include "proofer.h"
#include "ssz.h"
#include "version.h"
#include <inttypes.h> // Include this header for PRIu64 and PRIx64
#include <stdlib.h>
#include <string.h>

c4_status_t c4_proof_block(proofer_ctx_t* ctx) {
  uint8_t           empty_selector = 0;
  beacon_block_t    block          = {0};
  bytes32_t         body_root      = {0};
  ssz_builder_t     block_proof    = ssz_builder_for_type(ETH_SSZ_VERIFY_BLOCK_PROOF);
  blockroot_proof_t historic_proof = {0};

  // fetch the block
  TRY_ASYNC(c4_beacon_get_block_for_eth(ctx, json_at(ctx->params, 0), &block));
  TRY_ASYNC(c4_check_historic_proof(ctx, &historic_proof, &block));

  // create merkle proof
  bytes_t execution_payload_proof = ssz_create_proof(block.body, body_root, ssz_gindex(block.body.def, 1, "executionPayload"));

  // build the proof
  ssz_add_builders(&block_proof, "executionPayload", (ssz_builder_t) {.def = block.execution.def, .fixed = {.data = bytes_dup(block.execution.bytes)}});
  ssz_add_bytes(&block_proof, "proof", execution_payload_proof);
  safe_free(execution_payload_proof.data);
  ssz_add_builders(&block_proof, "header", c4_proof_add_header(block.header, body_root));
  ssz_add_blockroot_proof(&block_proof, &block, historic_proof);

  ctx->proof = eth_create_proof_request(
      ctx->chain_id,
      NULL_SSZ_BUILDER,
      block_proof,
      NULL_SSZ_BUILDER);

  c4_free_block_proof(&historic_proof);

  return C4_SUCCESS;
}

c4_status_t c4_proof_block_number(proofer_ctx_t* ctx) {
  uint8_t           empty_selector = 0;
  beacon_block_t    block          = {0};
  bytes32_t         body_root      = {0};
  ssz_builder_t     block_proof    = ssz_builder_for_type(ETH_SSZ_VERIFY_BLOCK_NUMBER_PROOF);
  blockroot_proof_t historic_proof = {0};

  // fetch the block
  TRY_ASYNC(c4_beacon_get_block_for_eth(ctx, json_parse("\"latest\""), &block));
  TRY_ASYNC(c4_check_historic_proof(ctx, &historic_proof, &block));

  // create merkle proof
  bytes_t execution_payload_proof = ssz_create_multi_proof(block.body, body_root, 2,
                                                           ssz_gindex(block.body.def, 2, "executionPayload", "blockNumber"),
                                                           ssz_gindex(block.body.def, 2, "executionPayload", "timestamp"));

  // build the proof
  ssz_add_bytes(&block_proof, "blockNumber", ssz_get(&block.execution, "blockNumber").bytes);
  ssz_add_bytes(&block_proof, "timestamp", ssz_get(&block.execution, "timestamp").bytes);
  ssz_add_bytes(&block_proof, "proof", execution_payload_proof);
  ssz_add_builders(&block_proof, "header", c4_proof_add_header(block.header, body_root));
  ssz_add_blockroot_proof(&block_proof, &block, historic_proof);
  ssz_add_bytes(&block_proof, "sync_committee_bits", ssz_get(&block.sync_aggregate, "syncCommitteeBits").bytes);
  ssz_add_bytes(&block_proof, "sync_committee_signature", ssz_get(&block.sync_aggregate, "syncCommitteeSignature").bytes);
  safe_free(execution_payload_proof.data);

  ctx->proof = eth_create_proof_request(
      ctx->chain_id,
      NULL_SSZ_BUILDER,
      block_proof,
      NULL_SSZ_BUILDER);

  c4_free_block_proof(&historic_proof);

  return C4_SUCCESS;
}
