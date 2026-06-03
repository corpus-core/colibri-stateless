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

#include "crypto.h"
#include "state_overrides.h"
#include "verify.h"

bool verify_account_proof(verify_ctx_t* ctx);
bool verify_hybrid_account_proof(verify_ctx_t* ctx);
bool verify_tx_proof(verify_ctx_t* ctx);
bool verify_receipt_proof(verify_ctx_t* ctx);
bool verify_logs_proof(verify_ctx_t* ctx);
bool verify_call_proof(verify_ctx_t* ctx);
bool verify_block_proof(verify_ctx_t* ctx);
bool verify_block_number_proof(verify_ctx_t* ctx);
bool verify_block_header_proof(verify_ctx_t* ctx);
bool verify_block_receipts_proof(verify_ctx_t* ctx);
bool verify_eth_local(verify_ctx_t* ctx);

#ifdef PAP
bool verify_pap_tx(verify_ctx_t* ctx);
#endif

// helper
#define ETH_BLOCK_DATA_MASK_ALL                  0xFFFFFFFF
#define ETH_BLOCK_DATA_MASK_ALL_WITHOUT_REQUESTS (ETH_BLOCK_DATA_MASK_ALL & ~(1 << 25))

c4_status_t c4_verify_blockroot_signature(verify_ctx_t* ctx, ssz_ob_t* header, ssz_ob_t* sync_committee_bits, ssz_ob_t* sync_committee_signature, uint64_t slot, bytes32_t pubkey_hash);
c4_status_t c4_verify_header(verify_ctx_t* ctx, ssz_ob_t header, ssz_ob_t block_proof);
void        eth_set_block_data(verify_ctx_t* ctx, uint32_t mask, ssz_ob_t block, bytes32_t parent_root, bytes32_t withdrawel_root, bool include_txs);
bool        eth_calculate_domain(chain_id_t chain_id, uint64_t slot, bytes32_t domain);
bool        c4_eth_verify_accounts(verify_ctx_t* ctx, ssz_ob_t accounts, bytes32_t state_root);
bool        verify_block_proof_for_block(verify_ctx_t* ctx, ssz_ob_t block_proof, json_t block_number, bytes32_t execution_payload_root);
bool        verify_block_receipts_proof_for(verify_ctx_t* ctx, ssz_ob_t receipts_proof);

typedef struct evm_call_ctx evm_call_ctx_t;

/**
 * Shared EVM call verification for `eth_call`, `eth_estimateGas`, and `colibri_simulateTransaction`.
 *
 * Validates JSON args, parses state overrides, runs the EVM, processes the
 * result according to `ctx->method`, and verifies accounts against the proof.
 * The caller must populate `evm->accounts` before calling and is responsible
 * for chain-specific state root verification using `evm->state_root` afterwards.
 * Cleanup via `evm_call_ctx_free()`.
 *
 * @param ctx verification context (method is detected automatically)
 * @param evm call context with `accounts` populated; outputs written on return
 * @return true on success, false on verification failure
 */
bool verify_evm_call(verify_ctx_t* ctx, evm_call_ctx_t* evm);

/**
 * Computes the EIP-191 `personal_sign` digest for a 32-byte message.
 *
 * The digest is `keccak256("\\x19Ethereum Signed Message:\\n32" || message)`.
 *
 * @param message 32-byte message to sign
 * @param out_digest 32-byte digest output buffer
 */
void c4_eth_eip191_digest_32(const bytes32_t message, bytes32_t out_digest);

/**
 * Returns `true` if `block_tag` is the JSON string `"latest"`.
 *
 * Intentionally narrow: only the literal `"latest"` triggers a freshness check;
 * `"safe"`, `"finalized"` and `"justified"` need a separate witness (see #283).
 *
 * @param block_tag JSON token from `ctx->args` (usually `json_at(args, idx)`)
 * @return `true` if the token is exactly the JSON string `"latest"`
 */
bool eth_json_is_latest(json_t block_tag);

/**
 * Shared freshness gate for proofs targeting the `"latest"` block tag.
 *
 * Reject proofs whose block timestamp is older than `ctx->min_latest_block_ts`.
 * This prevents replay of stale `latest` proofs (a proof for an old `latest`
 * block remains cryptographically valid forever; without this check it could
 * be presented as "current" months later).
 *
 * The check is **disabled** when `ctx->min_latest_block_ts == 0` (the host
 * opted out) or when `is_latest == false` (the request used a pinned tag).
 * When the check is enabled but `has_ts == false` the gate fails closed.
 *
 * @param ctx     verification context (supplies the host lower bound + error sink)
 * @param is_latest `true` if the request used the `"latest"` block tag
 * @param has_ts  `true` if a block timestamp could be extracted from the proof
 * @param block_ts block timestamp (Unix seconds) extracted from the proof
 * @return `true` if the proof passes (or the check is inactive); `false` on error
 */
bool eth_check_latest_freshness(verify_ctx_t* ctx, bool is_latest, bool has_ts, uint64_t block_ts);

// :: Oblivious node delayed-retry

/**
 * Initial delay (in milliseconds) before the first retry of an `eth_getProof`
 * request against an oblivious TEE node that reported the requested state as
 * not yet available. Subsequent retries use exponential backoff (doubling),
 * capped at `ETH_OBLIVIOUS_RETRY_MAX_MS`. Starting low polls fast for the
 * common case (data ready after ~1s); the cap keeps the request count low for
 * slow cases. The effective spacing is this delay plus the round-trip time.
 */
#define ETH_OBLIVIOUS_RETRY_BASE_MS 500

/**
 * Upper bound (in milliseconds) for the exponential backoff between oblivious
 * `eth_getProof` retries. Caps the doubling so a slow node is not polled with
 * ever-growing gaps.
 */
#define ETH_OBLIVIOUS_RETRY_MAX_MS 8000

/**
 * Maximum number of delayed retries for an oblivious `eth_getProof` request.
 * With the capped backoff this bounds the total wait (~55s) and avoids endless
 * loops if the node never returns the data.
 */
#define ETH_OBLIVIOUS_MAX_RETRIES 10

/**
 * Computes the exponential-backoff delay (in milliseconds) for the next
 * oblivious `eth_getProof` retry, given how many retries have already happened.
 *
 * The delay is `ETH_OBLIVIOUS_RETRY_BASE_MS << retry_count`, capped at
 * `ETH_OBLIVIOUS_RETRY_MAX_MS` (so the sequence is 500, 1000, 2000, 4000, 8000,
 * 8000, ...). Pass `data_request_t.retry_count` *before* the retry is scheduled.
 *
 * @param retry_count number of delayed retries already performed for the request
 * @return delay in milliseconds to wait before the next retry
 */
uint32_t eth_oblivious_retry_delay(uint16_t retry_count);

/**
 * Returns `true` if a JSON-RPC response signals that an oblivious node could not
 * (yet) provide the requested data and the request should be retried.
 *
 * Detects the oblivious-node specific signal `error.code == -32001` together
 * with a `data non availability` message. The match is intentionally narrow so
 * a regular RPC provider's unrelated `-32001` does not trigger pointless waits.
 *
 * Lives in the verifier library (MIT) so both the verifier (`call_ctx.c`) and
 * the prover (`eth_req.c`, which depends on the verifier) can share it without
 * the prover becoming a dependency of embedded verifier-only builds.
 *
 * @param response Parsed JSON-RPC response object (as returned by `json_parse`)
 * @return `true` if the response is an oblivious "data not available" error
 */
bool eth_is_oblivious_unavailable(json_t response);

#endif // eth_verify_h__
