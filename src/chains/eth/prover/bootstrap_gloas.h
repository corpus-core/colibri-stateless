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

#ifndef C4_ETH_BOOTSTRAP_GLOAS_H
#define C4_ETH_BOOTSTRAP_GLOAS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "json.h"
#include "prover.h"

// Size constants for the Gloas LightClientBootstrap layout.
//   - BeaconBlockHeader:                                             112 bytes
//   - GloasLightClientHeader = BeaconBlockHeader (112)
//                            + executionBlockHash (32)
//                            + executionBranch [Vector[bytes32, 11]] (352) = 496
//   - SyncCommittee          = pubkeys [Vector[Pubkey48, 512]] (24576)
//                            + aggregatePubkey [ByteVector[48]] (48)     = 24624
//   - currentSyncCommitteeBranch [Vector[bytes32, 11]]                     352
//   - GloasLightClientBootstrap                                          25472
#define C4_GLOAS_BOOTSTRAP_SIZE            25472u
#define C4_GLOAS_BOOTSTRAP_HEADER_SIZE     496u
#define C4_GLOAS_BOOTSTRAP_SYNC_COMMITTEE_SIZE 24624u
#define C4_GLOAS_BOOTSTRAP_BRANCH_DEPTH    11u
#define C4_GLOAS_BOOTSTRAP_BRANCH_SIZE     (C4_GLOAS_BOOTSTRAP_BRANCH_DEPTH * 32u)

// A SyncCommittee has 512 pubkeys and one aggregate pubkey. Each 48-byte pubkey
// is Merkleized as 2 chunks (32 + padding to 32), so the SyncCommittee holds
// 512 * 2 + 2 = 1026 leaf chunks.
#define C4_GLOAS_BOOTSTRAP_SYNC_COMMITTEE_CHUNKS 1026u

/**
 * Builds a complete Gloas `LightClientBootstrap` SSZ blob for the given
 * beacon-block identifier (typically `"finalized"`).
 *
 * The bootstrap is assembled from three independently fetched inputs:
 *   1. Beacon block + `executionBranch`   -- via the existing
 *      `c4_beacon_get_block_for_eth` (`eth_block_t.beacon.cl_header` and
 *      `block_hash_branch`).
 *   2. All 1026 `SyncCommittee` chunks    -- via a single Lodestar
 *      `CompactMultiProof` state-proof request against
 *      `header.beacon.stateRoot`.
 *   3. The `currentSyncCommitteeBranch`   -- captured as the subroot branch of
 *      that same state proof (at Gloas gindex 2945).
 *
 * All three pieces are cross-validated against `stateRoot` before being
 * serialized into the fixed-size SSZ container.
 *
 * Only supports the Gloas fork: if the resolved slot maps to any other fork
 * the call fails with `C4_ERROR`.
 *
 * Async: may return `C4_PENDING`; the caller must retry after fulfilling the
 * pending data requests via `c4_req_set_response`.
 *
 * @param ctx prover context (must have a Lodestar-capable beacon endpoint)
 * @param state_id_json JSON identifier for the block (e.g.
 *                      `json_parse("\"finalized\"")` or a hex block hash).
 *                      Same accepted formats as `c4_beacon_get_block_for_eth`.
 * @param out_bootstrap_ssz on `C4_SUCCESS`, populated with the heap-allocated
 *                          25472-byte SSZ bytes; caller must `safe_free(out_bootstrap_ssz->data)`.
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`.
 */
c4_status_t c4_create_gloas_bootstrap(prover_ctx_t* ctx,
                                      json_t        state_id_json,
                                      bytes_t*      out_bootstrap_ssz);

/**
 * Same as `c4_create_gloas_bootstrap` but binds the produced bootstrap to an
 * explicit beacon-block root instead of a state identifier.
 *
 * Intended as a fallback for `fetch_bootstrap_by_root` in `historic_proof.c`
 * when the beacon node cannot serve the standard
 * `eth/v1/beacon/light_client/bootstrap/{block_root}` endpoint. The block is
 * fetched via `eth/v2/beacon/blocks/{block_root}` and then handed to the same
 * proof/assembly pipeline as `c4_create_gloas_bootstrap`.
 *
 * A hash-tree-root check ties the returned bootstrap header back to the
 * caller-provided root, so a substituted block is rejected before the
 * bootstrap is assembled.
 *
 * Only supports the Gloas fork.
 *
 * @param ctx prover context
 * @param header_root hash_tree_root of the target `BeaconBlockHeader`
 * @param out_bootstrap_ssz on `C4_SUCCESS`, populated with the heap-allocated
 *                          25472-byte SSZ bytes; caller must `safe_free`.
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`.
 */
c4_status_t c4_create_gloas_bootstrap_by_root(prover_ctx_t* ctx,
                                              bytes32_t     header_root,
                                              bytes_t*      out_bootstrap_ssz);

/**
 * Assembles the SSZ bytes of a `GLOAS_LIGHT_CLIENT_BOOTSTRAP` container from
 * its already-verified components. No allocations or network calls -- just a
 * fixed-size byte concatenation.
 *
 * All input buffers must have the expected fixed lengths, otherwise `false`
 * is returned and `*out_bytes` stays `NULL_BYTES`.
 *
 * Exposed in the public header so tests can drive the assembly without a
 * network round-trip; production callers use `c4_create_gloas_bootstrap`.
 *
 * @param cl_header BeaconBlockHeader raw SSZ (112 bytes)
 * @param execution_block_hash 32 bytes
 * @param execution_branch 352 bytes (11 siblings)
 * @param sync_committee_bytes 24624 bytes (SYNC_COMMITTEE SSZ)
 * @param sync_committee_branch 352 bytes (11 siblings)
 * @param out_bytes heap-allocated 25472 bytes on success; caller frees.
 * @return true on success, false on any size mismatch
 */
bool c4_gloas_bootstrap_assemble(bytes_t  cl_header,
                                 bytes_t  execution_block_hash,
                                 bytes_t  execution_branch,
                                 bytes_t  sync_committee_bytes,
                                 bytes_t  sync_committee_branch,
                                 bytes_t* out_bytes);

/**
 * Reassembles the 24624 raw SSZ bytes of the `SyncCommittee` container from
 * the 1026 chunk leaves returned by the state proof. Also enforces the SSZ
 * 16-byte zero padding at the tail of chunk 1 of every 48-byte pubkey (a
 * non-canonical padding indicates that the returned chunk is not actually a
 * `ByteVector[uint8, 48]` chunk of the SyncCommittee).
 *
 * @param chunk_leaves 1026 chunks * 32 bytes = 32832 bytes; ordered as
 *                     `pubkey_0_c0, pubkey_0_c1, ..., pubkey_511_c1,
 *                      agg_c0, agg_c1`.
 * @param sync_committee_out caller-provided 24624-byte buffer, filled on success.
 * @return true on success, false if any padding byte is non-zero or the input
 *         is malformed.
 */
bool c4_gloas_bootstrap_chunks_to_sync_committee(bytes_t chunk_leaves,
                                                 bytes_t sync_committee_out);

#ifdef __cplusplus
}
#endif

#endif
