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

#ifndef op_verify_h__
#define op_verify_h__

#include "eth_verify.h"
#include "ssz.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Verifies the `sequencerProof` variant of `ETH_BLOCK_PROOF_UNION`.
 * Registered via `c4_register_block_proof_verify` for `C4_CHAIN_TYPE_OP`.
 *
 * @param ctx verification context
 * @param block selected `sequencerProof` union member
 * @param el_header receives the verified RLP header (borrowed from a ctx snapshot)
 * @param block_hash receives `keccak(el_header)`
 * @return `C4_SUCCESS` or `C4_ERROR`
 */
c4_status_t op_verify_sequencer_proof(verify_ctx_t* ctx, ssz_ob_t block, bytes_t* el_header, bytes32_t block_hash);

/**
 * Builds the RLP EL header and `ETH_BLOCK_BODY_CONTENT` from uncompressed
 * preconf bytes `[parentBeaconRoot(32) | SSZ execution_payload]`.
 * Tries Deneb / Electra / Gloas RLP layouts until `keccak(header)` matches
 * `execution_payload.blockHash`.
 *
 * @param state error sink
 * @param preconf uncompressed `[parentBeaconRoot | execution_payload]`
 * @param el_header_out owned RLP header (caller frees)
 * @param el_body_out owned body (caller frees `.bytes.data`)
 * @param block_hash_out 32-byte execution block hash
 * @return `C4_SUCCESS` or `C4_ERROR`
 */
c4_status_t op_el_from_preconf_bytes(c4_state_t* state, bytes_t preconf,
                                     bytes_t* el_header_out, ssz_ob_t* el_body_out, bytes32_t block_hash_out);

/**
 * Decompresses a ZSTD-framed preconf payload. Rejects frames larger than 32 MiB.
 *
 * @param state error sink
 * @param compressed ZSTD frame
 * @param out owned decompressed bytes (caller frees)
 * @return `C4_SUCCESS` or `C4_ERROR`
 */
c4_status_t op_decompress_preconf(c4_state_t* state, bytes_t compressed, bytes_t* out);

/**
 * Registers the OP `sequencerProof` verify hook. Idempotent.
 */
void op_register_block_proof_verify(void);

/**
 * Returns the ETH `C4Request` type for OP-Stack (same wire format as Ethereum).
 *
 * @param chain_type must be `C4_CHAIN_TYPE_OP`
 * @return ETH request SSZ def, or NULL
 */
const ssz_def_t* c4_op_get_request_type(chain_type_t chain_type);

/**
 * Chain-specific RPC-context init hook (registered via CMake `INIT_RPC_CTX`).
 * Registers the sequencer-proof verify handler.
 */
void op_init_rpc_ctx(c4_init_ctx_t* ctx);

#ifdef __cplusplus
}
#endif

#endif // op_verify_h__
