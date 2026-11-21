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

#ifndef eth_verify_h__
#define eth_verify_h__

#include "crypto.h"
#include "state_overrides.h"
#include "verify.h"
#include <string.h>

bool verify_account_proof(verify_ctx_t* ctx);
bool verify_hybrid_account_proof(verify_ctx_t* ctx);
bool verify_tx_proof(verify_ctx_t* ctx);
bool verify_receipt_proof(verify_ctx_t* ctx);
bool verify_logs_proof(verify_ctx_t* ctx);
bool verify_logs_completeness(verify_ctx_t* ctx);
bool verify_call_proof(verify_ctx_t* ctx);
bool verify_block_proof(verify_ctx_t* ctx); // verifies ETH_BLOCK_PROOF (body union NONE for header-only methods)
bool verify_block_receipts_proof(verify_ctx_t* ctx);
bool verify_eth_local(verify_ctx_t* ctx);

#ifdef PAP
bool verify_pap_tx(verify_ctx_t* ctx);
#endif

// helper
// Bits 27-29 are Arbitrum header fields. They stay clear until the chain type
// is Arbitrum, so Ethereum JSON keeps its previous keys.
#define ETH_BLOCK_BIT_WITHDRAWALS_ROOT           (1u << 7)
#define ETH_BLOCK_BIT_WITHDRAWALS                (1u << 11)
#define ETH_BLOCK_BIT_EXCESS_BLOB_GAS            (1u << 12)
#define ETH_BLOCK_BIT_PARENT_BEACON_ROOT         (1u << 20)
#define ETH_BLOCK_BIT_BLOB_GAS_USED              (1u << 24)
#define ETH_BLOCK_BIT_L1_BLOCK_NUMBER            (1u << 27)
#define ETH_BLOCK_BIT_SEND_COUNT                 (1u << 28)
#define ETH_BLOCK_BIT_SEND_ROOT                  (1u << 29)
#define ETH_BLOCK_ARB_HEADER_BITS                (ETH_BLOCK_BIT_L1_BLOCK_NUMBER | ETH_BLOCK_BIT_SEND_COUNT | ETH_BLOCK_BIT_SEND_ROOT)
#define ETH_BLOCK_DATA_MASK_ALL                  (0xFFFFFFFFu & ~ETH_BLOCK_ARB_HEADER_BITS)
#define ETH_BLOCK_DATA_MASK_ALL_WITHOUT_REQUESTS ((ETH_BLOCK_DATA_MASK_ALL & ~(1u << 25)) & ~(1u << 26))

