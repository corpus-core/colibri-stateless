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

#ifndef eth_req_h__
#define eth_req_h__

#ifndef DEFAULT_TTL
#define DEFAULT_TTL (3600 * 24)
#endif

#ifdef __cplusplus
extern "C" {
#endif
#include "../util/json.h"
#include "../util/state.h"
#include "../verifier/eth_tx.h"
#include "prover.h"

/** Header fields read from `eth_getBlockBy*` results. Extra properties (e.g. `transactions`) are ignored. */
#define JSON_BLOCK_HEADER_FIELDS "{number:hexuint,hash:bytes32,stateRoot:bytes32,receiptsRoot:bytes32,transactionsRoot:bytes32,logsBloom?:bytes,parentBeaconBlockRoot?:bytes32,slotNumber?:hexuint}"

/**
 * Fetches a transaction JSON object for the given hash (`eth_getTransactionByHash`).
 *
 * @param ctx prover context
 * @param txhash JSON string transaction hash
 * @param tx_data receives the RPC result object
 * @return `C4_PENDING`, `C4_SUCCESS`, or `C4_ERROR`
 */
c4_status_t get_eth_tx(prover_ctx_t* ctx, json_t txhash, json_t* tx_data);

/**
 * Fetches an account proof (`eth_getProof`) at `block_number`.
 *
 * @param ctx prover context
 * @param address account address JSON
 * @param storage_key storage slot key JSON (may be empty for account-only proof)
 * @param proof receives the RPC proof object
 * @param block_number execution block number for the state trie
 * @return `C4_PENDING`, `C4_SUCCESS`, or `C4_ERROR`
 */
c4_status_t eth_get_proof(prover_ctx_t* ctx, json_t address, json_t storage_key, json_t* proof, uint64_t block_number);

/**
 * Fetches contract bytecode (`eth_getCode`) at `block_number`.
 *
 * @param ctx prover context
 * @param address account address JSON
 * @param code receives hex-encoded bytecode
 * @param block_number execution block number
 * @return `C4_PENDING`, `C4_SUCCESS`, or `C4_ERROR`
 */
c4_status_t eth_get_code(prover_ctx_t* ctx, json_t address, json_t* code, uint64_t block_number);

/**
 * Runs `debug_traceCall` with the prestate tracer at `block_number`.
 *
 * @param ctx prover context
 * @param tx transaction call object JSON
 * @param trace receives trace JSON
 * @param block_number execution block number
 * @return `C4_PENDING`, `C4_SUCCESS`, or `C4_ERROR`
 */
c4_status_t eth_debug_trace_call(prover_ctx_t* ctx, json_t tx, json_t* trace, uint64_t block_number);

/**
 * Runs `eth_createAccessList` at `block_number` with optional state overrides.
 *
 * @param ctx prover context
 * @param tx transaction call object JSON
 * @param trace receives access-list JSON (same shape as used by proof builders)
 * @param block_number execution block number
 * @param state_overrides optional `stateOverride` object JSON
 * @return `C4_PENDING`, `C4_SUCCESS`, or `C4_ERROR`
 */
c4_status_t eth_create_access_list(prover_ctx_t* ctx, json_t tx, json_t* trace, uint64_t block_number, json_t state_overrides);

/**
 * Fetches logs matching the filter (`eth_getLogs`).
 *
 * @param ctx prover context
 * @param params filter object JSON (`JSON_GET_LOGS_FILTER_FIELDS` shape)
 * @param logs receives the RPC log array
 * @return `C4_PENDING`, `C4_SUCCESS`, or `C4_ERROR`
 */
c4_status_t eth_get_logs(prover_ctx_t* ctx, json_t params, json_t* logs);

/**
 * Fetches all receipts for a block (`eth_getBlockReceipts`).
 *
 * @param ctx prover context
 * @param block block hash or number JSON
 * @param receipts_array receives the RPC receipt array
 * @return `C4_PENDING`, `C4_SUCCESS`, or `C4_ERROR`
 */
c4_status_t eth_getBlockReceipts(prover_ctx_t* ctx, json_t block, json_t* receipts_array);

/**
 * Fetches a block header or full block (`eth_getBlockByHash` / `eth_getBlockByNumber`).
 *
 * @param ctx prover context
 * @param block block hash or number JSON
 * @param full_tx if true, request full transaction objects instead of hashes
 * @param result receives the RPC block object
 * @return `C4_PENDING`, `C4_SUCCESS`, or `C4_ERROR`
 */
c4_status_t eth_get_block(prover_ctx_t* ctx, json_t block, bool full_tx, json_t* result);

/**
 * Fetches the current chain head block number (`eth_blockNumber`).
 *
 * @param ctx prover context
 * @param number_out receives the hex-encoded block number as integer
 * @return `C4_PENDING`, `C4_SUCCESS`, or `C4_ERROR`
 */
c4_status_t eth_block_number(prover_ctx_t* ctx, uint64_t* number_out);

/**
 * Fetches a raw execution block via `debug_getRawBlock`.
 *
 * On first success the JSON-RPC hex result is decoded and stored in the request.
 * `validated` on this request means `response` is already raw RLP (not a JSON
 * envelope). Later calls reuse those bytes. The returned `bytes_t` is a view of
 * that response; the prover state owns the memory.
 *
 * @param ctx prover context
 * @param block_hash 32-byte execution block hash
 * @param result receives the decoded raw block bytes
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
c4_status_t eth_debug_get_raw_block(prover_ctx_t* ctx, const uint8_t* block_hash, bytes_t* result);

/**
 * Serializes an RPC receipt JSON object into SSZ bytes using `buf` for allocation.
 *
 * @param r receipt JSON object
 * @param buf growable buffer for SSZ encoding
 * @return SSZ-encoded receipt bytes (may reference `buf` storage)
 */
bytes_t c4_serialize_receipt(json_t r, buffer_t* buf);

/**
 * Issues a cached JSON-RPC request to the configured execution client.
 *
 * @param ctx prover context
 * @param method JSON-RPC method name
 * @param params JSON array parameter string
 * @param ttl cache TTL in seconds for the underlying `data_request_t`
 * @param result receives parsed JSON result on success
 * @param req optional; receives the underlying `data_request_t*`
 * @return `C4_PENDING`, `C4_SUCCESS`, or `C4_ERROR`
 */
c4_status_t c4_send_eth_rpc(prover_ctx_t* ctx, char* method, char* params, uint32_t ttl, json_t* result, data_request_t** req);

/**
 * Executes `eth_call` at `block_number`.
 *
 * @param ctx prover context
 * @param tx transaction call object JSON
 * @param result receives hex return data
 * @param block_number execution block number
 * @return `C4_PENDING`, `C4_SUCCESS`, or `C4_ERROR`
 */
c4_status_t eth_call(prover_ctx_t* ctx, json_t tx, json_t* result, uint64_t block_number);

/**
 * Fetches a transaction by block hash and index (`eth_getTransactionByBlockHashAndIndex`).
 *
 * @param ctx prover context
 * @param block_hash JSON block hash
 * @param index transaction index in the block
 * @param tx_data receives the RPC transaction object
 * @return `C4_PENDING`, `C4_SUCCESS`, or `C4_ERROR`
 */
c4_status_t get_eth_tx_by_hash_and_index(prover_ctx_t* ctx, json_t block_hash, uint32_t index, json_t* tx_data);

#ifdef __cplusplus
}
#endif
#endif
