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
#include "el_header.h"
#include "eth_compute_units.h"
#include "eth_req.h"
#include "eth_tools.h"
#include "historic_proof.h"
#include "json.h"
#include "prover.h"
#include "ssz.h"
#include "version.h"
#include <stdlib.h>
#include <string.h>

static ssz_builder_t serialize_receipts(json_t block_receipts) {
  // serialize all receipts to RLP and build the receipts list (single pass to avoid repeated json parsing)
  ssz_builder_t receipts_builder = ssz_builder_for_def(ssz_get_def(eth_ssz_verification_type(ETH_SSZ_VERIFY_BLOCK_RECEIPTS_PROOF), "receipts"));
  buffer_t      rbuf             = {0};
  uint32_t      num_receipts     = 0;

  json_for_each_value(block_receipts, receipt) {
    ssz_add_dynamic_list_bytes(&receipts_builder, 0, c4_serialize_receipt(receipt, &rbuf));
    buffer_reset(&rbuf);
    num_receipts++;
  }
  ssz_builder_fix_list_offsets(&receipts_builder, num_receipts);
  buffer_free(&rbuf);
  return receipts_builder;
}

c4_status_t c4_proof_block_receipts(prover_ctx_t* ctx) {
  ssz_builder_t     sync_proof     = NULL_SSZ_BUILDER;
  beacon_block_t    block          = {0};
  json_t            block_receipts = {0};
  blockroot_proof_t block_proof    = {0};
  c4_status_t       status         = C4_SUCCESS;
  char              block_num_hex[32];

  CHECK_JSON(ctx->params, "[block]", "invalid block parameter!");
  TRY_ASYNC(c4_beacon_get_block_for_eth_with_body(ctx, json_at(ctx->params, 0), &block));

  // use the resolved block number for fetching receipts to avoid race conditions with "latest"
  sbprintf(block_num_hex, "\"0x%lx\"", eth_el_header_get_uint64(block.el_header, EL_BLOCK_NUMBER));
  TRY_ADD_ASYNC(status, eth_getBlockReceipts(ctx, json_parse(block_num_hex), &block_receipts));
  TRY_ADD_ASYNC(status, c4_check_blockroot_proof(ctx, &block_proof, &block));
  TRY_ADD_ASYNC(status, c4_get_syncdata_proof(ctx, &block_proof.sync, &sync_proof));
  TRY_ASYNC(status);

  // serialize all receipts to RLP and build the receipts list (single pass to avoid repeated json parsing)
  ssz_builder_t proof_builder = ssz_builder_for_type(ETH_SSZ_VERIFY_BLOCK_RECEIPTS_PROOF);
  ssz_add_bytes(&proof_builder, "transactions", ssz_get(&block.el_body, "transactions").bytes);
  ssz_add_builders(&proof_builder, "receipts", serialize_receipts(block_receipts));
  eth_add_block_proof(ctx, &proof_builder, &block, &block_proof);
  ctx->proof = eth_create_proof_request(ctx->chain_id, NULL_SSZ_BUILDER, proof_builder, sync_proof);

  c4_free_block_proof(&block_proof);
  return C4_SUCCESS;
}
