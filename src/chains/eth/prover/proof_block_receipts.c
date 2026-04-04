/*
 * Copyright (c) 2025 corpus.core
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

#include "beacon.h"
#include "beacon_types.h"
#include "bytes.h"
#include "eth_req.h"
#include "eth_tools.h"
#include "historic_proof.h"
#include "json.h"
#include "prover.h"
#include "ssz.h"
#include "version.h"
#include <stdlib.h>
#include <string.h>

c4_status_t c4_proof_block_receipts(prover_ctx_t* ctx) {
  json_t            block_param    = json_at(ctx->params, 0);
  beacon_block_t    block          = {0};
  json_t            block_receipts = {0};
  bytes32_t         body_root      = {0};
  blockroot_proof_t block_proof    = {0};
  ssz_builder_t     sync_proof     = NULL_SSZ_BUILDER;

  // first resolve the block to get a concrete block number
  TRY_ASYNC(c4_beacon_get_block_for_eth(ctx, block_param, &block));

  if (block.header_only)
    return c4_hybrid_delegate_proof(ctx);

  // use the resolved block number for fetching receipts to avoid race conditions with "latest"
  uint64_t block_num = ssz_get_uint64(&block.execution, "blockNumber");
  char     block_num_hex[32];
  sbprintf(block_num_hex, "\"0x%lx\"", block_num);
  TRY_ASYNC(eth_getBlockReceipts(ctx, json_parse(block_num_hex), &block_receipts));

  TRY_ASYNC(c4_check_blockroot_proof(ctx, &block_proof, &block));
  TRY_ASYNC(c4_get_syncdata_proof(ctx, &block_proof.sync, &sync_proof));

  REQUEST_WORKER_THREAD_CATCH(ctx, c4_free_block_proof(&block_proof));

  ssz_builder_t proof_builder = ssz_builder_for_type(ETH_SSZ_VERIFY_BLOCK_RECEIPTS_PROOF);

  // add transactions list from execution payload
  ssz_add_bytes(&proof_builder, "transactions", ssz_get(&block.execution, "transactions").bytes);

  // serialize all receipts to RLP and build the receipts list (single pass to avoid repeated json parsing)
  const ssz_def_t* receipts_list_def = ssz_get_def(proof_builder.def, "receipts");
  ssz_builder_t    receipts_builder  = ssz_builder_for_def(receipts_list_def);
  buffer_t         rbuf             = {0};
  uint32_t         num_receipts     = 0;

  json_for_each_value(block_receipts, receipt) {
    bytes_t serialized = c4_serialize_receipt(receipt, &rbuf);
    ssz_add_dynamic_list_bytes(&receipts_builder, 0, serialized);
    buffer_reset(&rbuf);
    num_receipts++;
  }
  ssz_builder_fix_list_offsets(&receipts_builder, num_receipts);
  buffer_free(&rbuf);
  ssz_add_builders(&proof_builder, "receipts", receipts_builder);

  // add blockNumber, blockHash and baseFeePerGas (verifier builds receipt data from RLP, needs base_fee for effectiveGasPrice)
  ssz_add_bytes(&proof_builder, "blockNumber", ssz_get(&block.execution, "blockNumber").bytes);
  ssz_add_bytes(&proof_builder, "blockHash", ssz_get(&block.execution, "blockHash").bytes);
  ssz_add_bytes(&proof_builder, "baseFeePerGas", ssz_get(&block.execution, "baseFeePerGas").bytes);

  // create multi-merkle proof for blockNumber, blockHash, receiptsRoot, transactions, baseFeePerGas
  gindex_t br_gi[5] = {ssz_gindex(block.body.def, 2, "executionPayload", "blockNumber"),
                        ssz_gindex(block.body.def, 2, "executionPayload", "blockHash"),
                        ssz_gindex(block.body.def, 2, "executionPayload", "receiptsRoot"),
                        ssz_gindex(block.body.def, 2, "executionPayload", "transactions"),
                        ssz_gindex(block.body.def, 2, "executionPayload", "baseFeePerGas")};
  bytes_t  multi_proof = NULL_BYTES;
#ifdef PROVER_CACHE
  if (block.merkle_cache.valid)
    multi_proof = ssz_create_multi_proof_from_body_cache(&block.merkle_cache, body_root, br_gi, 5);
  if (!multi_proof.data)
#endif
    multi_proof = ssz_create_multi_proof(block.body, body_root, 5, br_gi[0], br_gi[1], br_gi[2], br_gi[3], br_gi[4]);
  ssz_add_bytes(&proof_builder, "block_proof", multi_proof);
  safe_free(multi_proof.data);

  // add header and header proof
  ssz_add_builders(&proof_builder, "header", c4_proof_add_header(block.header, body_root));
  ssz_add_header_proof(&proof_builder, &block, block_proof);

  // verifier builds receipt data from RLP receipts + transactions to avoid redundant payload
  ctx->proof = eth_create_proof_request(ctx->chain_id, NULL_SSZ_BUILDER, proof_builder, sync_proof);

  c4_free_block_proof(&block_proof);
  return C4_SUCCESS;
}
