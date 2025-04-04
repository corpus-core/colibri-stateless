#include "beacon_types.h"
#include "bytes.h"
#include "crypto.h"
#include "eth_account.h"
#include "eth_tx.h"
#include "eth_verify.h"
#include "json.h"
#include "patricia.h"
#include "rlp.h"
#include "ssz.h"
#include "sync_committee.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define EXECUTION_PAYLOAD_ROOT_GINDEX 25

static const char* SHA3_UNCLUES = "\x1d\xcc\x4d\xe8\xde\xc7\x5d\x7a\xab\x85\xb5\x67\xb6\xcc\xd4\x1a\xd3\x12\x45\x1b\x94\x8a\x74\x13\xf0\xa1\x42\xfd\x40\xd4\x93\x47";

static ssz_builder_t create_txs_builder(verify_ctx_t* ctx, const ssz_def_t* tx_union_def, bool include_txs, ssz_ob_t txs, bytes32_t tx_root, uint64_t block_number, bytes32_t block_hash, uint64_t base_fee) {
  ssz_builder_t txs_builder = ssz_builder_for_def(tx_union_def->def.container.elements + ((int) include_txs));
  node_t*       root        = NULL;
  bytes32_t     tmp         = {0};
  buffer_t      buf         = stack_buffer(tmp);
  ssz_builder_t tx_builder  = ssz_builder_for_def(txs_builder.def->def.vector.type);

  int len = ssz_len(txs);
  for (int i = 0; i < len; i++) {
    bytes_t   raw_tx = ssz_at(txs, i).bytes;
    bytes32_t tx_hash;
    keccak(raw_tx, tx_hash);
    patricia_set_value(&root, c4_eth_create_tx_path(i, &buf), raw_tx);

    if (include_txs) {
      // we reset the builder to to avoid allocating memory too ofter and simply resuing the already allocated memory
      tx_builder.fixed.data.len   = 0;
      tx_builder.dynamic.data.len = 0;
      if (!c4_write_tx_data_from_raw(ctx, &tx_builder, raw_tx, tx_hash, block_hash, block_number, i, base_fee)) break;
      buffer_append(&tx_builder.fixed, tx_builder.dynamic.data);
      ssz_add_dynamic_list_bytes(&txs_builder, len, tx_builder.fixed.data);
    }
    else
      buffer_append(&txs_builder.fixed, bytes(tx_hash, 32));
  }
  memcpy(tx_root, patricia_get_root(root).data, 32);

  patricia_node_free(root);
  buffer_free(&tx_builder.dynamic);
  buffer_free(&tx_builder.fixed);

  return txs_builder;
}

static void set_data(verify_ctx_t* ctx, ssz_ob_t block, bytes32_t parent_root, bytes32_t withdrawel_root, bool include_txs) {
  if (ctx->data.def && ctx->data.def->type == SSZ_TYPE_CONTAINER) return;

  bytes32_t     tx_root = {0};
  ssz_builder_t data    = ssz_builder_for_type(ETH_SSZ_DATA_BLOCK);
  ssz_add_bytes(&data, "number", ssz_get(&block, "blockNumber").bytes);
  ssz_add_bytes(&data, "hash", ssz_get(&block, "blockHash").bytes);
  ssz_add_builders(&data, "transactions", create_txs_builder(ctx, ssz_get_def(data.def, "transactions"), include_txs, ssz_get(&block, "transactions"), tx_root, ssz_get_uint64(&block, "blockNumber"), ssz_get(&block, "blockHash").bytes.data, ssz_get_uint64(&block, "baseFeePerGas")));
  ssz_add_bytes(&data, "logsBloom", ssz_get(&block, "logsBloom").bytes);
  ssz_add_bytes(&data, "receiptsRoot", ssz_get(&block, "receiptsRoot").bytes);
  ssz_add_bytes(&data, "extraData", ssz_get(&block, "extraData").bytes);
  ssz_add_bytes(&data, "withdrawalsRoot", bytes(withdrawel_root, 32));
  ssz_add_bytes(&data, "baseFeePerGas", ssz_get(&block, "baseFeePerGas").bytes);
  ssz_add_bytes(&data, "nonce", bytes(NULL, 8));
  ssz_add_bytes(&data, "miner", ssz_get(&block, "feeRecipient").bytes);
  ssz_add_bytes(&data, "withdrawals", ssz_get(&block, "withdrawals").bytes);
  ssz_add_bytes(&data, "excessBlobGas", ssz_get(&block, "excessBlobGas").bytes);
  ssz_add_bytes(&data, "difficulty", NULL_BYTES);
  ssz_add_bytes(&data, "gasLimit", ssz_get(&block, "gasLimit").bytes);
  ssz_add_bytes(&data, "gasUsed", ssz_get(&block, "gasUsed").bytes);
  ssz_add_bytes(&data, "timestamp", ssz_get(&block, "timestamp").bytes);
  ssz_add_bytes(&data, "mixHash", ssz_get(&block, "prevRandao").bytes);
  ssz_add_bytes(&data, "parentHash", ssz_get(&block, "parentHash").bytes);
  ssz_add_bytes(&data, "uncles", NULL_BYTES);
  ssz_add_bytes(&data, "parentBeaconBlockRoot", bytes(parent_root, 32));
  ssz_add_bytes(&data, "sha3Uncles", bytes(SHA3_UNCLUES, 32));
  ssz_add_bytes(&data, "transactionsRoot", bytes(tx_root, 32));
  ssz_add_bytes(&data, "stateRoot", ssz_get(&block, "stateRoot").bytes);
  ssz_add_bytes(&data, "blobGasUsed", ssz_get(&block, "blobGasUsed").bytes);
  ctx->data = ssz_builder_to_bytes(&data);
  ctx->flags |= VERIFY_FLAG_FREE_DATA;

  bytes_write(ctx->data.bytes, fopen("data.ssz", "wb"), true);
}

bool verify_block_proof(verify_ctx_t* ctx) {

  json_t    block_number             = json_at(ctx->args, 0);
  bool      include_txs              = json_as_bool(json_at(ctx->args, 1));
  bytes32_t body_root                = {0};
  bytes32_t exec_root                = {0};
  ssz_ob_t  execution_payload        = ssz_get(&ctx->proof, "executionPayload");
  ssz_ob_t  proof                    = ssz_get(&ctx->proof, "proof");
  ssz_ob_t  header                   = ssz_get(&ctx->proof, "header");
  ssz_ob_t  sync_committee_bits      = ssz_get(&ctx->proof, "sync_committee_bits");
  ssz_ob_t  sync_committee_signature = ssz_get(&ctx->proof, "sync_committee_signature");

  // calculate the tree root of the execution payload
  ssz_hash_tree_root(execution_payload, exec_root);

  ssz_verify_single_merkle_proof(proof.bytes, exec_root, EXECUTION_PAYLOAD_ROOT_GINDEX, body_root);
  if (memcmp(body_root, ssz_get(&header, "bodyRoot").bytes.data, 32) != 0) RETURN_VERIFY_ERROR(ctx, "invalid body root!");
  if (c4_verify_blockroot_signature(ctx, &header, &sync_committee_bits, &sync_committee_signature, 0) != C4_SUCCESS) return false;
  ssz_hash_tree_root(ssz_get(&execution_payload, "withdrawals"), exec_root);
  set_data(ctx, execution_payload, ssz_get(&header, "parentRoot").bytes.data, exec_root, include_txs);
  if (ctx->state.error) return false;

  ctx->success = true;
  return true;
}