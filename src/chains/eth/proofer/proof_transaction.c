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

typedef struct {
  ssz_ob_t sync_aggregate;
  bytes_t  historic_proof;
  gindex_t gindex;
  ssz_ob_t proof_header;
} blockroot_proof_t;
// static c4_status_t

static c4_status_t create_eth_tx_proof(proofer_ctx_t* ctx, uint32_t tx_index, beacon_block_t* block_data, bytes32_t body_root, bytes_t tx_proof, blockroot_proof_t block_proof) {

  ssz_builder_t eth_tx_proof = ssz_builder_for_type(ETH_SSZ_VERIFY_TRANSACTION_PROOF);

  // build the proof
  ssz_add_bytes(&eth_tx_proof, "transaction", ssz_at(ssz_get(&block_data->execution, "transactions"), tx_index).bytes);
  ssz_add_uint32(&eth_tx_proof, tx_index);
  ssz_add_bytes(&eth_tx_proof, "blockNumber", ssz_get(&block_data->execution, "blockNumber").bytes);
  ssz_add_bytes(&eth_tx_proof, "blockHash", ssz_get(&block_data->execution, "blockHash").bytes);
  ssz_add_uint64(&eth_tx_proof, ssz_get_uint64(&block_data->execution, "baseFeePerGas"));
  ssz_add_bytes(&eth_tx_proof, "proof", tx_proof);
  ssz_add_builders(&eth_tx_proof, "header", c4_proof_add_header(block_data->header, body_root));

  if (block_proof.historic_proof.data) {
    ssz_builder_t bp = ssz_builder_for_def(ssz_get_def(eth_tx_proof.def, "historic_proof")->def.container.elements + 1);
    ssz_add_bytes(&bp, "proof", block_proof.historic_proof);
    ssz_add_bytes(&bp, "header", block_proof.proof_header.bytes);
    ssz_add_uint64(&bp, (uint64_t) block_proof.gindex);
    ssz_add_builders(&eth_tx_proof, "historic_proof", bp);
    ssz_add_bytes(&eth_tx_proof, "sync_committee_bits", ssz_get(&block_proof.sync_aggregate, "syncCommitteeBits").bytes);
    ssz_add_bytes(&eth_tx_proof, "sync_committee_signature", ssz_get(&block_proof.sync_aggregate, "syncCommitteeSignature").bytes);
  }
  else {
    ssz_add_bytes(&eth_tx_proof, "historic_proof", bytes(NULL, 1));
    ssz_add_bytes(&eth_tx_proof, "sync_committee_bits", ssz_get(&block_data->sync_aggregate, "syncCommitteeBits").bytes);
    ssz_add_bytes(&eth_tx_proof, "sync_committee_signature", ssz_get(&block_data->sync_aggregate, "syncCommitteeSignature").bytes);
  }

  ctx->proof = eth_create_proof_request(
      ctx->chain_id,
      NULL_SSZ_BUILDER,
      eth_tx_proof,
      NULL_SSZ_BUILDER);

  return C4_SUCCESS;
}

static const ssz_def_t HISTORICAL_SUMMARY[] = {
    SSZ_BYTES32("block_summary_root"),
    SSZ_BYTES32("state_summary_root")};
static const ssz_def_t HISTORICAL_SUMMARY_CONTAINER = SSZ_CONTAINER("HISTORICAL_SUMMARY", HISTORICAL_SUMMARY);
static const ssz_def_t SUMMARIES                    = SSZ_LIST("summaries", HISTORICAL_SUMMARY_CONTAINER, 1 << 24);

