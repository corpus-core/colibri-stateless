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

#ifdef __cplusplus
extern "C" {
#endif
#include "../util/json.h"
#include "../util/state.h"
#include "../verifier/eth_tx.h"
#include "prover.h"

// get the eth transaction for the given hash
c4_status_t get_eth_tx(prover_ctx_t* ctx, json_t txhash, json_t* tx_data);

c4_status_t eth_get_proof(prover_ctx_t* ctx, json_t address, json_t storage_key, json_t* proof, uint64_t block_number);

c4_status_t eth_get_code(prover_ctx_t* ctx, json_t address, json_t* code, uint64_t block_number);
c4_status_t eth_debug_trace_call(prover_ctx_t* ctx, json_t tx, json_t* trace, uint64_t block_number);
c4_status_t eth_create_access_list(prover_ctx_t* ctx, json_t tx, json_t* trace, uint64_t block_number, json_t state_overrides);
// get the logs
c4_status_t eth_get_logs(prover_ctx_t* ctx, json_t params, json_t* logs);

// get the block receipts for the given block
c4_status_t eth_getBlockReceipts(prover_ctx_t* ctx, json_t block, json_t* receipts_array);

// serialize the receipt for the given json using the buffer to allocate memory
bytes_t c4_serialize_receipt(json_t r, buffer_t* buf);

c4_status_t c4_send_eth_rpc(prover_ctx_t* ctx, char* method, char* params, uint32_t ttl, json_t* result, data_request_t** req);

c4_status_t eth_call(prover_ctx_t* ctx, json_t tx, json_t* result, uint64_t block_number);

c4_status_t get_eth_tx_by_hash_and_index(prover_ctx_t* ctx, json_t block_hash, uint32_t index, json_t* tx_data);

#ifdef __cplusplus
}
#endif
#endif
