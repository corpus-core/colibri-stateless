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

#ifndef C4_ETH_STATE_PROOFS_H
#define C4_ETH_STATE_PROOFS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "prover.h"
#include "ssz.h"

/**
 * Issues a Lodestar `CompactMultiProof` state-proof request against
 * `state_root` with the given caller-supplied `descriptor`, and returns the
 * SSZ objects of the response's `leaves` list and its echoed `descriptor`.
 *
 * The echoed descriptor is byte-checked against the request descriptor as a
 * defense-in-depth guard before returning; the reconstruction root check is
 * still the primary anchor of trust.
 *
 * Async: may return `C4_PENDING`; the caller must retry after fulfilling the
 * pending data request. Requests are routed to Lodestar
 * (`BEACON_CLIENT_LODESTAR`).
 *
 * `leaves_out` and `descriptor_out` point into the response cache and must
 * not be freed by the caller.
 *
 * @param ctx prover context
 * @param state_root the anchor beacon-state root
 * @param descriptor caller-computed compact-multi-proof descriptor bitlist
 * @param leaves_out on success, borrowed `leaves` ssz_ob_t
 * @param descriptor_out on success, borrowed `descriptor` ssz_ob_t
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
c4_status_t c4_state_proofs_beacon_fetch(prover_ctx_t* ctx,
                                         bytes32_t     state_root,
                                         bytes_t       descriptor,
                                         ssz_ob_t*     leaves_out,
                                         ssz_ob_t*     descriptor_out);

/**
 * Fetches a classical Merkle branch for a `BeaconState` field via Lodestar's
 * unofficial `/eth/v0/beacon/proof/state/{state_id}` endpoint.
 *
 * The endpoint returns a `CompactMultiProof {leaves, descriptor}` covering the
 * requested descriptor. This helper builds a single-gindex descriptor, verifies
 * that the reconstructed root equals `state_root`, and returns the siblings on
 * the path from the target leaf up to (but excluding) the root -- the classical
 * branch format expected by `ssz_verify_single_merkle_proof` and by the SSZ
 * `Vector[Bytes32, depth]` merkle branch fields in `LightClientBootstrap` /
 * `LightClientUpdate`.
 *
 * Async: may return `C4_PENDING`; the caller must retry after the request has
 * been fulfilled via `c4_req_set_response`. Requests are routed to Lodestar
 * (`BEACON_CLIENT_LODESTAR`).
 *
 * @param ctx prover context
 * @param state_root the beacon state root the proof is anchored against
 * @param gindex generalized index of the leaf inside `BeaconState` (must be >= 2)
 * @param proof_result output; `N * 32` bytes, leaf-to-root order. Heap-allocated on
 *                     `C4_SUCCESS`; caller must `safe_free(proof_result->data)`
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
c4_status_t c4_create_state_proof(prover_ctx_t* ctx,
                                  bytes32_t     state_root,
                                  gindex_t      gindex,
                                  bytes_t*      proof_result);

/**
 * Computes the ChainSafe compact-multi-proof descriptor for a set of
 * generalized indices. This descriptor is used as the `format` query parameter
 * of the Lodestar `/eth/v0/beacon/proof/state/{state_id}` endpoint.
 *
 * Reference implementation:
 * https://github.com/ChainSafe/ssz/blob/main/packages/persistent-merkle-tree/src/proof/compactMulti.ts
 *
 * @param indices generalized indices to include (each must be >= 1)
 * @param count number of indices
 * @return heap-allocated descriptor bytes; caller must `safe_free`.
 *         Returns `NULL_BYTES` on invalid input.
 */
bytes_t c4_ssz_compute_compact_descriptor(const gindex_t* indices, int count);

