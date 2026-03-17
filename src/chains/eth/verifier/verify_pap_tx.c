/*
 * Copyright (c) 2025,2026 corpus.core
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#ifdef PAP

#include "beacon_types.h"
#include "bytes.h"
#include "crypto.h"
#include "eth_tx.h"
#include "eth_verify.h"
#include "json.h"
#include "pap_req.h"
#include "pap_tx_cache.h"
#include "pap_tx_cache_types.h"
#include "ssz.h"
#include "sync_committee.h"
#include "verify.h"
#include <string.h>

#define EXECUTION_PAYLOAD_ROOT_GINDEX 25

/* ── tx cache fetch ── */

static c4_status_t fetch_tx_cache_from_server(verify_ctx_t* ctx) {
  bytes_t response;
  TRY_ASYNC(pap_request_get(ctx, "tx_cache", &response));

  if (response.len > PAP_TX_CACHE_MAX_SSZ_SIZE)
    THROW_ERROR("PAP: tx_cache response exceeds size limit");

  ssz_ob_t snapshot = {.bytes = response, .def = &PAP_TX_CACHE_SNAPSHOT};
  if (!ssz_is_valid(snapshot, true, &ctx->state))
    return C4_ERROR;

  pap_tx_cache_populate_from_ssz(ctx->chain_id, response);
  if (!pap_tx_cache_is_loaded(ctx->chain_id))
    THROW_ERROR("PAP: failed to populate tx cache from server data");

  return C4_SUCCESS;
}

static c4_status_t ensure_tx_cache(verify_ctx_t* ctx) {
  if (pap_tx_cache_is_loaded(ctx->chain_id))
    return C4_SUCCESS;

  if (pap_tx_cache_load(ctx->chain_id))
    return C4_SUCCESS;

  if (!(ctx->flags & VERIFY_FLAG_REMOTE_PROVER))
    return C4_SUCCESS;

  return fetch_tx_cache_from_server(ctx);
}

/* ── block proof verification + tx extraction ── */

static bool extract_tx_from_block_proof(verify_ctx_t* ctx, ssz_ob_t proof_req,
                                        json_t          req_block,
                                        uint32_t        target_tx_index,
                                        const bytes32_t expected_tx_hash) {
  ctx->sync_data = ssz_get(&proof_req, "sync_data");
  if (!c4_update_from_sync_data(ctx)) return false;

  ssz_ob_t block_proof = ssz_get(&proof_req, "proof");
#ifdef ETH_BLOCK
  if (!verify_block_proof_for_block(ctx, block_proof, req_block, NULL))
    return false;
#else
  RETURN_VERIFY_ERROR(ctx, "PAP: block proof verification requires ETH_BLOCK");
#endif

  ssz_ob_t exec_payload = ssz_get(&block_proof, "executionPayload");
  ssz_ob_t txs          = ssz_get(&exec_payload, "transactions");
  uint32_t num_txs      = ssz_len(txs);
  if (target_tx_index >= num_txs)
    RETURN_VERIFY_ERROR(ctx, "PAP: transaction index out of range");

  bytes_t   raw_tx = ssz_at(txs, target_tx_index).bytes;
  bytes32_t tx_hash_computed;
  keccak(raw_tx, tx_hash_computed);

  if (expected_tx_hash && memcmp(tx_hash_computed, expected_tx_hash, 32) != 0)
    RETURN_VERIFY_ERROR(ctx, "PAP: extracted tx hash does not match requested hash");

  uint64_t      block_number = ssz_get_uint64(&exec_payload, "blockNumber");
  uint64_t      base_fee     = ssz_get_uint64(&exec_payload, "baseFeePerGas");
  ssz_builder_t tx_data      = ssz_builder_for_type(ETH_SSZ_DATA_TX);
  if (!c4_write_tx_data_from_raw(ctx, &tx_data, raw_tx, tx_hash_computed,
                                 ssz_get(&exec_payload, "blockHash").bytes.data,
                                 block_number, target_tx_index, base_fee)) {
    buffer_free(&tx_data.dynamic);
    buffer_free(&tx_data.fixed);
    if (!ctx->state.error)
      RETURN_VERIFY_ERROR(ctx, "PAP: failed to create tx data");
    return false;
  }

  ctx->data = ssz_builder_to_bytes(&tx_data);
  ctx->flags |= VERIFY_FLAG_FREE_DATA;
  ctx->success = true;
  return true;
}