/**
 * Verifies a sync-committee aggregate over a beacon block root.
 *
 * Rejects the proof unless `sync_committee_bits` has a 2/3 supermajority, before
 * the signing root is taken from the block-hash cache or the validators are loaded.
 * `slot` 0 uses `header.slot + 1`. An invalid signature is tried once more with the
 * previous period's keys.
 *
 * @param ctx verification context
 * @param header attested beacon header
 * @param sync_committee_bits participation bits of the aggregate
 * @param sync_committee_signature aggregate signature
 * @param slot signing slot, or 0 for `header.slot + 1`
 * @param pubkey_hash expected sync-committee key hash, or NULL
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
c4_status_t c4_verify_blockroot_signature(verify_ctx_t* ctx, ssz_ob_t* header, ssz_ob_t* sync_committee_bits, ssz_ob_t* sync_committee_signature, uint64_t slot, bytes32_t pubkey_hash);
// Authenticates `header` via `clHeaderProof` of an `ETH_CL_HEADER_PROOF` (`block_proof`).
c4_status_t c4_verify_header(verify_ctx_t* ctx, ssz_ob_t header, ssz_ob_t block_proof);

/**
 * Verifies a sync-committee signature over an attested header and one Merkle branch.
 *
 * `leaf_header` is the requested beacon header. Its `hash_tree_root` must sit at
 * `gindex` under `proof.header.stateRoot`. The caller pins `gindex`. A `"finalized"`
 * request uses `c4_finalized_root_gindex` of the attested slot. A `"safe"` request
 * uses `c4_current_justified_root_gindex`. The proof does not carry the index;
 * both tags share the `signatureFinalized` container.
 *
 * Checks run in this order: Merkle inclusion, then `c4_verify_blockroot_signature`
 * over `proof.header`. That function requires a 2/3 sync-committee supermajority
 * before the BLS aggregate (`slot` 0, so the signing slot is `header.slot + 1`,
 * including the previous-period fallback).
 *
 * @param ctx verification context
 * @param leaf_header beacon header whose hash tree root is the Merkle leaf
 * @param proof `signatureFinalized` container (`header`, `proof`, sync aggregate)
 * @param gindex state gindex of the leaf; `0` is rejected
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
c4_status_t c4_verify_signed_checkpoint_header(verify_ctx_t* ctx, ssz_ob_t leaf_header, ssz_ob_t proof, gindex_t gindex);
// verifies an ETH_EL_PROOF_UNION. On success `el_header` holds the verified RLP header
// and `block_hash` its keccak hash. The header is NOT owned by the caller: it either points
// into the proof (`clProof`) or into a C4_DATA_TYPE_CACHE snapshot attached to
// ctx->state (`blockHash` / `sequencerProof`), so it stays valid for the lifetime of the
// verify_ctx and is freed automatically with it.
// A `clProof` is rejected when `ctx->chain_id` settles on another chain
// (`c4_get_l1_chain_id_for_l2(ctx->chain_id) != ctx->chain_id`, i.e. EEZ or Arbitrum One):
// such a chain has no consensus layer, its blocks are proven by the hook of its chain type.
// An L1 header inside a batch-poster update is verified with the chain id switched to that L1.
// `eip2935` is verified here for every chain: the history contract proves an older block
// from the state of a newer block of the same chain.
// On a chain with its own consensus layer, the request's own block rejects a `"finalized"`
// or `"safe"` tag unless the proof is a `clProof` whose `clHeaderProof` is `signatureFinalized`.
// `"safe"` then checks `c4_current_justified_root_gindex`; `"finalized"` checks `c4_finalized_root_gindex`.
// `c4_verify_block` sets `VERIFY_FLAG_INNER_BLOCK` for the duration of the call, so a nested
// proof (EEZ L1, eip2935) does not apply that requirement again. Recursion is bounded by
// `ssz_is_valid` (`SSZ_MAX_NESTING`) before a request is verified.
c4_status_t c4_verify_block(verify_ctx_t* ctx, ssz_ob_t block, bytes_t* el_header, bytes32_t block_hash);

/**
 * Extra handler for `ETH_EL_PROOF_UNION` variants that ETH itself does not
 * implement (`sequencerProof`, ...). `witnessProof` is reserved and currently
 * rejected by `c4_verify_block`. One handler per `chain_type_t`; there is no
 * fallback to another variant. Return `C4_SUCCESS` or `C4_ERROR` / `C4_PENDING`
 * like `c4_verify_block`.
 */
typedef c4_status_t (*c4_verify_block_extra_fn)(verify_ctx_t* ctx, ssz_ob_t block, bytes_t* el_header, bytes32_t block_hash);

/**
 * Registers a chain-type-specific `c4_verify_block` handler. Idempotent.
 * ETH (`blockHash`, `clProof`) stays in `c4_verify_block`; this is only for extras.
 *
 * @param chain_type chain type that owns the handler (e.g. `C4_CHAIN_TYPE_OP`)
 * @param fn handler, or NULL to clear
 */
void c4_register_block_proof_verify(chain_type_t chain_type, c4_verify_block_extra_fn fn);

/**
 * Dispatches a proof to the ETH verify implementations without the ETH chain-type
 * or sync-committee gate. Used by OP and EEZ through `c4_eth_verify_request`
 * after they register their block-proof handler. Local methods are classified
 * with `c4_eth_classify_method`, so the chain-type gate of `c4_eth_get_method_type`
 * does not hide them.
 *
 * @param ctx verification context (proof type already parsed)
 * @return true if a handler ran (success or failure is in `ctx->success` / `ctx->state`)
 */
bool c4_eth_dispatch_proof(verify_ctx_t* ctx);

