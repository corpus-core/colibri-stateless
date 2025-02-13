#include "../util/json.h"
#include "../util/logger.h"
#include "../util/patricia.h"
#include "../util/rlp.h"
#include "../util/ssz.h"
#include "../verifier/types_beacon.h"
#include "../verifier/types_verify.h"
#include "beacon.h"
#include "eth_req.h"
#include "proofs.h"
#include "ssz_types.h"
#include <inttypes.h> // Include this header for PRIu64 and PRIx64
#include <stdlib.h>
#include <string.h>

static c4_status_t create_eth_receipt_proof(proofer_ctx_t* ctx, json_t tx_data, beacon_block_t* block_data, bytes32_t body_root, ssz_ob_t receipt_proof, json_t receipt, bytes_t tx_proof) {

  buffer_t      tmp          = {0};
  ssz_builder_t eth_tx_proof = {0};
  ssz_builder_t c4_req       = {.def = (ssz_def_t*) &C4_REQUEST_CONTAINER, .dynamic = {0}, .fixed = {0}};
  uint32_t      tx_index     = json_get_uint32(tx_data, "transactionIndex");

  // build the proof
  ssz_add_uint8(&eth_tx_proof, ssz_union_selector_index(C4_REQUEST_PROOFS_UNION, "ReceiptProof", &eth_tx_proof.def));
  ssz_add_bytes(&eth_tx_proof, "transaction", ssz_at(ssz_get(&block_data->execution, "transactions"), tx_index).bytes);
  ssz_add_uint32(&eth_tx_proof, tx_index);
  ssz_add_uint64(&eth_tx_proof, json_get_uint64(tx_data, "blockNumber"));
  ssz_add_bytes(&eth_tx_proof, "blockHash", json_get_bytes(tx_data, "blockHash", &tmp));
  ssz_add_bytes(&eth_tx_proof, "receipt_proof", receipt_proof.bytes);
  ssz_add_bytes(&eth_tx_proof, "block_proof", tx_proof);
  ssz_add_builders(&eth_tx_proof, "header", c4_proof_add_header(block_data->header, body_root));
  ssz_add_bytes(&eth_tx_proof, "sync_committee_bits", ssz_get(&block_data->sync_aggregate, "syncCommitteeBits").bytes);
  ssz_add_bytes(&eth_tx_proof, "sync_committee_signature", ssz_get(&block_data->sync_aggregate, "syncCommitteeSignature").bytes);

  // build the request
  ssz_add_bytes(&c4_req, "data", c4_proofer_add_data(tx_data, "EthReceiptData", &tmp));
  ssz_add_builders(&c4_req, "proof", eth_tx_proof);
  ssz_add_bytes(&c4_req, "sync_data", bytes(NULL, 1));

  buffer_free(&tmp);
  ctx->proof = ssz_builder_to_bytes(&c4_req).bytes;
  return C4_SUCCESS;
}

static bytes_t create_path(uint32_t tx_index, buffer_t* buf) {
  bytes32_t tmp     = {0};
  buffer_t  val_buf = stack_buffer(tmp);
  bytes_t   path    = {.data = buf->data.data, .len = 0};

  // create_path
  if (tx_index > 0) {
    buffer_add_be(&val_buf, tx_index, 4);
    path = bytes_remove_leading_zeros(bytes(tmp, 4));
  }
  buf->data.len = 0;
  rlp_add_item(buf, path);
  return buf->data;
}

