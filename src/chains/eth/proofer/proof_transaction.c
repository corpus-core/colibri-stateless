#include "beacon.h"
#include "beacon_types.h"
#include "eth_req.h"
#include "eth_tools.h"
#include "json.h"
#include "logger.h"
#include "proofer.h"
#include "ssz.h"
#include "version.h"
#include <inttypes.h> // Include this header for PRIu64 and PRIx64
#include <stdlib.h>
#include <string.h>

static c4_status_t create_eth_tx_proof(proofer_ctx_t* ctx, uint32_t tx_index, json_t tx_data, beacon_block_t* block_data, bytes32_t body_root, bytes_t tx_proof) {

  ssz_builder_t eth_tx_proof = ssz_builder_for_type(ETH_SSZ_VERIFY_TRANSACTION_PROOF);
  ssz_ob_t      raw          = ssz_at(ssz_get(&block_data->execution, "transactions"), tx_index);

  // build the proof
  ssz_add_bytes(&eth_tx_proof, "transaction", raw.bytes);
  ssz_add_uint32(&eth_tx_proof, tx_index);
  ssz_add_bytes(&eth_tx_proof, "blockNumber", ssz_get(&block_data->execution, "blockNumber").bytes);
  ssz_add_bytes(&eth_tx_proof, "blockHash", ssz_get(&block_data->execution, "blockHash").bytes);
  ssz_add_uint64(&eth_tx_proof, ssz_get_uint64(&block_data->execution, "baseFeePerGas"));
  ssz_add_bytes(&eth_tx_proof, "proof", tx_proof);
  ssz_add_builders(&eth_tx_proof, "header", c4_proof_add_header(block_data->header, body_root));
  ssz_add_bytes(&eth_tx_proof, "sync_committee_bits", ssz_get(&block_data->sync_aggregate, "syncCommitteeBits").bytes);
  ssz_add_bytes(&eth_tx_proof, "sync_committee_signature", ssz_get(&block_data->sync_aggregate, "syncCommitteeSignature").bytes);

  ctx->proof = eth_create_proof_request(
      ctx->chain_id,
      (ctx->flags & C4_PROOFER_FLAG_INCLUDE_DATA) ? FROM_JSON(tx_data, ETH_SSZ_DATA_TX) : NULL_SSZ_BUILDER,
      eth_tx_proof,
      NULL_SSZ_BUILDER);

  return C4_SUCCESS;
}

c4_status_t c4_proof_transaction(proofer_ctx_t* ctx) {
  bytes32_t      body_root    = {0};
  json_t         txhash       = json_at(ctx->params, 0);
  json_t         tx_data      = {0};
  beacon_block_t block        = {0};
  uint32_t       tx_index     = 0;
  json_t         block_number = {0};
  c4_status_t    status       = C4_SUCCESS;
  if (strcmp(ctx->method, "eth_getTransactionByBlockHashAndIndex") == 0) {
    tx_index     = json_as_uint32(json_at(ctx->params, 1));
    block_number = json_at(ctx->params, 0);
    if ((ctx->flags & C4_PROOFER_FLAG_INCLUDE_DATA))
      TRY_ADD_ASYNC(status, get_eth_tx_by_hash_and_index(ctx, block_number, tx_index, &tx_data));
  }
  else {
    if (txhash.type != JSON_TYPE_STRING || txhash.len != 68 || txhash.start[1] != '0' || txhash.start[2] != 'x') THROW_ERROR("Invalid hash");
    TRY_ASYNC(get_eth_tx(ctx, txhash, &tx_data));
    tx_index     = json_get_uint32(tx_data, "transactionIndex");
    block_number = json_get(tx_data, "blockNumber");
    if (block_number.type != JSON_TYPE_STRING || block_number.len < 5 || block_number.start[1] != '0' || block_number.start[2] != 'x') THROW_ERROR("Invalid block number");
  }

  if (block_number.type != JSON_TYPE_STRING) return C4_PENDING;
  TRY_ADD_ASYNC(status, c4_beacon_get_block_for_eth(ctx, block_number, &block));
  if (status != C4_SUCCESS) return status;

  bytes_t state_proof = ssz_create_multi_proof(block.body, body_root, 4,
                                               ssz_gindex(block.body.def, 2, "executionPayload", "blockNumber"),
                                               ssz_gindex(block.body.def, 2, "executionPayload", "blockHash"),
                                               ssz_gindex(block.body.def, 2, "executionPayload", "baseFeePerGas"),
                                               ssz_gindex(block.body.def, 3, "executionPayload", "transactions", tx_index)

  );
  TRY_ASYNC_FINAL(
      create_eth_tx_proof(ctx, tx_index, tx_data, &block, body_root, state_proof),
      free(state_proof.data));
  return C4_SUCCESS;
}