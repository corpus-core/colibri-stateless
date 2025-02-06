#include "../util/json.h"
#include "../util/logger.h"
#include "../util/ssz.h"
#include "../verifier/types_beacon.h"
#include "../verifier/types_verify.h"
#include "beacon.h"
#include "proofs.h"
#include "ssz_types.h"
#include <inttypes.h> // Include this header for PRIu64 and PRIx64
#include <stdlib.h>
#include <string.h>

static c4_status_t get_eth_tx(proofer_ctx_t* ctx, json_t txhash, json_t* tx_data) {
  uint8_t  tmp[200];
  buffer_t buf = stack_buffer(tmp);
  return c4_send_eth_rpc(ctx, "eth_getTransactionByHash", bprintf(&buf, "[%J]", txhash), tx_data);
}

static c4_status_t create_eth_tx_proof(proofer_ctx_t* ctx, json_t tx_data, beacon_block_t* block_data, bytes32_t body_root, bytes_t tx_proof) {

  buffer_t      tmp           = {0};
  ssz_builder_t eth_tx_proof  = {0};
  ssz_builder_t beacon_header = {.def = (ssz_def_t*) &BEACON_BLOCKHEADER_CONTAINER, .dynamic = {0}, .fixed = {0}};
  ssz_builder_t c4_req        = {.def = (ssz_def_t*) &C4_REQUEST_CONTAINER, .dynamic = {0}, .fixed = {0}};
  uint32_t      tx_index      = json_get_uint32(tx_data, "transactionIndex");
  ssz_ob_t      block         = block_data->header;
  ssz_ob_t      raw           = ssz_at(ssz_get(&block_data->execution, "transactions"), tx_index);

  // build the header
  ssz_add_bytes(&beacon_header, "slot", ssz_get(&block, "slot").bytes);
  ssz_add_bytes(&beacon_header, "proposerIndex", ssz_get(&block, "proposerIndex").bytes);
  ssz_add_bytes(&beacon_header, "parentRoot", ssz_get(&block, "parentRoot").bytes);
  ssz_add_bytes(&beacon_header, "stateRoot", ssz_get(&block, "stateRoot").bytes);
  ssz_add_bytes(&beacon_header, "bodyRoot", bytes(body_root, 32));

  // build the proof
  ssz_add_uint8(&eth_tx_proof, ssz_union_selector_index(C4_REQUEST_PROOFS_UNION, "TransactionProof", &eth_tx_proof.def));
  ssz_add_bytes(&eth_tx_proof, "transaction", raw.bytes);
  ssz_add_uint32(&eth_tx_proof, tx_index);
  ssz_add_uint64(&eth_tx_proof, json_get_uint64(tx_data, "blockNumber"));
  ssz_add_bytes(&eth_tx_proof, "blockHash", json_get_bytes(tx_data, "blockHash", &tmp));
  ssz_add_bytes(&eth_tx_proof, "proof", tx_proof);
  ssz_add_builders(&eth_tx_proof, "header", &beacon_header);
  ssz_add_bytes(&eth_tx_proof, "sync_committee_bits", ssz_get(&block_data->sync_aggregate, "syncCommitteeBits").bytes);
  ssz_add_bytes(&eth_tx_proof, "sync_committee_signature", ssz_get(&block_data->sync_aggregate, "syncCommitteeSignature").bytes);

  // build the data
  const ssz_def_t* data_type = NULL;
  tmp.data.data[0]           = ssz_union_selector_index(C4_REQUEST_DATA_UNION, "EthTransactionData", &data_type);
  tmp.data.len               = 1;
  ssz_ob_t tx_data_ob        = ssz_from_json(tx_data, data_type);
  buffer_append(&tmp, tx_data_ob.bytes);
  free(tx_data_ob.bytes.data);

  // build the request
  ssz_add_bytes(&c4_req, "data", tmp.data);
  ssz_add_builders(&c4_req, "proof", &eth_tx_proof);
  ssz_add_bytes(&c4_req, "sync_data", bytes(NULL, 1));

  buffer_free(&tmp);
  ctx->proof = ssz_builder_to_bytes(&c4_req).bytes;
  return C4_SUCCESS;
}

c4_status_t c4_proof_transaction(proofer_ctx_t* ctx) {
  json_t txhash = json_at(ctx->params, 0);

  if (txhash.type != JSON_TYPE_STRING || txhash.len != 68 || txhash.start[1] != '0' || txhash.start[2] != 'x') {
    ctx->error = strdup("Invalid hash");
    return C4_ERROR;
  }

  json_t tx_data;
  TRY_ASYNC(get_eth_tx(ctx, txhash, &tx_data));

  uint32_t tx_index     = json_get_uint32(tx_data, "transactionIndex");
  json_t   block_number = json_get(tx_data, "blockNumber");
  if (block_number.type != JSON_TYPE_STRING || block_number.len < 5 || block_number.start[1] != '0' || block_number.start[2] != 'x') {
    ctx->error = strdup("Invalid block number");
    return C4_ERROR;
  }

  beacon_block_t block = {0};
  TRY_ASYNC(c4_beacon_get_block_for_eth(ctx, block_number, &block));

  bytes32_t body_root;
  ssz_hash_tree_root(block.body, body_root);

  bytes_t state_proof = ssz_create_multi_proof(block.body, 3,
                                               ssz_gindex(block.body.def, 2, "executionPayload", "blockNumber"),
                                               ssz_gindex(block.body.def, 2, "executionPayload", "blockHash"),
                                               /* 1704984576 + tx_index */ ssz_gindex(block.body.def, 3, "executionPayload", "transactions", tx_index)

  );
  TRY_ASYNC_FINAL(
      create_eth_tx_proof(ctx, tx_data, &block, body_root, state_proof),
      free(state_proof.data));
  return C4_SUCCESS;
}