/**
 * Reconstructs the compact-multi-proof tree from `leaves` + `descriptor`,
 * verifies its root equals `expected_root`, and extracts the classical
 * leaf-to-root Merkle branch for `gindex`.
 *
 * Returns `false` and does not allocate `branch_out` if the descriptor is
 * malformed, the leaf count is inconsistent, `gindex` is not present in the
 * compact tree, or the reconstructed root does not match `expected_root`.
 *
 * `*branch_out` is always overwritten with `NULL_BYTES` before any work is
 * done. Callers must pass a fresh or previously freed pointer; otherwise the
 * prior allocation is leaked.
 *
 * @param leaves 32-byte-aligned buffer of tree leaves
 * @param descriptor compact-multi-proof descriptor bitlist
 * @param gindex generalized index to build the branch for (must be >= 2)
 * @param expected_root expected root of the reconstructed tree
 * @param branch_out on success, populated with heap-allocated branch (`N * 32`
 *                   bytes, leaf-to-root); caller must `safe_free(branch_out->data)`.
 *                   The value at gindex used when verifying is caller-supplied
 *                   -- for classical SSZ container proofs this is
 *                   `hash_tree_root(subtree)`, not `leaves[i]` from the
 *                   compact proof.
 * @return true on success, false on any validation error
 */
bool c4_ssz_compact_to_branch(bytes_t   leaves,
                              bytes_t   descriptor,
                              gindex_t  gindex,
                              bytes32_t expected_root,
                              bytes_t*  branch_out);

/**
 * Multi-gindex sibling of `c4_ssz_compact_to_branch`.
 *
 * Reconstructs the compact-multi-proof tree from `leaves` + `descriptor`,
 * verifies its root equals `expected_root`, and optionally does BOTH of:
 *   1. **Leaf extraction:** For each caller `gindices[i]`, copies the matching
 *      32-byte compact-tree leaf into `leaves_out.data + i*32` (caller order,
 *      independent of the internal sort). All caller gindices must correspond
 *      to actual leaves of the compact tree.
 *   2. **Subroot capture:** Computes the hash at `subroot_gindex` into
 *      `subroot_hash_out` (leaf value if the subroot is itself a leaf) and
 *      returns the classical leaf-to-root Merkle branch (`(depth) * 32` bytes)
 *      in `*subroot_branch_out`.
 *
 * `count == 0` disables leaf extraction; `subroot_gindex == 0` disables subroot
 * capture. Both may be disabled together to only verify the root.
 *
 * `*subroot_branch_out` (if non-NULL) is always overwritten with `NULL_BYTES`
 * before any work is done. Callers must pass a fresh or previously-freed
 * pointer; otherwise the prior allocation is leaked.
 *
 * Duplicate caller gindices are rejected so `leaves_out[i]` stays well-defined.
 *
 * @param leaves 32-byte-aligned buffer of compact-tree leaves
 * @param descriptor compact-multi-proof descriptor bitlist
 * @param gindices caller-supplied gindices to extract (each must be present as
 *                 a leaf in the compact tree). May be NULL when `count == 0`.
 * @param count number of caller gindices (must fit into `leaves_out.len / 32`)
 * @param expected_root expected root of the reconstructed tree
 * @param leaves_out caller-provided buffer of at least `count * 32` bytes;
 *                   filled in place with one 32-byte leaf per caller gindex.
 *                   May be `NULL_BYTES` when `count == 0`.
 * @param subroot_gindex generalized index whose hash + branch to root are
 *                       captured. `0` disables capture.
 * @param subroot_hash_out on success, filled with the hash at `subroot_gindex`
 *                         (or the leaf value if that gindex is a compact-tree
 *                         leaf). Untouched when `subroot_gindex == 0`.
 * @param subroot_branch_out on success (and when `subroot_gindex != 0`),
 *                           populated with a heap-allocated branch
 *                           (`(bitlen(subroot_gindex) - 1) * 32` bytes,
 *                           leaf-to-root). Caller must `safe_free`.
 * @return true on success, false on any validation error
 */
bool c4_ssz_compact_multi_extract(bytes_t         leaves,
                                  bytes_t         descriptor,
                                  const gindex_t* gindices,
                                  uint32_t        count,
                                  bytes32_t       expected_root,
                                  bytes_t         leaves_out,
                                  gindex_t        subroot_gindex,
                                  bytes32_t       subroot_hash_out,
                                  bytes_t*        subroot_branch_out);

#ifdef __cplusplus
}
#endif

#endif
