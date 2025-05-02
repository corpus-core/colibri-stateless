#include "historic_proof.h"
#include "beacon.h"
#include "beacon_types.h"
#include "eth_req.h"
#include "eth_tools.h"
#include "json.h"
#include "logger.h"
#include "proofer.h"
#include "ssz.h"
#include "sync_committee.h"
#include "version.h"
#include <inttypes.h> // Include this header for PRIu64 and PRIx64
#include <stdlib.h>
#include <string.h>

static const ssz_def_t HISTORICAL_SUMMARY[] = {
    SSZ_BYTES32("block_summary_root"),
    SSZ_BYTES32("state_summary_root")};
static const ssz_def_t HISTORICAL_SUMMARY_CONTAINER = SSZ_CONTAINER("HISTORICAL_SUMMARY", HISTORICAL_SUMMARY);
static const ssz_def_t SUMMARIES                    = SSZ_LIST("summaries", HISTORICAL_SUMMARY_CONTAINER, 1 << 24);
static const ssz_def_t BLOCKS                       = SSZ_VECTOR("blocks", ssz_bytes32, 8192);

static void verify_proof(char* name, bytes32_t leaf, bytes32_t root, bytes_t proof, gindex_t gindex) {
  bytes32_t out = {0};
  ssz_verify_single_merkle_proof(proof, leaf, gindex, out);
  buffer_t debug = {0};
  bprintf(&debug, "%s\n-leaf :0x%b\n", name, bytes(leaf, 32));
  bprintf(&debug, "-gidx :%l\n", gindex);
  bprintf(&debug, "-root :0x%b\n", bytes(root, 32));
  bprintf(&debug, "-res  :0x%b\n", bytes(out, 32));
  printf("%s\n", (char*) debug.data.data);
  safe_free(debug.data.data);
}