bytes_t serialize_receipt(json_t r, buffer_t* buf) {
  uint8_t  tmp[300]   = {0};
  buffer_t tmp_buf    = stack_buffer(tmp);
  uint8_t  tmp2[32]   = {0};
  buffer_t short_buf  = stack_buffer(tmp2);
  buffer_t topics_buf = {0};
  buffer_t logs_buf   = {0};
  buffer_t log_buf    = {0};
  buf->data.len       = 0;
  uint8_t type        = json_get_uint8(r, "type");
  if (type > 0) rlp_add_uint64(buf, json_get_uint64(r, "status"));
  rlp_add_uint64(buf, json_get_uint64(r, "cumulativeGasUsed"));
  rlp_add_item(buf, json_get_bytes(r, "logsBloom", &tmp_buf));

  json_for_each_value(json_get(r, "logs"), log) {
    log_buf.data.len = 0;
    rlp_add_item(&log_buf, json_get_bytes(log, "address", &tmp_buf));

    topics_buf.data.len = 0;
    json_for_each_value(json_get(log, "topics"), topic)
        rlp_add_item(&topics_buf, json_as_bytes(topic, &short_buf));
    rlp_add_list(&log_buf, topics_buf.data);
    rlp_add_item(&log_buf, json_get_bytes(log, "data", &topics_buf));
    rlp_add_list(&logs_buf, log_buf.data);
  }
  rlp_add_list(buf, logs_buf.data);
  rlp_to_list(buf);
  if (type) buffer_splice(buf, 0, 0, bytes(&type, 1));
  buffer_free(&logs_buf);
  buffer_free(&log_buf);
  buffer_free(&topics_buf);
  return buf->data;
}

ssz_ob_t create_receipts_proof(json_t block_receipts, uint32_t tx_index, json_t* receipt) {
  node_t*   root         = NULL;
  json_t    r            = {0};
  bytes32_t tmp          = {0};
  buffer_t  buf          = stack_buffer(tmp);
  buffer_t  receipts_buf = {0};

  json_for_each_value(block_receipts, r) {
    uint32_t index = json_get_uint32(r, "transactionIndex");
    if (index == tx_index) *receipt = r;
    patricia_set_value(&root, create_path(index, &buf), serialize_receipt(r, &receipts_buf));
  }

  ssz_ob_t proof = patricia_create_merkle_proof(root, create_path(tx_index, &buf));

  print_hex(stderr, patricia_get_root(root), "receipts root : 0x", "\n");

  patricia_node_free(root);
  buffer_free(&buf);
  buffer_free(&receipts_buf);
  return proof;
}

c4_status_t c4_proof_receipt(proofer_ctx_t* ctx) {
  json_t txhash = json_at(ctx->params, 0);

  if (txhash.type != JSON_TYPE_STRING || txhash.len != 68 || txhash.start[1] != '0' || txhash.start[2] != 'x') {
    ctx->state.error = strdup("Invalid hash");
    return C4_ERROR;
  }

  // collect the data
  json_t         tx_data        = {0};
  json_t         block_receipts = {0};
  beacon_block_t block          = {0};
  json_t         receipt        = {0};

  TRY_ASYNC(get_eth_tx(ctx, txhash, &tx_data));

  uint32_t tx_index     = json_get_uint32(tx_data, "transactionIndex");
  json_t   block_number = json_get(tx_data, "blockNumber");
  if (block_number.type != JSON_TYPE_STRING || block_number.len < 5 || block_number.start[1] != '0' || block_number.start[2] != 'x') {
    ctx->state.error = strdup("Invalid block number");
    return C4_ERROR;
  }

  TRY_2_ASYNC(
      c4_beacon_get_block_for_eth(ctx, block_number, &block),
      eth_getBlockReceipts(ctx, block_number, &block_receipts));

  bytes32_t body_root;
  ssz_hash_tree_root(block.body, body_root);

  ssz_ob_t receipt_proof = create_receipts_proof(block_receipts, tx_index, &receipt);
  bytes_t  state_proof   = ssz_create_multi_proof(block.body, 4,
                                                  ssz_gindex(block.body.def, 2, "executionPayload", "blockNumber"),
                                                  ssz_gindex(block.body.def, 2, "executionPayload", "blockHash"),
                                                  ssz_gindex(block.body.def, 2, "executionPayload", "receiptsRoot"),
                                                  ssz_gindex(block.body.def, 3, "executionPayload", "transactions", tx_index)

     );
  TRY_ASYNC_FINAL(
      create_eth_receipt_proof(ctx, tx_data, &block, body_root, receipt_proof, receipt, state_proof),
      free(state_proof.data);
      free(receipt_proof.bytes.data));
  return C4_SUCCESS;
}