/**
 * Classifies an execution-layer RPC method the same way Ethereum does.
 *
 * Chain modules call this after their own chain-type gate. `"pending"` and
 * `"earliest"` block tags, including `fromBlock` / `toBlock` of log filters,
 * are `METHOD_UNPROOFABLE`. `eth_estimateGas` is `METHOD_UNPROOFABLE` unless
 * `VERIFY_FLAG_PAP` is set, in which case it is `METHOD_LOCAL`. Other PAP
 * shortcuts match Ethereum.
 *
 * Does not check the chain type. `c4_verify_init` already rejects a NULL method.
 *
 * @param method RPC method name
 * @param params RPC params
 * @param flags verify flags (`VERIFY_FLAG_PAP` changes some methods to `METHOD_LOCAL`)
 * @return `METHOD_PROOFABLE`, `METHOD_LOCAL`, `METHOD_UNPROOFABLE`, or `METHOD_UNDEFINED`
 */
method_type_t c4_eth_classify_method(char* method, json_t params, verify_flags_t flags);

/**
 * Applies sync data, then dispatches the proof.
 *
 * `c4_update_from_sync_data` may emit a pending request (for example checkpointz
 * for the weak-subjectivity check). `c4_verify()` reads `ctx->state` for errors
 * or pending requests before returning to the caller. For an L2 that settles on
 * L1 the sync committee belongs to that L1; `c4_update_from_sync_data` switches
 * the chain id itself and restores it. Empty sync data succeeds immediately.
 *
 * The caller has already accepted the chain and registered any block-proof hook.
 *
 * @param ctx verification context
 * @return `true` when this module handled the request (success, error, or pending is in `ctx`)
 */
bool c4_eth_verify_request(verify_ctx_t* ctx);

/**
 * Builds the JSON-facing block object from a verified RLP execution header.
 *
 * `eth_getBlockByNumber` and `eth_getBlockByHash` use `body` for the transaction
 * and withdrawal lists. Header-only methods ignore `body`. On Arbitrum the nonce
 * is the raw 8 header bytes, difficulty comes from the header, and
 * `l1BlockNumber`, `sendCount`, and `sendRoot` are read from `mixHash` and
 * `extraData`. Post-Shanghai keys stay hidden when that header does not contain
 * the matching field. On every other chain those three keys stay off and
 * difficulty stays `0x0`.
 *
 * @param ctx verification context; `method` and `chain_id` select the object
 * @param el_header verified RLP execution header
 * @param include_txs true writes full transactions, false writes hashes
 * @param body proven block body, required for the full-block methods
 * @param mask optional-field bits of `ETH_BLOCK_DATA`
 * @return true when `ctx->data` holds the object
 */