c4_status_t c4_check_historic_proof(proofer_ctx_t* ctx, blockroot_proof_t* block_proof, uint64_t slot) {
  beacon_block_t block         = {0};
  c4_status_t    status        = C4_SUCCESS;
  json_t         history_proof = {0};
  uint8_t        tmp[200]      = {0};
  buffer_t       buf           = stack_buffer(tmp);
  buffer_t       buf2          = stack_buffer(tmp);
  uint32_t       block_period  = slot >> 13; // the period of the target block
  bytes_t        blocks        = {0};

  if (!ctx->client_state.len) return C4_SUCCESS;                     // no client state means we can't check for historic proofs and assume we simply use the synccommittee for this block.
  uint32_t state_period = c4_eth_get_last_period(ctx->client_state); // this is the oldest period we have in the client state
  if (!state_period) return C4_SUCCESS;                              // the client does not have a state yet, so he might as well get the head and verify the block.
  if (block_period >= state_period) return C4_SUCCESS;               // the target block is within the current range of the client

  TRY_ASYNC(c4_beacon_get_block_for_eth(ctx, json_parse("\"latest\""), &block)); // we get the latest because we know for latest we get the a proof for the state. Older sztates are not stored
  TRY_ADD_ASYNC(status, c4_send_beacon_json(ctx, bprintf(&buf, "eth/v1/lodestar/historical_summaries/0x%b", ssz_get(&block.header, "stateRoot").bytes), NULL, 120, &history_proof));
  TRY_ADD_ASYNC(status, c4_send_internal_request(ctx, "eth/period_store", bprintf(&buf2, "period=%d&type=1", block_period), 0, &blocks)); // get the blockd
  if (status != C4_SUCCESS) return status;

  json_t    data           = json_get(history_proof, "data"); // the the main json-object
  uint32_t  summary_idx    = block_period - 758 - 0;          // the index starting from the  cappella fork, where we got zhe first Summary entry.
  uint32_t  block_idx      = slot % 8192;                     // idx within the period
  buffer_t  full_proof     = {0};
  buffer_t  list_data      = {0};
  bytes32_t root           = {0};
  gindex_t  summaries_gidx = 32 + 27;                                                      // the gindex of the field for thesummaries in the state
  gindex_t  period_gidx    = ssz_gindex(&SUMMARIES, 2, summary_idx, "block_summary_root"); // the gindex of the single summary-object we need to proof
  gindex_t  block_gidx     = ssz_gindex(&BLOCKS, 1, block_idx);
  bytes32_t body_root      = {0};

  // create summary-list
  json_for_each_value(json_get(data, "historical_summaries"), entry) {
    buffer_append(&list_data, json_get_bytes(entry, "block_summary_root", &buf));
    buffer_append(&list_data, json_get_bytes(entry, "state_summary_root", &buf));
  }

  // create the proofs
  bytes_t block_idx_proof  = ssz_create_proof((ssz_ob_t) {.bytes = blocks, .def = &BLOCKS}, root, block_gidx);
  bytes_t period_idx_proof = ssz_create_proof((ssz_ob_t) {.bytes = list_data.data, .def = &SUMMARIES}, root, period_gidx);

  // combine the proofs
  buffer_append(&full_proof, block_idx_proof);
  buffer_append(&full_proof, period_idx_proof);               // add the proof from summary to the root of the list.
  json_for_each_value(json_get(data, "proof"), entry)         // add the proof from the root of the list to the root of the state.
      buffer_append(&full_proof, json_as_bytes(entry, &buf)); // as provided by lodestar

  // calc header
  ssz_hash_tree_root(block.body, body_root);
  block_proof->historic_proof = full_proof.data;
  block_proof->gindex         = ssz_add_gindex(ssz_add_gindex(summaries_gidx, period_gidx), block_gidx);
  block_proof->sync_aggregate = block.sync_aggregate;
  block_proof->proof_header   = bytes(safe_malloc(112), 112);
  memcpy(block_proof->proof_header.data, block.header.bytes.data, 112 - 32);
  memcpy(block_proof->proof_header.data + 112 - 32, body_root, 32);

  safe_free(block_idx_proof.data);
  safe_free(period_idx_proof.data);
  safe_free(list_data.data.data);

  /*
  buffer_t debug      = {0};
  bytes_t  block_root = bytes_slice(blocks, 32 * block_idx, 32);
  bprintf(&debug, "block_root: 0x%b\n", block_root);
  ssz_verify_single_merkle_proof(block_proof->historic_proof, block_root.data, block_proof->gindex, root);
  bprintf(&debug, "merkle_root: 0x%b\n", bytes(root, 32));
  bprintf(&debug, "state_root: 0x%b\n", ssz_get(&block_proof->proof_header, "stateRoot").bytes);
  printf("%s\n", (char*) debug.data.data);
  */
  return C4_SUCCESS;
}

void ssz_add_blockroot_proof(ssz_builder_t* builder, beacon_block_t* block_data, blockroot_proof_t block_proof) {
  if (block_proof.historic_proof.data) {
    ssz_builder_t bp = ssz_builder_for_def(ssz_get_def(builder->def, "historic_proof")->def.container.elements + 1);
    ssz_add_bytes(&bp, "proof", block_proof.historic_proof);
    ssz_add_bytes(&bp, "header", block_proof.proof_header);
    ssz_add_uint64(&bp, (uint64_t) block_proof.gindex);
    ssz_add_builders(builder, "historic_proof", bp);
    ssz_add_bytes(builder, "sync_committee_bits", ssz_get(&block_proof.sync_aggregate, "syncCommitteeBits").bytes);
    ssz_add_bytes(builder, "sync_committee_signature", ssz_get(&block_proof.sync_aggregate, "syncCommitteeSignature").bytes);
  }
  else {
    ssz_add_bytes(builder, "historic_proof", bytes(NULL, 1));
    ssz_add_bytes(builder, "sync_committee_bits", ssz_get(&block_data->sync_aggregate, "syncCommitteeBits").bytes);
    ssz_add_bytes(builder, "sync_committee_signature", ssz_get(&block_data->sync_aggregate, "syncCommitteeSignature").bytes);
  }
}
