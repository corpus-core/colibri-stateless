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

#ifndef eth_verify_h__
#define eth_verify_h__

#include "verify.h"

bool verify_account_proof(verify_ctx_t* ctx);
bool verify_tx_proof(verify_ctx_t* ctx);
bool verify_receipt_proof(verify_ctx_t* ctx);
bool verify_logs_proof(verify_ctx_t* ctx);
bool verify_call_proof(verify_ctx_t* ctx);
bool verify_simulate_proof(verify_ctx_t* ctx);
bool verify_block_proof(verify_ctx_t* ctx);
bool verify_block_number_proof(verify_ctx_t* ctx);
bool verify_eth_local(verify_ctx_t* ctx);

// helper
#define ETH_BLOCK_DATA_MASK_ALL                  0xFFFFFFFF
#define ETH_BLOCK_DATA_MASK_ALL_WITHOUT_REQUESTS (ETH_BLOCK_DATA_MASK_ALL & ~(1 << 25))

c4_status_t c4_verify_blockroot_signature(verify_ctx_t* ctx, ssz_ob_t* header, ssz_ob_t* sync_committee_bits, ssz_ob_t* sync_committee_signature, uint64_t slot, bytes32_t pubkey_hash);
c4_status_t c4_verify_header(verify_ctx_t* ctx, ssz_ob_t header, ssz_ob_t block_proof);
void        eth_set_block_data(verify_ctx_t* ctx, uint32_t mask, ssz_ob_t block, bytes32_t parent_root, bytes32_t withdrawel_root, bool include_txs);
bool        eth_calculate_domain(chain_id_t chain_id, uint64_t slot, bytes32_t domain);
bool        c4_eth_verify_accounts(verify_ctx_t* ctx, ssz_ob_t accounts, bytes32_t state_root);

/**
 * Computes the EIP-191 `personal_sign` digest for a 32-byte message.
 *
 * The digest is `keccak256("\\x19Ethereum Signed Message:\\n32" || message)`.
 *
 * @param message 32-byte message to sign
 * @param out_digest 32-byte digest output buffer
 */
void c4_eth_eip191_digest_32(const bytes32_t message, bytes32_t out_digest);
#endif // eth_verify_h__
