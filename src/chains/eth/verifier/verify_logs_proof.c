
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

static bool verify_merkle_proof(verify_ctx_t* ctx, ssz_ob_t block, bytes32_t receipt_root) {
  ssz_ob_t  txs          = ssz_get(&block, "txs");
  int       tx_count     = ssz_len(txs);
  uint8_t*  leafes       = safe_calloc(3 + tx_count, 32);
  gindex_t* gindexes     = safe_calloc(3 + tx_count, sizeof(gindex_t));
  bytes_t   block_number = ssz_get(&block, "blockNumber").bytes;
  bytes_t   block_hash   = ssz_get(&block, "blockHash").bytes;
  ssz_ob_t  header       = ssz_get(&block, "header");
  ssz_ob_t  proof        = ssz_get(&block, "proof");
  bytes32_t root_hash    = {0}; // calculated body root hash
  bytes_t   body_root    = ssz_get(&header, "bodyRoot").bytes;

  // copy data to leafes and gindexes
  memcpy(leafes, block_number.data, block_number.len);
  memcpy(leafes + 32, block_hash.data, block_hash.len);
  memcpy(leafes + 64, receipt_root, 32);

  gindexes[0] = GINDEX_BLOCKUMBER;
  gindexes[1] = GINDEX_BLOCHASH;
  gindexes[2] = GINDEX_RECEIPT_ROOT;

  for (int i = 0; i < tx_count; i++) {
    ssz_ob_t tx = ssz_at(txs, i);
    ssz_hash_tree_root(ssz_get(&tx, "transaction"), leafes + 96 + 32 * i);
    gindexes[3 + i] = GINDEX_TXINDEX_G + ssz_get_uint64(&tx, "transactionIndex");
  }

  bool merkle_proof_match = ssz_verify_multi_merkle_proof(proof.bytes, bytes(leafes, (3 + tx_count) * 32), gindexes, root_hash);
  safe_free(leafes);
  safe_free(gindexes);

  if (!merkle_proof_match)
    RETURN_VERIFY_ERROR(ctx, "invalid tx proof, missing nodes!");
  if (memcmp(root_hash, body_root.data, 32) != 0) RETURN_VERIFY_ERROR(ctx, "invalid tx proof, body root mismatch!");
  return true;
}

static bool verify_tx(verify_ctx_t* ctx, ssz_ob_t block, ssz_ob_t tx, bytes32_t receipt_root) {
  bytes_t   raw_receipt  = {0};
  bytes32_t root_hash    = {0};
  uint32_t  log_len      = ssz_len(ctx->data);
  ssz_ob_t  tidx         = ssz_get(&tx, "transactionIndex");
  bytes_t   block_hash   = ssz_get(&block, "blockHash").bytes;
  ssz_ob_t  block_number = ssz_get(&block, "blockNumber");

  // verify receipt proof
  if (!c4_tx_verify_receipt_proof(ctx, ssz_get(&tx, "proof"), ssz_uint32(tidx), root_hash, &raw_receipt)) RETURN_VERIFY_ERROR(ctx, "invalid receipt proof!");
  if (bytes_all_zero(bytes(receipt_root, 32)))
    memcpy(receipt_root, root_hash, 32);
  else if (memcmp(receipt_root, root_hash, 32) != 0)
    RETURN_VERIFY_ERROR(ctx, "invalid receipt proof, receipt root mismatch!");

  for (int i = 0; i < log_len; i++) {
    ssz_ob_t log = ssz_at(ctx->data, i);
    if (bytes_eq(block_number.bytes, ssz_get(&log, "blockNumber").bytes) && bytes_eq(tidx.bytes, ssz_get(&log, "transactionIndex").bytes)) {
      if (!c4_tx_verify_log_data(ctx, log, block_hash.data, ssz_uint64(block_number), ssz_uint32(tidx), ssz_get(&tx, "transaction").bytes, raw_receipt)) RETURN_VERIFY_ERROR(ctx, "invalid log data!");
    }
  }
  return true;
}

static c4_status_t verif_block(verify_ctx_t* ctx, ssz_ob_t block) {
  ssz_ob_t  header                   = ssz_get(&block, "header");
  ssz_ob_t  sync_committee_bits      = ssz_get(&block, "sync_committee_bits");
  ssz_ob_t  sync_committee_signature = ssz_get(&block, "sync_committee_signature");
  ssz_ob_t  txs                      = ssz_get(&block, "txs");
  bytes32_t receipt_root             = {0};
  uint32_t  tx_count                 = ssz_len(txs);

  // verify each tx and get the receipt root
  for (int i = 0; i < tx_count; i++) {
    if (!verify_tx(ctx, block, ssz_at(txs, i), receipt_root)) THROW_ERROR("invalid receipt proof!");
  }
  if (!verify_merkle_proof(ctx, block, receipt_root)) THROW_ERROR("invalid tx proof!");
  return c4_verify_blockroot_signature(ctx, &header, &sync_committee_bits, &sync_committee_signature, 0);
}

static bool has_proof(verify_ctx_t* ctx, bytes_t block_number, bytes_t tx_index, uint32_t block_count) {
  for (int i = 0; i < block_count; i++) {
    ssz_ob_t block = ssz_at(ctx->proof, i);
    if (bytes_eq(block_number, ssz_get(&block, "blockNumber").bytes)) {
      ssz_ob_t txs      = ssz_get(&block, "txs");
      uint32_t tx_count = ssz_len(txs);
      for (int j = 0; j < tx_count; j++) {
        ssz_ob_t tx = ssz_at(txs, j);
        if (bytes_eq(tx_index, ssz_get(&tx, "transactionIndex").bytes))
          return true;
      }
      return false;
    }
  }
  return false;
}

bool verify_logs_proof(verify_ctx_t* ctx) {

  uint32_t log_count   = ssz_len(ctx->data);
  uint32_t block_count = ssz_len(ctx->proof);

  // verify each block we have a proof for
  for (int i = 0; i < block_count; i++) {
    if (verif_block(ctx, ssz_at(ctx->proof, i)) != C4_SUCCESS) return false;
  }

  // make sure we have a proof for each log
  for (int i = 0; i < log_count; i++) {
    ssz_ob_t log = ssz_at(ctx->data, i);
    if (!has_proof(ctx, ssz_get(&log, "blockNumber").bytes, ssz_get(&log, "transactionIndex").bytes, block_count)) RETURN_VERIFY_ERROR(ctx, "missing log proof!");
  }

  ctx->success = true;
  return true;
}