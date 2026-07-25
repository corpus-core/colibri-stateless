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

#ifndef ETH_TOOLS_H
#define ETH_TOOLS_H

#include "beacon.h"
#include "eth_prover.h"
#include "historic_proof.h"
#include "ssz.h"

#define JSON_TX_CALL_FIELDS     "{to:address,data:bytes,gas?:hexuint,value?:hexuint,gasPrice?:hexuint,from?:address}"
#define JSON_ACCESS_LIST_FIELDS "{accessList:[{address:address,storageKeys:[hex32]}],error?:string,gasUsed?:hexuint}"

// Schema for the eth_getLogs filter object (all fields optional):
//   - fromBlock/toBlock: block tag ("latest"/"safe"/...) or a hex block number
//   - blockHash: a single 32-byte block hash (mutually exclusive with from/toBlock, not enforced here)
//   - topics: array where each position is a single topic, an array of alternatives, or null (wildcard)
//   - bloomFilter: optional pre-computed query blooms used by PAP mode
//   - address: a single address or an array of addresses
#define JSON_GET_LOGS_FILTER_FIELDS "{fromBlock?:block,toBlock?:block,blockHash?:bytes32,topics?:[bytes32|[bytes32|null]|null],bloomFilter?:[bytes],address?:address|[address]}"

// Forward declaration (defined in src/chains/eth/verifier/state_overrides.h).
// Prover code only needs the pointer type.
typedef struct eth_state_overrides eth_state_overrides_t;

#define NULL_SSZ_BUILDER      (ssz_builder_t){0}
#define FROM_JSON(data, type) ssz_builder_from(ssz_from_json(data, eth_ssz_verification_type(type), &ctx->state))

bytes_t       eth_create_proof_request(chain_id_t chain_id, ssz_builder_t data, ssz_builder_t proof, ssz_builder_t sync_data);
ssz_builder_t eth_ssz_create_state_proof(prover_ctx_t* ctx, json_t block_number, beacon_block_t* block, blockroot_proof_t* historic_proof, bool is_call);
c4_status_t   c4_eth_get_receipt_proof(prover_ctx_t* ctx, bytes32_t block_hash, json_t block_receipts, uint32_t tx_index, json_t* receipt, ssz_ob_t* receipt_proof);
c4_status_t   c4_get_eth_proofs(prover_ctx_t* ctx, json_t trace, uint64_t block_number, ssz_builder_t* builder, address_t miner, const eth_state_overrides_t* overrides);

#ifdef PROVER_CACHE
uint8_t* c4_eth_receipt_cachekey(bytes32_t target, bytes32_t blockhash);
#endif

#endif