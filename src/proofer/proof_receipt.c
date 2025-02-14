#include "../util/json.h"
#include "../util/logger.h"
#include "../util/patricia.h"
#include "../util/rlp.h"
#include "../util/ssz.h"
#include "../verifier/types_beacon.h"
#include "../verifier/types_verify.h"
#include "beacon.h"
#include "eth_req.h"
#include "proofer.h"
#include "ssz_types.h"
#include <inttypes.h> // Include this header for PRIu64 and PRIx64
#include <stdlib.h>
#include <string.h>

static c4_status_t create_eth_receipt_proof(proofer_ctx_t* ctx, beacon_block_t* block_data, bytes32_t body_root, ssz_ob_t receipt_proof, json_t receipt, bytes_t tx_proof) {

  buffer_t      tmp          = {0};
  ssz_builder_t eth_tx_proof = {0};
  ssz_builder_t c4_req       = {.def = (ssz_def_t*) &C4_REQUEST_CONTAINER, .dynamic = {0}, .fixed = {0}};
  uint32_t      tx_index     = json_get_uint32(receipt, "transactionIndex");

  // build the proof
  ssz_add_uint8(&eth_tx_proof, ssz_union_selector_index(C4_REQUEST_PROOFS_UNION, "ReceiptProof", &eth_tx_proof.def));
  ssz_add_bytes(&eth_tx_proof, "transaction", ssz_at(ssz_get(&block_data->execution, "transactions"), tx_index).bytes);
  ssz_add_uint32(&eth_tx_proof, tx_index);
  ssz_add_uint64(&eth_tx_proof, json_get_uint64(receipt, "blockNumber"));
  ssz_add_bytes(&eth_tx_proof, "blockHash", json_get_bytes(receipt, "blockHash", &tmp));
  ssz_add_bytes(&eth_tx_proof, "receipt_proof", receipt_proof.bytes);
  ssz_add_bytes(&eth_tx_proof, "block_proof", tx_proof);
  ssz_add_builders(&eth_tx_proof, "header", c4_proof_add_header(block_data->header, body_root));
  ssz_add_bytes(&eth_tx_proof, "sync_committee_bits", ssz_get(&block_data->sync_aggregate, "syncCommitteeBits").bytes);
  ssz_add_bytes(&eth_tx_proof, "sync_committee_signature", ssz_get(&block_data->sync_aggregate, "syncCommitteeSignature").bytes);

  // build the request
  ssz_add_bytes(&c4_req, "data", c4_proofer_add_data(receipt, "EthReceiptData", &tmp));
  ssz_add_builders(&c4_req, "proof", eth_tx_proof);
  ssz_add_bytes(&c4_req, "sync_data", bytes(NULL, 1));

  buffer_free(&tmp);
  ctx->proof = ssz_builder_to_bytes(&c4_req).bytes;
  return C4_SUCCESS;
}

static ssz_ob_t create_receipts_proof(json_t block_receipts, uint32_t tx_index, json_t* receipt) {
  node_t*   root         = NULL;
  bytes32_t tmp          = {0};
  buffer_t  receipts_buf = {0};
  buffer_t  buf          = stack_buffer(tmp);

  json_for_each_value(block_receipts, r) {
    uint32_t index = json_get_uint32(r, "transactionIndex");
    if (index == tx_index) *receipt = r;
    patricia_set_value(&root, c4_eth_create_tx_path(index, &buf), c4_serialize_receipt(r, &receipts_buf));
  }

  ssz_ob_t proof = patricia_create_merkle_proof(root, c4_eth_create_tx_path(tx_index, &buf));

  patricia_node_free(root);
  buffer_free(&buf);
  buffer_free(&receipts_buf);
  return proof;
}

c4_status_t c4_proof_receipt(proofer_ctx_t* ctx) {
  json_t         txhash         = json_at(ctx->params, 0);
  json_t         tx_data        = {0};
  json_t         block_receipts = {0};
  beacon_block_t block          = {0};
  json_t         receipt        = {0};
  bytes32_t      body_root      = {0};

  if (txhash.type != JSON_TYPE_STRING || txhash.len != 68 || txhash.start[1] != '0' || txhash.start[2] != 'x') THROW_ERROR("Invalid hash");

  TRY_ASYNC(get_eth_tx(ctx, txhash, &tx_data));

  uint32_t tx_index     = json_get_uint32(tx_data, "transactionIndex");
  json_t   block_number = json_get(tx_data, "blockNumber");
  if (block_number.type != JSON_TYPE_STRING || block_number.len < 5 || block_number.start[1] != '0' || block_number.start[2] != 'x') THROW_ERROR("Invalid block number");

  TRY_2_ASYNC(
      c4_beacon_get_block_for_eth(ctx, block_number, &block),
      eth_getBlockReceipts(ctx, block_number, &block_receipts));

  ssz_hash_tree_root(block.body, body_root);

  ssz_ob_t receipt_proof = create_receipts_proof(block_receipts, tx_index, &receipt);
  bytes_t  state_proof   = ssz_create_multi_proof(block.body, 4,
                                                  ssz_gindex(block.body.def, 2, "executionPayload", "blockNumber"),
                                                  ssz_gindex(block.body.def, 2, "executionPayload", "blockHash"),
                                                  ssz_gindex(block.body.def, 2, "executionPayload", "receiptsRoot"),
                                                  ssz_gindex(block.body.def, 3, "executionPayload", "transactions", tx_index)

     );

  TRY_ASYNC_FINAL(
      create_eth_receipt_proof(ctx, &block, body_root, receipt_proof, receipt, state_proof),

      free(state_proof.data);
      free(receipt_proof.bytes.data));
  return C4_SUCCESS;
}