static bool get_tx_index_and_block(verify_ctx_t* ctx, bytes32_t requested_hash, uint64_t* block_number, uint32_t* tx_index) {
  if (ensure_tx_cache(ctx) != C4_SUCCESS) return false;

  if (!pap_tx_cache_get(ctx->chain_id, requested_hash, block_number, tx_index)) {
    if (pap_tx_cache_is_pending(ctx->chain_id, requested_hash)) {

      // if it is a pending tx and we can't fetch updates from the prover -> return null
      if (!(ctx->flags & VERIFY_FLAG_REMOTE_PROVER)) {
        ctx->success = true;
        return false;
      }

      // update the tx cache from the server
      if (fetch_tx_cache_from_server(ctx) != C4_SUCCESS) return false;

      // if the tx is still not found, return null
      if (!pap_tx_cache_get(ctx->chain_id, requested_hash, block_number, tx_index)) {
        ctx->success = true;
        return false;
      }

      // we found it, so it is no longer pending
      pap_tx_cache_remove_pending(ctx->chain_id, requested_hash);
    }
    else {
      // we didn't find it in the cache and since it is not pending, we need to fetch it from the server
      uint8_t  tmp[200];
      buffer_t buf = stack_buffer(tmp);
      ssz_ob_t proof_req;
      if (pap_request_proof(ctx, ctx->method, bprintf(&buf, "[%J]", json_at(ctx->args, 0)), &proof_req) == C4_SUCCESS) {
        ctx->proof     = ssz_get(&proof_req, "proof");
        ctx->data      = ssz_get(&proof_req, "data");
        ctx->sync_data = ssz_get(&proof_req, "sync_data");
        c4_verify(ctx);
      }
      return false;
    }
  }
  else
    pap_tx_cache_remove_pending(ctx->chain_id, requested_hash);
  return true;
}

static bool pap_tx_receipt(verify_ctx_t* ctx) {
  bytes32_t requested_hash = {0};
  buffer_t  hbuf           = stack_buffer(requested_hash);
  bytes_t   h              = json_as_bytes(json_at(ctx->args, 0), &hbuf);
  char      tmp[80]        = {0};
  buffer_t  buf            = stack_buffer(tmp);
  uint64_t  block_number   = 0;
  uint32_t  tx_index       = 0;
  ssz_ob_t  proof;

  if (h.len != 32) RETURN_VERIFY_ERROR(ctx, "PAP: invalid transaction hash");

  if (!get_tx_index_and_block(ctx, requested_hash, &block_number, &tx_index)) return false;

  if (pap_request_proof(ctx, "eth_getBlockReceipts", bprintf(&buf, "[\"0x%lx\"]", block_number), &proof) != C4_SUCCESS)
    return false;

  ctx->sync_data           = ssz_get(&proof, "sync_data");
  ssz_ob_t receipt_proof   = ssz_get(&proof, "proof");
  ssz_ob_t receipts        = ssz_get(&receipt_proof, "receipts");
  ssz_ob_t transactions    = ssz_get(&receipt_proof, "transactions");
  ssz_ob_t block_hash      = ssz_get(&receipt_proof, "blockHash");
  uint64_t blk_num         = ssz_get_uint64(&receipt_proof, "blockNumber");
  uint64_t base_fee        = ssz_get_uint64(&receipt_proof, "baseFeePerGas");
  uint32_t num_receipts    = ssz_len(ssz_get(&receipt_proof, "receipts"));
  uint64_t prev_cumulative = 0;
  uint32_t next_log_index  = 0;
  if (tx_index > num_receipts) RETURN_VERIFY_ERROR(ctx, "PAP: invalid transaction index");
  if (!c4_update_from_sync_data(ctx)) return false;
  #ifdef ETH_RECEIPT
    if (!verify_block_receipts_proof_for(ctx, receipt_proof)) return false;
  #else
    RETURN_VERIFY_ERROR(ctx, "PAP: ETH_RECEIPT is not enabled");
  #endif

  ssz_builder_t builder = ssz_builder_for_def(eth_ssz_verification_type(ETH_SSZ_DATA_RECEIPT));
  for (uint32_t i = 0; i <= tx_index; i++) {
    buffer_reset(&builder.dynamic);
    buffer_reset(&builder.fixed);
    bytes_t raw_tx      = ssz_at(transactions, i).bytes;
    bytes_t raw_receipt = ssz_at(receipts, i).bytes;
    if (!c4_write_receipt_data_from_raw(ctx, &builder, raw_tx, raw_receipt, block_hash.bytes.data, blk_num, i, base_fee, &prev_cumulative, &next_log_index))
      RETURN_VERIFY_ERROR(ctx, "invalid receipt data from RLP!");
  }

  ctx->data = ssz_builder_to_bytes(&builder);
  ctx->flags |= VERIFY_FLAG_FREE_DATA;
  ctx->success = true;
  return true;
}

