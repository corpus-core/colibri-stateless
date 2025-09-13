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
#include "eth_req.h"
#include "eth_tools.h"
#include "json.h"
#include "logger.h"
#include "op_proofer.h"
#include "op_tools.h"
#include "op_types.h"
#include "proofer.h"
#include "ssz.h"
#include "version.h"
#include <inttypes.h> // Include this header for PRIu64 and PRIx64
#include <stdlib.h>
#include <string.h>

typedef struct {
  ssz_builder_t block_proof;
  ssz_ob_t*     execution_payload;
  uint64_t      target_block;
  bytes_t       miner;
  json_t        trace;
  ssz_builder_t accounts;

} op_call_proof_t;

static c4_status_t create_eth_call_proof(proofer_ctx_t* ctx, op_call_proof_t* proof) {

  ssz_builder_t eth_call_proof = ssz_builder_for_op_type(OP_SSZ_VERIFY_CALL_PROOF);
  ssz_add_builders(&eth_call_proof, "accounts", proof->accounts);
  ssz_add_builders(&eth_call_proof, "block_proof", proof->block_proof);

  proof->accounts    = (ssz_builder_t) {0};
  proof->block_proof = (ssz_builder_t) {0};
  ctx->proof         = op_create_proof_request(
      ctx->chain_id,
      NULL_SSZ_BUILDER,
      eth_call_proof,
      NULL_SSZ_BUILDER);

  return C4_SUCCESS;
}

static void free_proof(op_call_proof_t* proof) {
  ssz_builder_free(&proof->block_proof);
  ssz_builder_free(&proof->accounts);
  if (proof->execution_payload) safe_free(proof->execution_payload);
}

c4_status_t c4_op_proof_call(proofer_ctx_t* ctx) {
  json_t          tx           = json_at(ctx->params, 0);
  json_t          block_number = json_at(ctx->params, 1);
  c4_status_t     status       = C4_SUCCESS;
  op_call_proof_t proof        = {0};

  // get the latest block proof
  TRY_ASYNC(c4_op_create_block_proof(ctx, block_number, &proof.block_proof));
  proof.execution_payload = op_get_execution_payload(&proof.block_proof);
  proof.target_block      = ssz_get_uint64(proof.execution_payload, "blockNumber");
  proof.miner             = ssz_get(proof.execution_payload, "feeRecipient").bytes;

  TRY_ASYNC_CATCH(eth_debug_trace_call(ctx, tx, &proof.trace, proof.target_block), free_proof(&proof));
  // TRY_ASYNC_CATCH(eth_debug_trace_call(ctx, tx, &proof.trace, proof.target_block), free_proof(&proof));
  TRY_ASYNC_CATCH(c4_get_eth_proofs(ctx, tx, proof.trace, proof.target_block, &proof.accounts, proof.miner.data), free_proof(&proof));

  status = create_eth_call_proof(ctx, &proof);
  free_proof(&proof);
  return status;
}
