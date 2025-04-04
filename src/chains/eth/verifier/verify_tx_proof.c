
#include "beacon_types.h"
#include "bytes.h"
#include "crypto.h"
#include "eth_tx.h"
#include "eth_verify.h"
#include "json.h"
#include "patricia.h"
#include "rlp.h"
#include "ssz.h"
#include "sync_committee.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GINDEX_BLOCKUMBER    806
#define GINDEX_BLOCHASH      812
#define GINDEX_BASEFEEPERGAS 811
#define GINDEX_TXINDEX_G     1704984576L // gindex of the first tx

static bool verify_merkle_proof(verify_ctx_t* ctx, ssz_ob_t proof, bytes_t block_hash, bytes_t block_number, bytes_t base_fee_per_gas, bytes_t raw, uint32_t tx_index, bytes32_t body_root) {
  uint8_t   leafes[128] = {0};                                                                                     // 4 leafes, 32 bytes each
  bytes32_t root_hash   = {0};                                                                                     // calculated body root hash
  gindex_t  gindexes[]  = {GINDEX_BLOCKUMBER, GINDEX_BLOCHASH, GINDEX_BASEFEEPERGAS, GINDEX_TXINDEX_G + tx_index}; // calculate the gindexes for the proof

  // copy leaf data
  memcpy(leafes, block_number.data, block_number.len);
  memcpy(leafes + 32, block_hash.data, block_hash.len);
  memcpy(leafes + 64, base_fee_per_gas.data, base_fee_per_gas.len);
  ssz_hash_tree_root(ssz_ob(ssz_transactions_bytes, raw), leafes + 96);

  if (!ssz_verify_multi_merkle_proof(proof.bytes, bytes(leafes, sizeof(leafes)), gindexes, root_hash)) RETURN_VERIFY_ERROR(ctx, "invalid tx proof, missing nodes!");
  if (memcmp(root_hash, body_root, 32) != 0) RETURN_VERIFY_ERROR(ctx, "invalid tx proof, body root mismatch!");
  return true;
}

static bool create_eth_tx_data(verify_ctx_t* ctx, bytes_t raw, bytes32_t block_hash, uint64_t block_number, uint64_t base_fee_per_gas, uint32_t tx_index) {
  ssz_builder_t tx_data = ssz_builder_for_type(ETH_SSZ_DATA_TX);
  bytes32_t     tx_hash = {0};
  keccak(raw, tx_hash);
  bool success = c4_write_tx_data_from_raw(ctx, &tx_data, raw, tx_hash, block_hash, block_number, tx_index, base_fee_per_gas);
  if (!success) {
    buffer_free(&tx_data.dynamic);
    buffer_free(&tx_data.fixed);
    RETURN_VERIFY_ERROR(ctx, "invalid tx proof!");
  }
  ctx->data = ssz_builder_to_bytes(&tx_data);
  ctx->flags |= VERIFY_FLAG_FREE_DATA;
  return true;
}

bool verify_tx_proof(verify_ctx_t* ctx) {
  ssz_ob_t raw                      = ssz_get(&ctx->proof, "transaction");
  ssz_ob_t tx_proof                 = ssz_get(&ctx->proof, "proof");
  ssz_ob_t tx_index                 = ssz_get(&ctx->proof, "transactionIndex");
  ssz_ob_t header                   = ssz_get(&ctx->proof, "header");
  ssz_ob_t sync_committee_bits      = ssz_get(&ctx->proof, "sync_committee_bits");
  ssz_ob_t sync_committee_signature = ssz_get(&ctx->proof, "sync_committee_signature");
  ssz_ob_t block_hash               = ssz_get(&ctx->proof, "blockHash");
  ssz_ob_t block_number             = ssz_get(&ctx->proof, "blockNumber");
  ssz_ob_t body_root                = ssz_get(&header, "bodyRoot");
  ssz_ob_t base_fee_per_gas         = ssz_get(&ctx->proof, "baseFeePerGas");

  if (ssz_is_error(header) || ssz_is_error(raw) || ssz_is_error(tx_index) || ssz_is_error(body_root) || body_root.bytes.len != 32 || ssz_is_error(tx_proof) || ssz_is_error(block_hash) || block_hash.bytes.len != 32 || ssz_is_error(block_number)) RETURN_VERIFY_ERROR(ctx, "invalid proof, missing header or blockhash_proof!");
  if (ssz_is_error(sync_committee_bits) || sync_committee_bits.bytes.len != 64 || ssz_is_error(sync_committee_signature) || sync_committee_signature.bytes.len != 96) RETURN_VERIFY_ERROR(ctx, "invalid proof, missing sync committee bits or signature!");

  if (!c4_tx_verify_tx_hash(ctx, raw.bytes)) RETURN_VERIFY_ERROR(ctx, "invalid tx hash!");
  if (!verify_merkle_proof(ctx, tx_proof, block_hash.bytes, block_number.bytes, base_fee_per_gas.bytes, raw.bytes, ssz_uint32(tx_index), body_root.bytes.data)) RETURN_VERIFY_ERROR(ctx, "invalid tx proof!");
  if (c4_verify_blockroot_signature(ctx, &header, &sync_committee_bits, &sync_committee_signature, 0) != C4_SUCCESS) return false;
  if (ctx->data.def->type == SSZ_TYPE_NONE) {
    if (!create_eth_tx_data(ctx, raw.bytes, block_hash.bytes.data, ssz_uint64(block_number), ssz_uint64(base_fee_per_gas), ssz_uint32(tx_index))) return false;
  }
  else if (!c4_tx_verify_tx_data(ctx, ctx->data, raw.bytes, block_hash.bytes.data, ssz_uint64(block_number)))
    RETURN_VERIFY_ERROR(ctx, "invalid tx data!");

  ctx->success = true;
  return true;
}