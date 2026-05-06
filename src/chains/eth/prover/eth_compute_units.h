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

#ifndef ETH_COMPUTE_UNITS_H
#define ETH_COMPUTE_UNITS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "prover.h"
#include <stdint.h>
#include <string.h>

// :: Compute Units
//
// Constants and helpers used to accumulate `prover_ctx_t.compute_units` while a
// proof is being built. The accumulated value is later returned to the caller
// as the `Compute-Units` HTTP response header (see `src/server/handle_proof.c`)
// and is intended to be consumed by an upstream load balancer for API-key based
// billing.
//
// The scale is intentionally small/discrete (Alchemy-style): a typical
// `eth_blockNumber` ends up around 25 CU, an `eth_call` around 500-2000 CU,
// while a cache-hit internal sub-request only costs 1 CU. The numbers below are
// a defensible starting point; fine-tuning per chain/method is expected to
// happen later by adjusting only this header.
//
// Counting strategy:
//
// - `c4_prover_execute` resets `ctx->compute_units` to 0 at the start of each
//   pass. Because the prover may run multiple passes (one per `C4_PENDING`
//   round-trip), only the final pass -- where every sub-request is a cache hit
//   and the actual proof bytes are produced -- contributes to the published
//   value. This avoids double-counting work that is repeated across passes.
// - Hybrid mode is *not* explicitly excluded. RPC sub-requests, Beacon-API
//   fetches, internal requests and the Patricia-trie work performed by
//   `c4_eth_get_receipt_proof` are still billed when running in hybrid mode --
//   the server actually performed that work. What hybrid skips is the SSZ
//   Merkle-proof construction (`ssz_create_*proof*` call sites guarded by
//   `C4_PROVER_FLAG_HYBRID`), which keeps the published value lower than for
//   the equivalent non-hybrid request without requiring extra branches here.

// ::: Sub-request costs (network + provider billing surrogate)

#define CU_INTERNAL_REQUEST   1  // c4_send_internal_request (period_store, internal sub-prover)
#define CU_BEACON_JSON        5  // c4_send_beacon_json (small JSON GET, e.g. header lookup)
#define CU_BEACON_SSZ        15  // c4_send_beacon_ssz (full SignedBeaconBlock or LCU)

#define CU_RPC_DEFAULT       10  // eth_getBlockBy*, eth_getTransactionBy*, eth_chainId, web3_sha3, ...
#define CU_RPC_GET_CODE      15  // eth_getCode
#define CU_RPC_RECEIPT       15  // eth_getTransactionReceipt
#define CU_RPC_GET_PROOF     50  // eth_getProof (incl. storage keys)
#define CU_RPC_BLOCK_RECEIPTS 50 // eth_getBlockReceipts
#define CU_RPC_LOGS          75  // eth_getLogs
#define CU_RPC_ACCESS_LIST  150  // eth_createAccessList
#define CU_RPC_TRACE_CALL   300  // debug_traceCall (most expensive provider call)

// ::: SSZ / Merkle proof costs (server CPU)

#define CU_SSZ_PROOF              3  // ssz_create_proof (single gindex)
#define CU_SSZ_MULTI_PROOF_BASE   2  // ssz_create_multi_proof base cost
#define CU_SSZ_MULTI_PROOF_LEAF   1  // per extra gindex in a multi-proof

// ::: Patricia / receipt-trie costs (server CPU)

#define CU_PATRICIA_INSERT        1  // per element inserted into the receipts/tx trie
#define CU_PATRICIA_PROOF         5  // generation of one Patricia-Merkle proof

// ::: Historic-proof costs (server CPU on top of the sub-requests they trigger)

#define CU_HISTORIC_HEADER_HOP    5  // per step of the header-chain proof (depth bounded)
#define CU_HISTORIC_DIRECT       40  // historical_summaries + period_store proof construction
#define CU_ZK_PROOF_INCLUDE      80  // ZK-proof data attached to the sync section

// ::: Helpers

/**
 * Map an eth-RPC method name to its compute-unit cost class.
 *
 * Heavy provider-side calls (`debug_traceCall`, `eth_createAccessList`) cost the
 * most; bulk-data calls (`eth_getProof`, `eth_getBlockReceipts`, `eth_getLogs`)
 * are next; everything else falls into the default light tier.
 *
 * @param method JSON-RPC method name; NULL is treated as default.
 * @return cost class in compute units
 */
static inline uint32_t cu_for_eth_rpc_method(const char* method) {
  if (!method) return CU_RPC_DEFAULT;
  if (strcmp(method, "eth_getProof") == 0) return CU_RPC_GET_PROOF;
  if (strcmp(method, "eth_getCode") == 0) return CU_RPC_GET_CODE;
  if (strcmp(method, "debug_traceCall") == 0) return CU_RPC_TRACE_CALL;
  if (strcmp(method, "eth_createAccessList") == 0) return CU_RPC_ACCESS_LIST;
  if (strcmp(method, "eth_getBlockReceipts") == 0) return CU_RPC_BLOCK_RECEIPTS;
  if (strcmp(method, "eth_getLogs") == 0) return CU_RPC_LOGS;
  if (strcmp(method, "eth_getTransactionReceipt") == 0) return CU_RPC_RECEIPT;
  return CU_RPC_DEFAULT;
}

/**
 * Add a raw number of compute units to the prover context.
 *
 * Prefer one of the more specific helpers (`eth_cu_add_proof`,
 * `eth_cu_add_multi_proof`, `eth_cu_add_patricia`) when applicable so the
 * cost model stays in one place. No-op if `ctx` is NULL.
 */
static inline void eth_cu_add(prover_ctx_t* ctx, uint32_t cu) {
  if (ctx) ctx->compute_units += cu;
}

/** Account for a single-leaf SSZ Merkle proof (`ssz_create_proof`). */
static inline void eth_cu_add_proof(prover_ctx_t* ctx) {
  if (ctx) ctx->compute_units += CU_SSZ_PROOF;
}

/**
 * Account for an SSZ multi-proof with `gindex_count` leaves
 * (`ssz_create_multi_proof*` family).
 */
static inline void eth_cu_add_multi_proof(prover_ctx_t* ctx, uint32_t gindex_count) {
  if (ctx) ctx->compute_units += CU_SSZ_MULTI_PROOF_BASE + gindex_count * CU_SSZ_MULTI_PROOF_LEAF;
}

/**
 * Account for building a Patricia-Merkle proof over a trie of `n_inserts`
 * elements with `n_proofs` Merkle proofs extracted from it: linear cost for
 * filling the trie plus a per-proof cost.
 */
static inline void eth_cu_add_patricia(prover_ctx_t* ctx, uint32_t n_inserts, uint32_t n_proofs) {
  if (ctx) ctx->compute_units += n_inserts * CU_PATRICIA_INSERT + n_proofs * CU_PATRICIA_PROOF;
}

#ifdef __cplusplus
}
#endif

#endif /* ETH_COMPUTE_UNITS_H */
