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

#ifndef C4_ETH_LCU_GLOAS_H
#define C4_ETH_LCU_GLOAS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "prover.h"

// Size constants for the Gloas LightClientUpdate layout.
//
//   GLOAS_LIGHT_CLIENT_UPDATE (fixed-size = 26424 bytes):
//     [    0 ..   496)  attestedHeader           (GLOAS_LIGHT_CLIENT_HEADER, 496 B)
//     [  496 .. 25120)  nextSyncCommittee        (SYNC_COMMITTEE,          24624 B)
//     [25120 .. 25472)  nextSyncCommitteeBranch  (Vector[bytes32, 11],       352 B)
//     [25472 .. 25968)  finalizedHeader          (GLOAS_LIGHT_CLIENT_HEADER, 496 B)
//     [25968 .. 26256)  finalityBranch           (Vector[bytes32,  9],       288 B)
//     [26256 .. 26416)  syncAggregate            (SYNC_AGGREGATE,            160 B)
//     [26416 .. 26424)  signatureSlot            (uint64,                      8 B)
//
//   GLOAS_LIGHT_CLIENT_HEADER = BeaconBlockHeader (112)
//                             + executionBlockHash (32)
//                             + executionBranch [Vector[bytes32, 11]] (352) = 496
#define C4_GLOAS_LCU_SSZ_SIZE             26424u
#define C4_GLOAS_LCU_HEADER_SIZE          496u
#define C4_GLOAS_LCU_SYNC_COMMITTEE_SIZE  24624u
#define C4_GLOAS_LCU_NEXT_SC_BRANCH_SIZE  352u // depth 11
#define C4_GLOAS_LCU_FINALITY_BRANCH_SIZE 288u // depth 9
#define C4_GLOAS_LCU_SYNC_AGGREGATE_SIZE  160u

// Beacon-API `light_client/updates` wire-format prefix per update:
//   [0 .. 8)  length (uint64 LE) = 4 + update_size
//   [8 .. 12) context (4 bytes) -- per Beacon API spec this is the
//             `ForkDigest = hash_tree_root(ForkData{version, gvr})[:4]`.
//             The wrapper copies whatever the caller supplies verbatim;
//             production callers should pass the digest computed via
//             `c4_eth_compute_fork_digest`.
// followed by the raw LCU-SSZ payload.
#define C4_GLOAS_LCU_WIRE_PREFIX_SIZE 12u
#define C4_GLOAS_LCU_WIRE_SIZE        (C4_GLOAS_LCU_WIRE_PREFIX_SIZE + C4_GLOAS_LCU_SSZ_SIZE)

/**
 * Builds a complete Gloas `LightClientUpdate` SSZ blob for a target
 * sync-committee period.
 *
 * Roles of the three fetched blocks:
 *   - `sig_block`  = head block that carries a non-empty `sync_aggregate`.
 *                    Provides `signature_slot` and `syncAggregate`.
 *   - `attested`   = `parent(sig_block)`. Provides `attestedHeader`
 *                    (light-client-header form) and the state root against
 *                    which both merkle proofs are verified.
 *   - `finalized`  = block returned by `state_id = "finalized"`. Provides
 *                    `finalizedHeader` (light-client-header form).
 *
 * Two Lodestar `CompactMultiProof` state proofs are issued against
 * `attested.state_root`:
 *   1. All 1026 chunks of `next_sync_committee` (gindices under 2946) +
 *      subroot capture at gindex 2946.
 *   2. Single-leaf branch for the `finalized_checkpoint.root` (gindex 735).
 *
 * All returned artifacts are cross-checked against `attested.state_root`:
 *   - `hash_tree_root(SyncCommittee) == captured_subroot`
 *   - `ssz_verify_single_merkle_proof(finality_branch, hash_tree_root(finalized_header.beacon), 735, attested.state_root)`
 *
 * Only supports the Gloas fork. All other forks fail with `C4_ERROR`.
 *
 * The build is skipped (with `C4_ERROR`) when `attested_slot >> 13 !=
 * target_period` -- i.e. the head has already advanced beyond the requested
 * period. This is expected to be handled by the caller as `skip-warn`.
 *
 * Async: may return `C4_PENDING` when beacon roundtrips are in flight.
 *
 * @param ctx prover context (Lodestar-capable beacon endpoint required).
 * @param target_period sync-committee period (`slot >> 13`) the caller expects.
 * @param out_lcu_ssz on `C4_SUCCESS`, populated with the heap-allocated
 *                    26424-byte SSZ bytes; caller must
 *                    `safe_free(out_lcu_ssz->data)`.
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`.
 */
c4_status_t c4_create_gloas_lcu(prover_ctx_t* ctx,
                                uint64_t      target_period,
                                bytes_t*      out_lcu_ssz);

/**
 * Assembles the SSZ bytes of a `GLOAS_LIGHT_CLIENT_UPDATE` container from
 * already-verified components. No allocations for verification, no network
 * calls -- just a fixed-size byte concatenation with strict size checks.
 *
 * All input buffers must have the expected fixed lengths, otherwise `false`
 * is returned and `*out_ssz` stays `NULL_BYTES`.
 *
 * Exposed in the public header so tests can drive the assembly without a
 * network round-trip; production callers use `c4_create_gloas_lcu`.
 *
 * @param attested_header GLOAS_LIGHT_CLIENT_HEADER raw SSZ (496 bytes)
 * @param next_sync_committee SYNC_COMMITTEE raw SSZ (24624 bytes)
 * @param next_sync_committee_branch 352 bytes (11 siblings)
 * @param finalized_header GLOAS_LIGHT_CLIENT_HEADER raw SSZ (496 bytes)
 * @param finality_branch 288 bytes (9 siblings)
 * @param sync_aggregate SYNC_AGGREGATE raw SSZ (160 bytes)
 * @param signature_slot slot of the sig_block; encoded as uint64 LE.
 * @param out_ssz heap-allocated 26424 bytes on success; caller frees.
 * @return `true` on success, `false` on any size mismatch.
 */
bool c4_gloas_lcu_assemble(bytes_t  attested_header,
                           bytes_t  next_sync_committee,
                           bytes_t  next_sync_committee_branch,
                           bytes_t  finalized_header,
                           bytes_t  finality_branch,
                           bytes_t  sync_aggregate,
                           uint64_t signature_slot,
                           bytes_t* out_ssz);

/**
 * Wraps a raw LCU-SSZ blob into the Beacon-API `light_client/updates`
 * on-wire format for a single entry:
 *
 * ```text
 * [8 bytes LE]  length = 4 + lcu_ssz.len
 * [4 bytes  ]   context (as passed by caller; per spec `ForkDigest`)
 * [lcu_ssz  ]   payload
 * ```
 *
 * Fails and returns `NULL_BYTES` when `lcu_ssz.len != C4_GLOAS_LCU_SSZ_SIZE`
 * or `lcu_ssz.data == NULL`.
 *
 * @param lcu_ssz the raw LCU-SSZ payload (borrowed, not consumed).
 * @param context the 4-byte context (typically a `ForkDigest`) written as-is.
 * @return heap-allocated `bytes_t` of length `12 + lcu_ssz.len` on success;
 *         `NULL_BYTES` on invalid input. Caller must `safe_free(.data)`.
 */
bytes_t c4_gloas_lcu_wrap_beacon_response(bytes_t lcu_ssz,
                                          uint8_t context[4]);

#ifdef __cplusplus
}
#endif

#endif