bool eth_set_block_data(verify_ctx_t* ctx, bytes_t el_header, bool include_txs, ssz_ob_t* body, uint32_t mask);
bool eth_calculate_domain(chain_id_t chain_id, uint64_t slot, bytes32_t domain);
bool c4_eth_verify_accounts(verify_ctx_t* ctx, ssz_ob_t accounts, bytes32_t state_root);
bool c4_eth_matches_blocknumber(verify_ctx_t* ctx, ssz_ob_t block, json_t req_block);
bool verify_block_receipts_proof_for(verify_ctx_t* ctx, ssz_ob_t receipts_proof, bytes_t* el_header, bytes32_t block_hash);

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
 * Intentionally narrow: only the literal `"latest"` triggers a freshness check.
 * `"safe"` is authenticated by a `signatureFinalized` header proof at `c4_current_justified_root_gindex` in `c4_verify_block`.
 * `"justified"` still needs a separate witness (see #283).
 * `"finalized"` is authenticated by a `signatureFinalized` header proof in `c4_verify_block`.
 *
 * @param block_tag JSON token from `ctx->args` (usually `json_at(args, idx)`)
 * @return `true` if the token is exactly the JSON string `"latest"`
 */
bool eth_json_is_latest(json_t block_tag);

/**
 * Returns `true` if `block_tag` is a block tag that cannot be proven via the beacon chain.
 *
 * These are `"pending"` (no beacon block exists yet) and `"earliest"` (genesis / block 0
 * predates the beacon chain). Requests for such tags must fall back to a direct RPC call
 * (`METHOD_UNPROOFABLE`) and must never be accepted as a wildcard match during verification.
 *
 * @param block_tag JSON token from `ctx->args` (usually `json_at(args, idx)`)
 * @return `true` if the token is exactly `"pending"` or `"earliest"`
 */
bool eth_json_is_unproofable_tag(json_t block_tag);

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
 * The check is also **suspended for EEZ chains** (`C4_CHAIN_TYPE_EEZ`): their
 * `latest` is the last L2 block posted to L1 and can be up to 4 hours old.
 * This is a temporary exception of EEZ milestone 1; a host that needs a
 * freshness bound on such a chain must check the block timestamp itself.
 *
 * @param ctx     verification context (supplies the host lower bound + error sink)
 * @param is_latest `true` if the request used the `"latest"` block tag
 * @param has_ts  `true` if a block timestamp could be extracted from the proof
 * @param block_ts block timestamp (Unix seconds) extracted from the proof
 * @return `true` if the proof passes (or the check is inactive); `false` on error
 */
bool eth_check_latest_freshness(verify_ctx_t* ctx, bool is_latest, bool has_ts, uint64_t block_ts);

/**
 * Checks that a verified execution block is the block the request asked for.
 *
 * A proof is only an answer to the request if it was built for the requested block.
 * Without this check a valid proof of any other block would be accepted.
 *
 * - a block number has to be the number of the verified header
 * - a block hash (32 bytes) has to be the hash of the verified header
 * - `"latest"` and `"safe"` match every verified block; the age of a `"latest"` block is checked by `eth_check_latest_freshness`
 * - `"finalized"` also matches here, because this function does not learn the block number from the tag. `c4_verify_block` additionally requires a `signatureFinalized` proof for that tag
 * - `"safe"` matches here for the same reason. `c4_verify_block` additionally requires `signatureFinalized` and checks it at `c4_current_justified_root_gindex`
 * - `"pending"`, `"earliest"` and everything else is rejected
 *
 * @param ctx verification context (error sink)
 * @param req_block block identifier of the request (JSON string from `ctx->args`)
 * @param el_header verified RLP-encoded execution header
 * @param block_hash verified hash of `el_header`
 * @return `true` if the verified block is the requested one; `false` with an error set otherwise
 */
bool eth_check_requested_block(verify_ctx_t* ctx, json_t req_block, bytes_t el_header, const bytes32_t block_hash);

/**
 * Parameter index of the block-tag argument, or `-1` when the method has none.
 *
 * `-2` means the tag lives in the filter object at `params[0]` (`fromBlock` / `toBlock`).
 *
 * @param method RPC method name
 * @return parameter index, `-2` for a log filter, or `-1`
 */
static inline int eth_block_tag_index(const char* method) {
  if (!method) return -1;
  if (strcmp(method, "eth_getStorageAt") == 0 || strcmp(method, "eth_getProof") == 0) return 2;
  if (strcmp(method, "eth_call") == 0 || strcmp(method, "eth_estimateGas") == 0 ||
      strcmp(method, "colibri_simulateTransaction") == 0 || strcmp(method, "eth_getBalance") == 0 ||
      strcmp(method, "eth_getCode") == 0 || strcmp(method, "eth_getTransactionCount") == 0) return 1;
  if (strcmp(method, "eth_getBlockByNumber") == 0 || strcmp(method, "eth_getBlockHeader") == 0 ||
      strcmp(method, "eth_getBlockReceipts") == 0 || strcmp(method, "eth_getTransactionByBlockNumberAndIndex") == 0) return 0;
  if (strcmp(method, "eth_getLogs") == 0 || strcmp(method, "eth_verifyLogs") == 0) return -2;
  return -1;
}

/**
 * Returns `true` when the request asks for the finalized block.
 *
 * For `eth_getLogs` and `eth_verifyLogs` only `toBlock` counts: that is the block
 * the execution proof is anchored to. `fromBlock` stays an open range bound.
 * Methods without a block tag return `false`.
 *
 * Inline so a caller of `c4_verify_block` does not pull the rest of the ETH
 * verify objects into binaries that only need the header check.
 *
 * @param ctx verification context (`method` and `args`)
 * @return `true` when the relevant block tag is exactly `"finalized"`
 */
static inline bool eth_request_is_finalized(const verify_ctx_t* ctx) {
  if (!ctx || !ctx->method) return false;
  int    idx = eth_block_tag_index(ctx->method);
  json_t tag = idx == -2 ? json_get(json_at(ctx->args, 0), "toBlock") : idx < 0 ? (json_t) {0}
                                                                                : json_at(ctx->args, idx);
  return tag.type == JSON_TYPE_STRING && tag.len == sizeof("\"finalized\"") - 1 && tag.start &&
         strncmp(tag.start, "\"finalized\"", sizeof("\"finalized\"") - 1) == 0;
}

/**
 * Returns `true` when the request asks for the safe block.
 *
 * For `eth_getLogs` and `eth_verifyLogs` only `toBlock` counts: that is the block
 * the execution proof is anchored to. `fromBlock` stays an open range bound.
 * Methods without a block tag return `false`.
 *
 * Inline so a caller of `c4_verify_block` does not pull the rest of the ETH
 * verify objects into binaries that only need the header check.
 *
 * @param ctx verification context (`method` and `args`)
 * @return `true` when the relevant block tag is exactly `"safe"`
 */
static inline bool eth_request_is_safe(const verify_ctx_t* ctx) {
  if (!ctx || !ctx->method) return false;
  int    idx = eth_block_tag_index(ctx->method);
  json_t tag = idx == -2 ? json_get(json_at(ctx->args, 0), "toBlock") : idx < 0 ? (json_t) {0}
                                                                                : json_at(ctx->args, idx);
  return tag.type == JSON_TYPE_STRING && tag.len == sizeof("\"safe\"") - 1 && tag.start &&
         strncmp(tag.start, "\"safe\"", sizeof("\"safe\"") - 1) == 0;
}

#ifdef ETH_OBLIVIOUS
// :: Oblivious node delayed-retry
//
// The entire oblivious helper surface is compiled out when `ETH_OBLIVIOUS` is
// disabled at configure time. Call sites in the verifier and prover are gated
// the same way, so the linker can drop the generic adaptive retry-delay
// learner (`src/util/retry_delay.c`) as dead code when no chain references it.

/**
 * Maximum number of delayed retries for an oblivious `eth_getProof` request.
 * With the capped backoff (see `C4_RETRY_DELAY_MAX_MS` in `retry_delay.h`)
 * this bounds the total wait (~55s in the worst case) and prevents endless
 * loops if the node never returns the data.
 */
#define ETH_OBLIVIOUS_MAX_RETRIES 10

/**
 * Computes the next exponential-backoff delay (in milliseconds) for an
 * oblivious `eth_getProof` retry. Thin wrapper around `c4_retry_delay_for`
 * with the oblivious category, so the prover and verifier paths share a
 * single, adaptive learner that persists per chain via the storage plugin.
 *
 * Pass `data_request_t.retry_count` *before* the retry is scheduled.
 *
 * @param chain target chain (used as part of the persistence key)
 * @param retry_count number of delayed retries already performed for the request
 * @return delay in milliseconds to wait before the next retry
 */
uint32_t eth_oblivious_retry_delay(chain_id_t chain, uint16_t retry_count);

/**
 * Notifies the adaptive learner that an oblivious `eth_getProof` request
 * eventually succeeded after `retry_count` delayed retries (0 means the very
 * first request already succeeded).
 *
 * Thin wrapper around `c4_retry_delay_observe` with the oblivious category.
 * Call this only when the request was actually routed to an oblivious node;
 * otherwise the learner would be polluted by unrelated traffic.
 *
 * @param chain target chain (used as part of the persistence key)
 * @param retry_count number of delayed retries that were performed
 */
void eth_oblivious_retry_observe(chain_id_t chain, uint16_t retry_count);

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

#endif // ETH_OBLIVIOUS

/**
 * Clears ETH in-process verifier caches (header cache, PAP tx cache).
 *
 * Registered via CMake `RESET_CACHES`. Persistent storage is left untouched.
 */
void c4_eth_reset_caches(void);

#endif // eth_verify_h__
