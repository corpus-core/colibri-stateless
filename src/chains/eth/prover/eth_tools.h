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

/**
 * Serializes a `C4Request` for the given chain from SSZ builder fragments.
 *
 * @param chain_id target chain id encoded in the request version bytes
 * @param data `data` union builder (may be empty)
 * @param proof `proof` union builder (may be empty)
 * @param sync_data `sync_data` union builder (may be empty)
 * @return encoded proof bytes; caller must `safe_free` the buffer
 */
bytes_t eth_create_proof_request(chain_id_t chain_id, ssz_builder_t data, ssz_builder_t proof, ssz_builder_t sync_data);

/**
 * Builds the Patricia-Merkle proof for transaction `tx_index` in `execution_payload`.
 *
 * @param ctx prover context
 * @param block_hash execution block hash (for receipt/tx trie context)
 * @param execution_payload SSZ execution payload or body fragment
 * @param tx_index index of the transaction in the block
 * @param tx_proof output SSZ transaction proof object
 * @return `C4_PENDING`, `C4_SUCCESS`, or `C4_ERROR`
 */
c4_status_t c4_eth_get_tx_proof(prover_ctx_t* ctx, bytes32_t block_hash, ssz_ob_t execution_payload, uint32_t tx_index, ssz_ob_t* tx_proof);

/**
 * Resolves receipt JSON and builds the SSZ receipt proof for `tx_index`.
 *
 * @param ctx prover context
 * @param block_hash execution block hash
 * @param block_receipts JSON array from `eth_getBlockReceipts` or equivalent
 * @param tx_index transaction index within the block
 * @param receipt receives the matching receipt JSON element
 * @param receipt_proof output SSZ receipt proof object
 * @return `C4_PENDING`, `C4_SUCCESS`, or `C4_ERROR`
 */
c4_status_t c4_eth_get_receipt_proof(prover_ctx_t* ctx, bytes32_t block_hash, json_t block_receipts, uint32_t tx_index, json_t* receipt, ssz_ob_t* receipt_proof);

/**
 * Materializes account/storage proofs from an access-list or trace JSON object.
 *
 * Used by `eth_call` / simulation paths to attach Patricia proofs for touched accounts.
 *
 * @param ctx prover context
 * @param trace access-list or trace JSON (`eth_createAccessList` / `debug_traceCall` shape)
 * @param block_number execution block number for state trie lookups
 * @param builder proof builder to append account proof containers to
 * @param miner fee recipient address for the block
 * @param overrides optional state overrides (may be NULL)
 * @return `C4_PENDING`, `C4_SUCCESS`, or `C4_ERROR`
 */
c4_status_t c4_get_eth_proofs(prover_ctx_t* ctx, json_t trace, uint64_t block_number, ssz_builder_t* builder, address_t miner, const eth_state_overrides_t* overrides);

/**
 * Appends the block proof section (`ETH_EL_PROOF_UNION`, sync data, header chain) to `builder`.
 *
 * @param ctx prover context
 * @param builder parent C4 request proof builder
 * @param block_data resolved execution block from beacon/hybrid/preconf
 * @param historic_block_proof historic sync / header proof state (may be partially filled)
 */
void eth_add_block_proof(prover_ctx_t* ctx, ssz_builder_t* builder, eth_block_t* block_data, blockroot_proof_t* historic_block_proof);

/**
 * Returns true if the verifier is expected to already hold `block_data`'s EL header
 * (`last_block_hash` match or hybrid header cache). Used to emit the `blockHash`
 * union variant and to decide whether a full-block proof still needs a body.
 *
 * @param ctx prover context
 * @param block_data the execution block being proven
 * @return true if a `blockHash`-only proof is safe
 */
bool eth_verifier_has_block_header(prover_ctx_t* ctx, eth_block_t* block_data);

/**
 * Extra handler that appends a chain-specific `ETH_EL_PROOF_UNION` variant
 * (e.g. `sequencerProof`). Return true if the variant was written.
 */
typedef bool (*c4_add_block_proof_extra_fn)(prover_ctx_t* ctx, ssz_builder_t* builder, eth_block_t* block_data, blockroot_proof_t* historic);

/**
 * Extra handler that fills `eth_block_t` from a chain-specific source (e.g. OP preconf).
 * Same contract as `c4_beacon_get_block_for_eth`: set `el_header`, `el_block_hash`,
 * and optionally `el_body`. Set `proof_type` to `SEQUENCER` when sequencer data
 * is present, otherwise `NONE`.
 */
typedef c4_status_t (*c4_get_el_block_extra_fn)(prover_ctx_t* ctx, json_t block, eth_block_t* out, bool with_body);

/**
 * Registers prover-side block-proof hooks for a chain type. Idempotent.
 *
 * @param chain_type chain type that owns the handlers
 * @param add handler for `eth_add_block_proof`, or NULL
 * @param get handler for `c4_beacon_get_block_for_eth`, or NULL
 */
void c4_register_block_proof_prover(chain_type_t chain_type, c4_add_block_proof_extra_fn add, c4_get_el_block_extra_fn get);

/**
 * Returns the registered `add` hook for `chain_type`, or NULL.
 *
 * @param chain_type chain type registered with `c4_register_block_proof_prover`
 * @return block-proof append hook, or NULL
 */
c4_add_block_proof_extra_fn c4_block_proof_add_fn(chain_type_t chain_type);

/**
 * Returns the registered `get` hook for `chain_type`, or NULL.
 *
 * @param chain_type chain type registered with `c4_register_block_proof_prover`
 * @return EL block fetch hook, or NULL
 */
c4_get_el_block_extra_fn c4_block_proof_get_fn(chain_type_t chain_type);

#ifdef PROVER_CACHE
/**
 * Builds a 64-byte prover-cache key for receipt trie lookups (`target` + `blockhash`).
 *
 * @param target trie key (typically receipt-related hash)
 * @param blockhash execution block hash
 * @return pointer to static/thread-local key buffer; valid until the next call
 */
uint8_t* c4_eth_receipt_cachekey(bytes32_t target, bytes32_t blockhash);

/**
 * Builds a 64-byte prover-cache key for transaction trie lookups (`target` + `blockhash`).
 *
 * @param target trie key (typically tx-related hash)
 * @param blockhash execution block hash
 * @return pointer to static/thread-local key buffer; valid until the next call
 */
uint8_t* c4_eth_tx_cachekey(bytes32_t target, bytes32_t blockhash);
#endif

#endif