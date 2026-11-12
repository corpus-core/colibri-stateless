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

#ifndef eez_verify_h__
#define eez_verify_h__

#include "common.h"
#include "eez_chains_conf.h"
#include "eth_verify.h"
#include "ssz.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Verifies a block proof of an EEZ chain. Registered via `c4_register_block_proof_verify`
 * for `C4_CHAIN_TYPE_EEZ`. `c4_verify_block` handles `eip2935` itself on every chain and
 * calls this hook for the variants it does not implement, above all `eezL1Proof`.
 * A direct call still accepts `eip2935` as well.
 *
 * | Variant of `ETH_EL_PROOF_UNION` | Handling                          |
 * |---------------------------------|-----------------------------------|
 * | `eezL1Proof`                    | `eez_verify_l1_proof`             |
 * | `eip2935`                       | `c4_verify_eip2935_block`         |
 * | anything else                   | rejected                          |
 *
 * `eezL1Proof` proves the anchor, the block whose hash L1 knows. `eip2935` proves a block
 * of the 8191 blocks before a newer, already proven block of this chain, which for an EEZ
 * chain is the anchor. The check itself is not EEZ-specific and lives in the Ethereum module.
 *
 * Further block proofs of EEZ chains (blocks newer than the L1 anchor or older than the
 * EIP-2935 window) get their own variant of the union and their own branch here.
 *
 * Built with external linkage when `TEST` is defined and as a `static` function
 * otherwise (`C4_STATIC_UNLESS_TEST`). The registration below takes the function's
 * address, which stays valid either way. Unit tests call it directly.
 *
 * @param ctx verification context of the L2 request
 * @param block selected member of `ETH_EL_PROOF_UNION`
 * @param el_header receives the verified RLP header of the L2 block (borrowed from the proof)
 * @param block_hash receives `keccak256(el_header)`
 * @return `C4_SUCCESS`, `C4_PENDING` (L1 sync committee data is missing) or `C4_ERROR`
 */
C4_STATIC_UNLESS_TEST c4_status_t eez_verify_block_proof(verify_ctx_t* ctx, ssz_ob_t block, bytes_t* el_header, bytes32_t block_hash);

/**
 * Verifies the `eezL1Proof` variant of `ETH_EL_PROOF_UNION`. Any other variant is an error.
 *
 * The L2 header is accepted when all of the following hold:
 *
 * 1. `account.elProof` is a `clProof` and verifies as a block of the L1 chain
 *    (sync committee signature). No other inner variant is accepted.
 * 2. `account.address` is the EEZ proxy configured for `ctx->chain_id`.
 * 3. `account.storageProof` holds exactly one entry and its key is the root
 *    slot of the configured rollup (`eez_root_slot`).
 * 4. The account proof and the storage proof verify against the `stateRoot`
 *    of the L1 block.
 * 5. The proven storage value is non-zero and equals `keccak256(elHeader)`.
 * 6. `elHeader` has the shape of an execution header (RLP list of at least 15 fields
 *    with 32-byte parent hash, state, transactions and receipts root and a 256-byte bloom,
 *    see `eth_el_header_is_valid`).
 *
 * `ctx->chain_id` is switched to the L1 chain only while the L1 block is
 * verified and is restored before the function returns, whatever the result.
 *
 * Built with external linkage when `TEST` is defined and as a `static` function
 * otherwise (`C4_STATIC_UNLESS_TEST`). Unit tests call it directly; production
 * code reaches it only through `eez_verify_block_proof` in the same file.
 *
 * @param ctx verification context of the L2 request
 * @param block selected `eezL1Proof` union member
 * @param el_header receives the verified RLP header of the L2 block (borrowed from the proof)
 * @param block_hash receives `keccak256(el_header)`
 * @return `C4_SUCCESS`, `C4_PENDING` (L1 sync committee data is missing) or `C4_ERROR`
 */
C4_STATIC_UNLESS_TEST c4_status_t eez_verify_l1_proof(verify_ctx_t* ctx, ssz_ob_t block, bytes_t* el_header, bytes32_t block_hash);

/**
 * Proves the value of the root slot of an EEZ rollup against an L1 state root.
 *
 * Checks the proxy address, that exactly the root slot is proven, the account
 * proof against `l1_state_root` and the storage proof against the storage hash
 * of the proven account. A storage leaf longer than 32 bytes, a missing leaf
 * and an exclusion proof are errors. The Merkle checks are those of
 * `eth_verify_storage_slot`; this function adds the proxy and the slot of the rollup.
 *
 * The caller is responsible for `l1_state_root`: the verifier passes the root
 * of a verified L1 block, the prover the root claimed by the L1 prover response.
 *
 * @param state error sink
 * @param conf configuration of the rollup
 * @param account `EthAccountProof` container (`accountProof`, `address`, `storageProof`)
 * @param l1_state_root 32-byte state root of the L1 block
 * @param value receives the 32-byte big-endian storage value (the L2 block hash)
 * @return `C4_SUCCESS` or `C4_ERROR`
 */
c4_status_t eez_get_l1_storage_value(c4_state_t* state, const eez_chain_config_t* conf, ssz_ob_t account, bytes_t l1_state_root, bytes32_t value);

/**
 * Registers `eez_verify_block_proof` as the block-proof verify hook of EEZ chains. Idempotent.
 */
void eez_register_block_proof_verify(void);

/**
 * Returns the ETH `C4Request` type for EEZ chains (same wire format as Ethereum).
 *
 * @param chain_type must be `C4_CHAIN_TYPE_EEZ`
 * @return ETH request SSZ def, or NULL
 */
const ssz_def_t* c4_eez_get_request_type(chain_type_t chain_type);

/**
 * Classifies an RPC method for an EEZ chain.
 *
 * @param chain_id chain of the request
 * @param method RPC method name
 * @param params RPC params (used to detect block tags that cannot be proven)
 * @param flags verify flags (unused)
 * @return method type, or `METHOD_UNDEFINED` when `chain_id` is not an EEZ chain or the method is unknown
 */
method_type_t c4_eez_get_method_type(chain_id_t chain_id, char* method, json_t params, verify_flags_t flags);

/**
 * Chain-specific RPC-context init hook (registered via CMake `INIT_RPC_CTX`).
 * Registers the block-proof verify handler of EEZ chains.
 *
 * @param ctx initialization context
 */
void eez_init_rpc_ctx(c4_init_ctx_t* ctx);

/**
 * Verifies a request of an EEZ chain (registered via CMake `VERIFY`).
 *
 * Sync data is applied to the sync committee state of the L1 chain, then the
 * proof is dispatched to the Ethereum proof verifiers, which reach
 * `eez_verify_block_proof` (and through it `eez_verify_l1_proof`) through `c4_verify_block`.
 *
 * @param ctx verification context
 * @return `false` if `ctx->chain_id` is not a configured EEZ chain, otherwise `true` (result in `ctx`)
 */
bool c4_eez_verify(verify_ctx_t* ctx);

#ifdef __cplusplus
}
#endif

#endif // eez_verify_h__
