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

// : EEZ
//
// EEZ (Ethereum Economic Zone) proofs use the same `C4Request` / proof unions as
// Ethereum. The only EEZ-specific wire difference is `ETH_EL_PROOF_UNION` index 4
// (`eezL1Proof`): the L2 block hash is read from the storage of the EEZ proxy
// contract on L1. Method classification is the Ethereum catalog, shared with the
// OP-Stack module.
//
// The supported RPC methods are the same set as on Ethereum, including which
// methods are proofable, local, or not verifiable yet. That list is published
// once: [Supported RPC-Methods](https://corpus-core.gitbook.io/specification-colibri-stateless/specifications/ethereum/supported-rpc-methods).
//
// ## Supported chains
//
// `c4_chain_type` maps these chain ids to EEZ. RollupX settles on Ethereum
// mainnet, so the sync-committee state of a RollupX client is the mainnet one.
//
// | Chain | Chain ID | L1 |
// |-------|----------|----|
// | RollupX | 696991 | Ethereum mainnet |
//
// ## How a block is proven
//
// The proxy contract on L1 stores one `RollupConfig` per rollup. Its `root`
// field is the `newRoot` of the last `L2ExecutionPerformed` event: the hash of
// the last L2 block posted to L1. That value lives at
// `keccak256(pad32(rollupId) || pad32(2)) + 1`. It is an anchor, not the head
// of the L2. `latest`, `safe` and `finalized` resolve to this committed block.
// Blocks newer than the anchor are not proven yet.
//
// `ETH_EL_PROOF_UNION` carries every block proof. EEZ uses two of its members,
// and they nest. Index 4 (`eezL1Proof`) proves the anchor. Index 5 (`eip2935`)
// proves an older block and contains that `eezL1Proof` as its own block proof.
// Inside the anchor proof, `account.elProof` is index 1 (`clProof`) of the L1
// block. A cached `blockHash` is not accepted there, so an L2 hash can never
// be resolved as an L1 header.
//
// ```
// eip2935                         older L2 block, only when it is not the anchor
//   elHeader                      RLP header of that older block
//   account                       storage proof of the history contract
//     elProof = eezL1Proof        the anchor, i.e. the committed block
//       elHeader                  RLP header of the committed L2 block
//       account                   storage proof of the proxy `root` slot
//         elProof = clProof       the L1 block whose state holds `newRoot`
// ```
//
// The anchor proof accepts the L2 header only when all of the following hold.
// `account.elProof` verifies as an L1 block (sync-committee signature).
// `account` is the configured proxy and proves exactly the `root` slot against
// that block's `stateRoot`. The proven value is non-zero and equals
// `keccak256(elHeader)`. `elHeader` has the shape of an execution header.
//
// Any of the 8191 blocks before the committed hash is proven with EIP-2935
// instead of a second L1 commit. The history contract
// `0x0000F90827F1C53a10cb7A02335B175320002935` stores the hash of block `n` in
// slot `n % 8191`. The anchor's state still serves every block `b` with
// `anchor - 8191 <= b < anchor`. The `eip2935` proof reads that slot from the
// anchor state, and the verifier accepts the older header only when
// `keccak256(elHeader)` equals the stored hash and the slot is the one for
// that header's block number. Blocks older than this window need a historic
// proof, which is not supported yet.

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