static c4_status_t c4_check_historic_proof(proofer_ctx_t* ctx, blockroot_proof_t* block_proof, uint64_t slot) {
  beacon_block_t block         = {0};
  json_t         history_proof = {0};
  uint8_t        tmp[200]      = {0};
  buffer_t       buf           = stack_buffer(tmp);
  if (!ctx->client_state.len) return C4_SUCCESS;
  uint32_t state_period = c4_eth_get_last_period(ctx->client_state);
  if (!state_period) return C4_SUCCESS;                // the client does not have a state yet, so he might as well get the head and verify the block.
  if ((slot >> 13) >= state_period) return C4_SUCCESS; // the target block is within the current range of the client

  TRY_ASYNC(c4_beacon_get_block_for_eth(ctx, json_parse("\"latest\""), &block));
  TRY_ASYNC(c4_send_beacon_json(ctx, bprintf(&buf, "eth/v1/lodestar/historical_summaries/0x%b", ssz_get(&block.header, "stateRoot").bytes), NULL, 120, &history_proof));
  json_t    data         = json_get(history_proof, "data");
  uint32_t  block_period = slot >> 13;
  uint32_t  idx          = block_period - 758 - 1; // cappella fork
  buffer_t  full_proof   = {0};
  buffer_t  list_data    = {0};
  bytes32_t root         = {0};

  // create summary-list
  json_for_each_value(json_get(data, "historical_summaries"), entry) {
    buffer_append(&list_data, json_get_bytes(entry, "block_summary_root", &buf));
    buffer_append(&list_data, json_get_bytes(entry, "state_summary_root", &buf));
  }

  gindex_t summaries_idx    = 32 + 27;
  gindex_t period_idx       = ssz_gindex(&SUMMARIES, 1, idx);
  ssz_ob_t summaries        = {.bytes = list_data.data, .def = &SUMMARIES};
  bytes_t  period_idx_proof = ssz_create_proof(summaries, root, period_idx);

  buffer_append(&full_proof, period_idx_proof);
  json_for_each_value(json_get(data, "proof"), entry)
      buffer_append(&full_proof, json_as_bytes(entry, &buf));

  // now get the histo
}

c4_status_t c4_proof_transaction(proofer_ctx_t* ctx) {
  bytes32_t         body_root    = {0};
  json_t            txhash       = json_at(ctx->params, 0);
  json_t            tx_data      = {0};
  beacon_block_t    block        = {0};
  uint32_t          tx_index     = 0;
  json_t            block_number = {0};
  blockroot_proof_t block_proof  = {0};
  c4_status_t       status       = C4_SUCCESS;

  if (strcmp(ctx->method, "eth_getTransactionByBlockHashAndIndex") == 0 || strcmp(ctx->method, "eth_getTransactionByBlockNumberAndIndex") == 0) {
    tx_index     = json_as_uint32(json_at(ctx->params, 1));
    block_number = json_at(ctx->params, 0);
  }
  else {
    if (txhash.type != JSON_TYPE_STRING || txhash.len != 68 || txhash.start[1] != '0' || txhash.start[2] != 'x') THROW_ERROR("Invalid hash");
    TRY_ASYNC(get_eth_tx(ctx, txhash, &tx_data));
    tx_index     = json_get_uint32(tx_data, "transactionIndex");
    block_number = json_get(tx_data, "blockNumber");
    if (block_number.type != JSON_TYPE_STRING || block_number.len < 5 || block_number.start[1] != '0' || block_number.start[2] != 'x') THROW_ERROR("Invalid block number");
  }

  // geth the beacon-block with signature
  TRY_ADD_ASYNC(status, c4_beacon_get_block_for_eth(ctx, block_number, &block));

  // check if we need historical proofs
  if (block.slot) TRY_ADD_ASYNC(status, c4_check_historic_proof(ctx, &block_proof, block.slot));

  if (status != C4_SUCCESS) return status;

  bytes_t state_proof = ssz_create_multi_proof(block.body, body_root, 4,
                                               ssz_gindex(block.body.def, 2, "executionPayload", "blockNumber"),
                                               ssz_gindex(block.body.def, 2, "executionPayload", "blockHash"),
                                               ssz_gindex(block.body.def, 2, "executionPayload", "baseFeePerGas"),
                                               ssz_gindex(block.body.def, 3, "executionPayload", "transactions", tx_index)

  );
  TRY_ASYNC_FINAL(
      create_eth_tx_proof(ctx, tx_index, &block, body_root, state_proof, block_proof),
      safe_free(state_proof.data));
  return C4_SUCCESS;
}