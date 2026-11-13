/*
 * Copyright (c) 2025,2026 corpus.core GmbH
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

#include "op_verify.h"
#include "beacon_types.h"
#include "chains.h"
#include "ssz.h"

// : OP-Stack
//
// OP-Stack proofs use the same `C4Request` / proof unions as Ethereum. The only
// OP-specific wire difference is `ETH_EL_PROOF_UNION` index 2 (`sequencerProof`).
//
// The supported RPC methods are the same set as on Ethereum, including which
// methods are proofable, local, or not verifiable yet. That list is published
// once: [Supported RPC-Methods](https://corpus-core.gitbook.io/specification-colibri-stateless/specifications/ethereum/supported-rpc-methods).
//
// Block proofs are the exception. Ethereum binds the execution payload to a
// beacon block (`clProof`). An OP-Stack proof carries `sequencerProof` instead.
// The verifier recovers the sequencer signature over the execution payload and
// checks it against the sequencer address configured for the chain.
//
// ## Supported chains
//
// `c4_chain_type` maps these chain ids to OP-Stack:
//
// | Chain | Chain ID |
// |-------|----------|
// | OP Mainnet | 10 |
// | Base | 8453 |
// | Worldchain | 480 |
// | Zora | 7777777 |
// | Unichain | 130 |
// | PGN | 424 |
// | Orderly Network | 291 |
// | Mode Network | 34443 |
// | Fraxtal | 252 |
// | Mantle | 5000 |
// | Klaytn | 8217 |

method_type_t c4_op_get_method_type(chain_id_t chain_id, char* method, json_t params, verify_flags_t flags) {
  if (c4_chain_type(chain_id) != C4_CHAIN_TYPE_OP) return METHOD_UNDEFINED;
  return c4_eth_classify_method(method, params, flags);
}

const ssz_def_t* c4_op_get_request_type(chain_type_t chain_type) {
  return chain_type == C4_CHAIN_TYPE_OP ? eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST) : NULL;
}

void op_init_rpc_ctx(c4_init_ctx_t* ctx) {
  (void) ctx;
  op_register_block_proof_verify();
}

bool c4_op_verify(verify_ctx_t* ctx) {
  op_register_block_proof_verify();
  if (c4_chain_type(ctx->chain_id) != C4_CHAIN_TYPE_OP) return false;
  return c4_eth_verify_request(ctx);
}