/* ── eth_getTransactionByHash ── */

static bool pap_tx_by_hash(verify_ctx_t* ctx) {
  uint64_t  block_number   = 0;
  uint32_t  tx_index       = 0;
  bytes32_t requested_hash = {0};
  buffer_t  hbuf           = stack_buffer(requested_hash);
  bytes_t   h              = json_as_bytes(json_at(ctx->args, 0), &hbuf);
  char      tmp[80]        = {0};
  buffer_t  buf            = stack_buffer(tmp);
  ssz_ob_t  proof;
  if (h.len != 32) RETURN_VERIFY_ERROR(ctx, "PAP: invalid transaction hash");
  if (!get_tx_index_and_block(ctx, requested_hash, &block_number, &tx_index)) return false;
  if (pap_request_proof(ctx, "eth_getBlockByNumber", bprintf(&buf, "[\"0x%lx\",true]", block_number), &proof) != C4_SUCCESS)
    return false;

  char     bn[24];
  buffer_t bn_buf = stack_buffer(bn);
  return extract_tx_from_block_proof(ctx, proof,
                                     json_parse(bprintf(&bn_buf, "\"0x%lx\"", block_number)),
                                     tx_index, requested_hash);
  ctx->success = true;
  return true;
}

/* ── eth_getTransactionByBlockNumberAndIndex ── */

static bool pap_tx_by_block_and_index(verify_ctx_t* ctx) {
  json_t   block_param = json_at(ctx->args, 0);
  uint32_t tx_index    = json_as_uint32(json_at(ctx->args, 1));

  uint8_t  tmp[200];
  buffer_t buf = stack_buffer(tmp);
  ssz_ob_t proof;
  if (pap_request_proof(ctx, "eth_getBlockByNumber",
                        bprintf(&buf, "[%J,true]", block_param), &proof) != C4_SUCCESS)
    return false;

  return extract_tx_from_block_proof(ctx, proof, block_param, tx_index, NULL);
}

/* ── eth_sendRawTransaction / eth_sendTransaction ── */

static bool pap_handle_send_tx(verify_ctx_t* ctx) {
  uint8_t  tmp[4096];
  buffer_t buf = stack_buffer(tmp);
  json_t   result;
  if (pap_request_eth_rpc(ctx, ctx->method,
                          bprintf(&buf, "[%J]", json_at(ctx->args, 0)), "bytes32", &result) != C4_SUCCESS)
    return false;

  if (result.type == JSON_TYPE_STRING && result.len == 66) {
    bytes32_t tx_hash = {0};
    buffer_t  hbuf    = stack_buffer(tx_hash);
    bytes_t   h       = json_as_bytes(result, &hbuf);
    if (h.len == 32)
      pap_tx_cache_add_pending(ctx->chain_id, tx_hash);
  }

  ctx->data    = (ssz_ob_t) {.def = &ssz_json_def, .bytes = bytes((uint8_t*) result.start, result.len)};
  ctx->success = true;
  return true;
}

/* ── public entry point ── */

bool verify_pap_tx(verify_ctx_t* ctx) {
  if (strcmp(ctx->method, "eth_getTransactionByHash") == 0)
    return pap_tx_by_hash(ctx);
  if (strcmp(ctx->method, "eth_getTransactionReceipt") == 0)
    return pap_tx_receipt(ctx);
  if (strcmp(ctx->method, "eth_getTransactionByBlockNumberAndIndex") == 0)
    return pap_tx_by_block_and_index(ctx);
  if (strcmp(ctx->method, "eth_sendRawTransaction") == 0 ||
      strcmp(ctx->method, "eth_sendTransaction") == 0)
    return pap_handle_send_tx(ctx);

  RETURN_VERIFY_ERROR(ctx, "PAP: unsupported method for PAP tx verification");
}

#endif /* PAP */
