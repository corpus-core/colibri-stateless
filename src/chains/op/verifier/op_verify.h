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

#include "verify.h"

bool op_verify_block(verify_ctx_t* ctx);
bool op_verify_tx_proof(verify_ctx_t* ctx);
bool op_verify_receipt_proof(verify_ctx_t* ctx);
bool op_verify_logs_proof(verify_ctx_t* ctx);
bool op_verify_call_proof(verify_ctx_t* ctx);
bool op_verify_account_proof(verify_ctx_t* ctx);

// extracts the execution payload from the block_proof and returns the ssz_ob if successful. Caller must free the ssz_ob_t!.
ssz_ob_t* op_extract_verified_execution_payload(verify_ctx_t* ctx, ssz_ob_t block_proof, json_t* block_number, bytes32_t parent_hash);

/**
 * Build the storage key used for the cached OP execution payload.
 * Single slot per chain - any new full payload replaces the previous one.
 *
 * @param chain_id chain identifier
 * @param out output buffer (must be at least 64 bytes)
 */
void op_payload_key(chain_id_t chain_id, char* out);

/**
 * Load a previously verified execution payload from local storage.
 *
 * @param chain_id chain identifier
 * @return raw decompressed bytes [parent_hash(32) | ssz_execution_payload], or NULL_BYTES if absent.
 *         Caller owns the returned buffer and must `safe_free(result.data)`.
 */
bytes_t op_load_cached_payload(chain_id_t chain_id);

/**
 * Persist a freshly verified execution payload and update the chain client state.
 * The previous cached entry is implicitly replaced; the chain state transitions to
 * `C4_STATE_SYNC_EXECUTION_PAYLOAD` referencing (block_number, blockhash).
 *
 * @param chain_id chain identifier
 * @param decompressed_data full decompressed preconf data: [parent_hash(32) | ssz_execution_payload]
 * @param block_number block number of the verified payload
 * @param blockhash block hash of the verified payload
 */
void op_store_cached_payload(chain_id_t chain_id, bytes_t decompressed_data, uint64_t block_number, bytes32_t blockhash);

/**
 * Chain-specific RPC-context init hook (registered via CMake `INIT_RPC_CTX`).
 *
 * Inspects `ctx->client_state` to find a cached execution payload and prepends
 * a `C4_DATA_TYPE_CACHE` `data_request_t` to `ctx->snapshots`, identified by
 * the cached payload's blockhash.
 */
void op_init_rpc_ctx(c4_init_ctx_t* ctx);

// helper
#endif // eth_verify_h__
