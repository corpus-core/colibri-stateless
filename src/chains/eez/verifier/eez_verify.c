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

#include "eez_verify.h"
#include "beacon_types.h"
#include "chains.h"
#include "ssz.h"

// EEZ (Ethereum Economic Zone) proofs use the same `C4Request` / proof unions as
// Ethereum. The only EEZ-specific wire difference is `ETH_EL_PROOF_UNION` index 4
// (`eezL1Proof`): the L2 block hash is read from the storage of the EEZ proxy
// contract on L1. Method classification is the Ethereum catalog, shared with the
// OP-Stack module.

method_type_t c4_eez_get_method_type(chain_id_t chain_id, char* method, json_t params, verify_flags_t flags) {
  if (c4_chain_type(chain_id) != C4_CHAIN_TYPE_EEZ) return METHOD_UNDEFINED;
  return c4_eth_classify_method(method, params, flags);
}

const ssz_def_t* c4_eez_get_request_type(chain_type_t chain_type) {
  return chain_type == C4_CHAIN_TYPE_EEZ ? eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST) : NULL;
}

void eez_init_rpc_ctx(c4_init_ctx_t* ctx) {
  (void) ctx;
  eez_register_block_proof_verify();
}

bool c4_eez_verify(verify_ctx_t* ctx) {
  eez_register_block_proof_verify();
  // An EEZ chain id without a table entry cannot be verified: there is no proxy to check against.
  if (c4_chain_type(ctx->chain_id) != C4_CHAIN_TYPE_EEZ || !eez_get_chain_config(ctx->chain_id)) return false;
  return c4_eth_verify_request(ctx);
}
