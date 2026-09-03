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

#include "bootstrap_gloas.h"
#include "beacon.h"
#include "beacon_types.h"
#include "eth_compute_units.h"
#include "state_proofs.h"
#include <stdlib.h> // free (used as cache_free_cb; safe_free is a macro)
#include <string.h>

// :: Sizing constants derived from the SSZ layout

// SyncCommittee layout: 512 pubkeys of 48 bytes + 1 aggregate pubkey of 48 bytes.
// Each 48-byte pubkey Merkleizes as 2 chunks (32 + 32, with the last 16 bytes
// zero-padded).
#define PUBKEY_BYTES               48u
#define PUBKEY_CHUNKS              2u  // 48 bytes -> 2 * 32-byte chunks
#define SYNC_COMMITTEE_PUBKEYS     512u
#define AGGREGATE_PUBKEY_CHUNKS    2u
#define TOTAL_SYNC_COMMITTEE_CHUNKS \
  (SYNC_COMMITTEE_PUBKEYS * PUBKEY_CHUNKS + AGGREGATE_PUBKEY_CHUNKS) // 1026

// Compile-time cross-check that the private chunk count matches the public
// header constant. `_Static_assert` at file scope is rejected by MSVC's C
// frontend (see `test_ssz_gloas.c:test_gloas_state_gindexes` for the same
// portability note), so we use the portable typedef-negative-size trick.
typedef char c4_gloas_bootstrap_chunk_count_check
    [(TOTAL_SYNC_COMMITTEE_CHUNKS == C4_GLOAS_BOOTSTRAP_SYNC_COMMITTEE_CHUNKS) ? 1 : -1];

// BeaconBlockHeader layout: slot(8) + proposerIndex(8) + parentRoot(32)
//                        + stateRoot(32) + bodyRoot(32) = 112 bytes.
#define BEACON_BLOCK_HEADER_BYTES  112u
#define STATE_ROOT_OFFSET          48u

// SyncCommittee subroot depth in Gloas BeaconState: gindex 2945 has bitlen 12,
// so the branch to state_root is 11 siblings.
#define GLOAS_STATE_ROOT_TO_SC_DEPTH 11u

// SyncCommittee-relative sub-gindices.
#define SC_SUB_GINDEX_PUBKEYS            2u
#define SC_SUB_GINDEX_AGGREGATE          3u
// Pubkeys is a Vector[Pubkey48, 512]; each pubkey_i sits at sub-gindex 512 + i.
#define PUBKEYS_SUB_GINDEX_ELEMENT_BASE  512u
// Each pubkey is a ByteVector[uint8, 48] with 2 chunks at sub-gindex 2, 3.
#define PUBKEY_SUB_GINDEX_CHUNK_BASE     2u

// :: Chunk reassembly

bool c4_gloas_bootstrap_chunks_to_sync_committee(bytes_t chunk_leaves, bytes_t sync_committee_out) {
  if (chunk_leaves.data == NULL || sync_committee_out.data == NULL) return false;
  if (chunk_leaves.len != TOTAL_SYNC_COMMITTEE_CHUNKS * 32u) return false;
  if (sync_committee_out.len != C4_GLOAS_BOOTSTRAP_SYNC_COMMITTEE_SIZE) return false;

  // Each 48-byte pubkey is stored across 2 chunks:
  //   chunk0 = bytes[0..32]
  //   chunk1 = bytes[32..48] || 16 zero-bytes  (SSZ ByteVector[48] padding)
  // Enforce the zero padding on chunk 1 -- a well-formed BeaconState chunk
  // returned by Lodestar always has these 16 bytes zeroed, and a non-canonical
  // padding indicates a corrupted or malicious response.
  //
  // Two-pass design: validate all 1026 chunk paddings before writing anything
  // so `sync_committee_out` stays untouched on any padding failure (the API
  // contract for failure paths).
  const uint8_t* chunks = chunk_leaves.data;

  // Pass 1: padding validation for all pubkeys + aggregate.
  for (uint32_t i = 0; i < SYNC_COMMITTEE_PUBKEYS; i++) {
    const uint8_t* chunk1 = chunks + (2u * i + 1u) * 32u;
    for (uint32_t p = 16; p < 32; p++) {
      if (chunk1[p] != 0) return false;
    }
  }
  {
    const uint8_t* agg_chunk1 = chunks + 1025u * 32u;
    for (uint32_t p = 16; p < 32; p++) {
      if (agg_chunk1[p] != 0) return false;
    }
  }

  // Pass 2: write pubkey bytes.
  uint8_t* sc_data = sync_committee_out.data;
  for (uint32_t i = 0; i < SYNC_COMMITTEE_PUBKEYS; i++) {
    const uint8_t* chunk0 = chunks + (2u * i) * 32u;
    const uint8_t* chunk1 = chunks + (2u * i + 1u) * 32u;
    memcpy(sc_data + i * PUBKEY_BYTES, chunk0, 32);
    memcpy(sc_data + i * PUBKEY_BYTES + 32, chunk1, 16);
  }
  memcpy(sc_data + SYNC_COMMITTEE_PUBKEYS * PUBKEY_BYTES,
         chunks + 1024u * 32u, 32);
  memcpy(sc_data + SYNC_COMMITTEE_PUBKEYS * PUBKEY_BYTES + 32,
         chunks + 1025u * 32u, 16);

  return true;
}

// :: Fixed-size SSZ assembly

bool c4_gloas_bootstrap_assemble(bytes_t  cl_header,
                                 bytes_t  execution_block_hash,
                                 bytes_t  execution_branch,
                                 bytes_t  sync_committee_bytes,
                                 bytes_t  sync_committee_branch,
                                 bytes_t* out_bytes) {
  if (!out_bytes) return false;
  *out_bytes = NULL_BYTES;
  if (cl_header.data == NULL || cl_header.len != BEACON_BLOCK_HEADER_BYTES) return false;
  if (execution_block_hash.data == NULL || execution_block_hash.len != 32) return false;
  if (execution_branch.data == NULL ||
      execution_branch.len != C4_GLOAS_BOOTSTRAP_BRANCH_SIZE) return false;
  if (sync_committee_bytes.data == NULL ||
      sync_committee_bytes.len != C4_GLOAS_BOOTSTRAP_SYNC_COMMITTEE_SIZE) return false;
  if (sync_committee_branch.data == NULL ||
      sync_committee_branch.len != C4_GLOAS_BOOTSTRAP_BRANCH_SIZE) return false;

  uint8_t* buf = (uint8_t*) safe_malloc(C4_GLOAS_BOOTSTRAP_SIZE);

  // Layout is entirely fixed-size, no SSZ offsets required:
  //   [0 .. 112)         BeaconBlockHeader
  //   [112 .. 144)       executionBlockHash
  //   [144 .. 496)       executionBranch (11 * 32)
  //   [496 .. 25120)     SyncCommittee
  //   [25120 .. 25472)   currentSyncCommitteeBranch (11 * 32)
  uint32_t off = 0;
  memcpy(buf + off, cl_header.data, BEACON_BLOCK_HEADER_BYTES);
  off += BEACON_BLOCK_HEADER_BYTES;
  memcpy(buf + off, execution_block_hash.data, 32);
  off += 32;
  memcpy(buf + off, execution_branch.data, C4_GLOAS_BOOTSTRAP_BRANCH_SIZE);
  off += C4_GLOAS_BOOTSTRAP_BRANCH_SIZE;
  memcpy(buf + off, sync_committee_bytes.data, C4_GLOAS_BOOTSTRAP_SYNC_COMMITTEE_SIZE);
  off += C4_GLOAS_BOOTSTRAP_SYNC_COMMITTEE_SIZE;
  memcpy(buf + off, sync_committee_branch.data, C4_GLOAS_BOOTSTRAP_BRANCH_SIZE);
  off += C4_GLOAS_BOOTSTRAP_BRANCH_SIZE;

  if (off != C4_GLOAS_BOOTSTRAP_SIZE) {
    // impossible unless the constants drift; fail safe.
    safe_free(buf);
    return false;
  }

  *out_bytes = bytes(buf, C4_GLOAS_BOOTSTRAP_SIZE);
  return true;
}

// :: Gindex table for the 1026 SyncCommittee chunks

// Builds the caller-order gindex list required by c4_ssz_compact_multi_extract.
// Order (must stay in lockstep with c4_gloas_bootstrap_chunks_to_sync_committee):
//   [0 .. 1024)   pubkey_i chunk_k = pubkey_0_c0, pubkey_0_c1, ..., pubkey_511_c1
//   [1024]        aggregate chunk 0
//   [1025]        aggregate chunk 1
//
// Returns false if any composed gindex overflows (which cannot happen with the
// Gloas BeaconState layout, but the check guards future forks that push
// current_sync_committee to a deeper gindex).
static bool build_sync_committee_gindices(gindex_t sync_root_gindex,
                                          gindex_t out_gindices[TOTAL_SYNC_COMMITTEE_CHUNKS]) {
  // Sub-gindex within pubkey_i for chunk k (k in {0,1}).
  // ssz_add_gindex(512 + i, PUBKEY_SUB_GINDEX_CHUNK_BASE + k) works because
  // PUBKEY_SUB_GINDEX_CHUNK_BASE + k in {2, 3} both have bitlen 2.
  for (uint32_t i = 0; i < SYNC_COMMITTEE_PUBKEYS; i++) {
    gindex_t elem_g = PUBKEYS_SUB_GINDEX_ELEMENT_BASE + i;
    for (uint32_t k = 0; k < PUBKEY_CHUNKS; k++) {
      gindex_t chunk_in_pubkey = elem_g * 2u + k; // == ssz_add_gindex(elem_g, 2+k)
      // pubkey chunks live under SyncCommittee.pubkeys (sub-gindex 2)
      gindex_t chunk_in_pubkeys_vec = ssz_add_gindex(SC_SUB_GINDEX_PUBKEYS, chunk_in_pubkey);
      if (chunk_in_pubkeys_vec == 0) return false;
      gindex_t abs = ssz_add_gindex(sync_root_gindex, chunk_in_pubkeys_vec);
      if (abs == 0) return false;
      out_gindices[i * PUBKEY_CHUNKS + k] = abs;
    }
  }

  // Aggregate pubkey chunks live under SyncCommittee.aggregatePubkey (sub-gindex 3)
  for (uint32_t k = 0; k < AGGREGATE_PUBKEY_CHUNKS; k++) {
    gindex_t chunk_in_agg = ssz_add_gindex(SC_SUB_GINDEX_AGGREGATE, PUBKEY_SUB_GINDEX_CHUNK_BASE + k);
    if (chunk_in_agg == 0) return false;
    gindex_t abs = ssz_add_gindex(sync_root_gindex, chunk_in_agg);
    if (abs == 0) return false;
    out_gindices[SYNC_COMMITTEE_PUBKEYS * PUBKEY_CHUNKS + k] = abs;
  }

  return true;
}

// :: Shared bootstrap-build core (block -> bootstrap SSZ)

// Given a fully filled `eth_block_t` for a Gloas-era block, run the state-proof
// round-trip + assembly and emit the SSZ bootstrap bytes.
//
// This is the shared code between the state-id and block-root entry points.
// It never fetches the block itself; both public wrappers do that up-front.
//
// Contract:
//   - Caller ensures `block->proof_type == C4_BLOCK_PROOF_TYPE_BEACON` and
//     `block->beacon.cl_header` / `block->beacon.block_hash_branch` /
//     `block->el_block_hash` are populated.
//   - On success `*out_bootstrap_ssz` owns a heap-allocated 25472-byte buffer.
//   - May return C4_PENDING (state-proof round-trip).
static c4_status_t build_gloas_bootstrap_from_block(prover_ctx_t* ctx,
                                                    eth_block_t*  block,
                                                    bytes_t*      out_bootstrap_ssz) {
  if (block->proof_type != C4_BLOCK_PROOF_TYPE_BEACON)
    THROW_ERROR("c4_create_gloas_bootstrap: requires beacon-proof block data");

  // Fork gate: only Gloas is supported by this builder. Explicit identity
  // check (not `>= C4_FORK_GLOAS`) so a future post-Gloas fork with a
  // different SyncCommittee or LightClientBootstrap layout does not silently
  // opt into this code path. Post-Gloas forks must add an explicit builder.
  const chain_spec_t* chain = c4_eth_get_chain_spec(ctx->chain_id);
  if (!chain) THROW_ERROR("c4_create_gloas_bootstrap: unknown chain");
  uint64_t  epoch = epoch_for_slot(block->slot, chain);
  fork_id_t fork  = c4_chain_fork_id(ctx->chain_id, epoch);
  if (fork != C4_FORK_GLOAS)
    THROW_ERROR("c4_create_gloas_bootstrap: only Gloas fork is supported");

  // The Gloas LightClientHeader carries `executionBlockHash` + a depth-11
  // branch proving it against `body_root`. `c4_beacon_get_block_for_eth`
  // already produces the block-hash branch at that gindex (2856), so its
  // length must exactly match the Gloas expectation. A mismatch means the
  // block fetch resolved a different fork than the epoch lookup above.
  if (block->beacon.block_hash_branch.len != C4_GLOAS_BOOTSTRAP_BRANCH_SIZE)
    THROW_ERROR("c4_create_gloas_bootstrap: unexpected executionBranch depth (not Gloas)");
  if (block->beacon.cl_header.bytes.len != BEACON_BLOCK_HEADER_BYTES)
    THROW_ERROR("c4_create_gloas_bootstrap: unexpected BeaconBlockHeader size");

  bytes32_t state_root = {0};
  memcpy(state_root, block->beacon.cl_header.bytes.data + STATE_ROOT_OFFSET, 32);

  // ---------------------------------------------------------------------------
  // 2. Compute the 1026 SyncCommittee-chunk gindices + the descriptor.
  // ---------------------------------------------------------------------------
  gindex_t sync_root_gindex = c4_current_sync_committee_gindex(ctx->chain_id, block->slot);
  if (sync_root_gindex == 0)
    THROW_ERROR("c4_create_gloas_bootstrap: no current_sync_committee gindex for slot");

  // Also confirm the branch depth matches the actual gindex depth. bitlen-1
  // is the classical Merkle branch depth.
  {
    uint8_t bl = 0;
    for (gindex_t g = sync_root_gindex; g; g >>= 1) bl++;
    if ((uint8_t) (bl - 1) != GLOAS_STATE_ROOT_TO_SC_DEPTH)
      THROW_ERROR("c4_create_gloas_bootstrap: unexpected sync-committee gindex depth");
  }

  gindex_t gindices[TOTAL_SYNC_COMMITTEE_CHUNKS] = {0};
  if (!build_sync_committee_gindices(sync_root_gindex, gindices))
    THROW_ERROR("c4_create_gloas_bootstrap: gindex composition overflow");

  bytes_t descriptor = c4_ssz_compute_compact_descriptor(gindices, TOTAL_SYNC_COMMITTEE_CHUNKS);
  if (descriptor.len == 0)
    THROW_ERROR("c4_create_gloas_bootstrap: failed to build compact descriptor");

  // ---------------------------------------------------------------------------
  // 3. Fetch the CompactMultiProof state proof (async; may return C4_PENDING).
  // ---------------------------------------------------------------------------
  ssz_ob_t    leaves_ob     = {0};
  ssz_ob_t    descriptor_ob = {0};
  c4_status_t status        = c4_state_proofs_beacon_fetch(ctx, state_root, descriptor,
                                                           &leaves_ob, &descriptor_ob);
  safe_free(descriptor.data);
  descriptor.data = NULL;
  descriptor.len  = 0;
  if (status != C4_SUCCESS) return status;

  // ---------------------------------------------------------------------------
  // 4. Extract the 1026 chunk leaves + the subroot hash + branch to state_root.
  // ---------------------------------------------------------------------------
  uint8_t   chunk_leaves[TOTAL_SYNC_COMMITTEE_CHUNKS * 32u] = {0};
  bytes32_t sync_committee_root                             = {0};
  bytes_t   sync_committee_branch                           = NULL_BYTES;

  bool ok = c4_ssz_compact_multi_extract(
      leaves_ob.bytes, descriptor_ob.bytes,
      gindices, TOTAL_SYNC_COMMITTEE_CHUNKS, state_root,
      bytes(chunk_leaves, sizeof(chunk_leaves)),
      sync_root_gindex, sync_committee_root, &sync_committee_branch);
  if (!ok)
    THROW_ERROR("c4_create_gloas_bootstrap: state-proof reconstruction failed");

  if (sync_committee_branch.len != C4_GLOAS_BOOTSTRAP_BRANCH_SIZE) {
    safe_free(sync_committee_branch.data);
    THROW_ERROR("c4_create_gloas_bootstrap: unexpected sync-committee branch length");
  }

  // ---------------------------------------------------------------------------
  // 5. Reassemble SyncCommittee raw bytes from the chunk leaves.
  // ---------------------------------------------------------------------------
  uint8_t sync_committee_bytes[C4_GLOAS_BOOTSTRAP_SYNC_COMMITTEE_SIZE] = {0};
  if (!c4_gloas_bootstrap_chunks_to_sync_committee(
          bytes(chunk_leaves, sizeof(chunk_leaves)),
          bytes(sync_committee_bytes, sizeof(sync_committee_bytes)))) {
    safe_free(sync_committee_branch.data);
    THROW_ERROR("c4_create_gloas_bootstrap: non-canonical SSZ pubkey chunk padding");
  }

  // ---------------------------------------------------------------------------
  // 6. Cross-check that hash_tree_root(SyncCommittee) matches the captured
  //    subroot hash. The primary anchor (root check inside multi_extract)
  //    already guarantees correctness; this second check localizes any
  //    chunk-reassembly bug to a clear error message.
  // ---------------------------------------------------------------------------
  static const ssz_def_t SYNC_COMMITTEE_CONTAINER =
      SSZ_CONTAINER("SyncCommittee", SYNC_COMMITTEE);
  ssz_ob_t sc_ob = {
      .def   = &SYNC_COMMITTEE_CONTAINER,
      .bytes = bytes(sync_committee_bytes, sizeof(sync_committee_bytes)),
  };
  bytes32_t computed_root = {0};
  ssz_hash_tree_root(sc_ob, computed_root);
  if (memcmp(computed_root, sync_committee_root, 32) != 0) {
    safe_free(sync_committee_branch.data);
    THROW_ERROR("c4_create_gloas_bootstrap: SyncCommittee root mismatch after reassembly");
  }

  // ---------------------------------------------------------------------------
  // 7. Assemble the final SSZ bytes.
  // ---------------------------------------------------------------------------
  bytes_t bootstrap = NULL_BYTES;
  if (!c4_gloas_bootstrap_assemble(
          block->beacon.cl_header.bytes,
          bytes(block->el_block_hash, 32),
          block->beacon.block_hash_branch,
          bytes(sync_committee_bytes, sizeof(sync_committee_bytes)),
          sync_committee_branch,
          &bootstrap)) {
    safe_free(sync_committee_branch.data);
    THROW_ERROR("c4_create_gloas_bootstrap: SSZ assembly failed");
  }

  safe_free(sync_committee_branch.data);

  // Account for the reconstruction cost. The per-request CU was already
  // charged by c4_state_proofs_beacon_fetch -> c4_send_beacon_ssz_*.
  eth_cu_add_proof(ctx);

  *out_bootstrap_ssz = bootstrap;
  return C4_SUCCESS;
}

// :: Public API (async)

c4_status_t c4_create_gloas_bootstrap(prover_ctx_t* ctx,
                                      json_t        state_id_json,
                                      bytes_t*      out_bootstrap_ssz) {
  if (!ctx || !out_bootstrap_ssz) return C4_ERROR;
  *out_bootstrap_ssz = NULL_BYTES;

  eth_block_t block = {0};
  TRY_ASYNC(c4_beacon_get_block_for_eth(ctx, state_id_json, &block));

  return build_gloas_bootstrap_from_block(ctx, &block, out_bootstrap_ssz);
}

// Fetch a SignedBeaconBlock by its beacon-block root as SSZ, verify that its
// root matches, and populate an `eth_block_t` via the standard fill helper.
//
// The bootstrap only needs `cl_header`, `block_hash_branch` and
// `el_block_hash`, so we deliberately pass the same block as both `data_block`
// and `sig_block`: `sync_aggregate` and `sign_parent_root` are not used
// downstream, and this saves a second round-trip.
static c4_status_t fetch_block_by_root_for_bootstrap(prover_ctx_t* ctx,
                                                     bytes32_t     header_root,
                                                     eth_block_t*  block_out) {
  ssz_ob_t signed_block   = {0};
  char     path[128]      = {0};
  buffer_t path_buf       = stack_buffer(path);
  bprintf(&path_buf, "eth/v2/beacon/blocks/0x%x", bytes(header_root, 32));

  TRY_ASYNC(c4_send_beacon_ssz(ctx, path, NULL, NULL, DEFAULT_TTL, &signed_block));

  // The SSZ envelope arrived without a definition attached; determine the fork
  // from the on-wire slot and validate the SSZ layout before we descend into
  // container access.
  if (signed_block.bytes.len < 108)
    THROW_ERROR("c4_create_gloas_bootstrap_by_root: signed block too short");
  const chain_spec_t* chain = c4_eth_get_chain_spec(ctx->chain_id);
  if (!chain) THROW_ERROR("c4_create_gloas_bootstrap_by_root: unknown chain");
  uint64_t block_slot_offset = uint32_from_le(signed_block.bytes.data);
  if (block_slot_offset > signed_block.bytes.len - 8)
    THROW_ERROR("c4_create_gloas_bootstrap_by_root: invalid signed block layout");
  uint64_t  slot = uint64_from_le(signed_block.bytes.data + block_slot_offset);
  fork_id_t fork = c4_chain_fork_id(ctx->chain_id, epoch_for_slot(slot, chain));
  if (fork != C4_FORK_GLOAS)
    THROW_ERROR("c4_create_gloas_bootstrap_by_root: block root does not resolve to a Gloas block");

  signed_block.def = eth_ssz_type_for_fork(ETH_SSZ_SIGNED_BEACON_BLOCK_CONTAINER, fork, ctx->chain_id);
  if (!signed_block.def)
    THROW_ERROR("c4_create_gloas_bootstrap_by_root: no SSZ def for signed block");
  if (!ssz_is_valid(signed_block, true, &ctx->state))
    THROW_ERROR("c4_create_gloas_bootstrap_by_root: invalid signed block SSZ");

  ssz_ob_t data_block = ssz_get(&signed_block, "message");
  if (!data_block.bytes.data)
    THROW_ERROR("c4_create_gloas_bootstrap_by_root: message field missing");

  // Anchor the returned block to the caller-provided header_root: if
  // hash_tree_root(header) differs, we would silently build a bootstrap for
  // the wrong block. This is the only spot in the by-root path that ties the
  // final bootstrap header to the caller's `header_root`.
  bytes32_t computed_root = {0};
  {
    ssz_ob_t body      = ssz_get(&data_block, "body");
    bytes32_t body_root = {0};
    ssz_hash_tree_root(body, body_root);
    uint8_t header_data[112] = {0};
    memcpy(header_data, data_block.bytes.data, 112 - 32);
    memcpy(header_data + 112 - 32, body_root, 32);
    ssz_ob_t hdr = {
        .def   = eth_ssz_type_for_denep(ETH_SSZ_BEACON_BLOCK_HEADER, ctx->chain_id),
        .bytes = bytes(header_data, 112),
    };
    ssz_hash_tree_root(hdr, computed_root);
  }
  if (memcmp(computed_root, header_root, 32) != 0)
    THROW_ERROR("c4_create_gloas_bootstrap_by_root: fetched block does not match header_root");

  // Reuse the standard fill helper with sig_block == data_block: the extra
  // fields it copies (`sync_aggregate`, `sign_parent_root`) are unused by the
  // bootstrap builder, and passing the same block avoids a second round-trip.
  bytes32_t data_root_copy = {0};
  memcpy(data_root_copy, header_root, 32);
  return c4_beacon_fill_becaon_block_from_eth(ctx, block_out, data_root_copy, data_block, data_block);
}

c4_status_t c4_create_gloas_bootstrap_by_root(prover_ctx_t* ctx,
                                              bytes32_t     header_root,
                                              bytes_t*      out_bootstrap_ssz) {
  if (!ctx || !out_bootstrap_ssz) return C4_ERROR;
  *out_bootstrap_ssz = NULL_BYTES;

  eth_block_t block = {0};
  TRY_ASYNC(fetch_block_by_root_for_bootstrap(ctx, header_root, &block));

  return build_gloas_bootstrap_from_block(ctx, &block, out_bootstrap_ssz);
}

// :: Server-side precompute + cache

void c4_gloas_bootstrap_cache_key(bytes32_t block_root, bytes32_t out_key) {
  // 4-byte prefix + 28 bytes of block_root. Same shape as the `ELH_` cache
  // key in beacon.c; the prefix makes the namespace visually distinct
  // from other 32-byte-key entries but does not by itself guarantee
  // uniqueness against a caller that reuses the same prefix.
  memcpy(out_key, "BSTR", 4);
  memcpy(out_key + 4, block_root + 4, 28);
}

// Derives the beacon-block root from a Gloas bootstrap SSZ blob by
// hash-tree-rooting the first 112 bytes (the embedded BeaconBlockHeader).
static void gloas_bootstrap_derive_block_root(prover_ctx_t* ctx,
                                              bytes_t       bootstrap_ssz,
                                              bytes32_t     out_root) {
  ssz_ob_t header = {
      .def   = eth_ssz_type_for_denep(ETH_SSZ_BEACON_BLOCK_HEADER, ctx->chain_id),
      .bytes = bytes(bootstrap_ssz.data, BEACON_BLOCK_HEADER_BYTES),
  };
  ssz_hash_tree_root(header, out_root);
}

c4_status_t c4_precompute_finalized_gloas_bootstrap(prover_ctx_t* ctx,
                                                    bytes32_t     expected_block_root) {
  if (!ctx) return C4_ERROR;

#ifndef PROVER_CACHE
  // Precomputing without a global cache to store the result would be a leak.
  // Fail loudly so the caller knows the feature is unavailable.
  (void) expected_block_root;
  THROW_ERROR("c4_precompute_finalized_gloas_bootstrap: PROVER_CACHE is disabled");
#else
  bytes_t bootstrap = NULL_BYTES;
  // Use state_id = "finalized" -- Lodestar serves this without triggering
  // a state regen (unlike a raw state root), see Lodestar issue #7780 and
  // PR #9641 (head/finalized/justified/genesis are exempt from the sync
  // guard). This is the whole point of the precompute path.
  TRY_ASYNC(c4_create_gloas_bootstrap(ctx, json_parse("\"finalized\""), &bootstrap));

  // Anchor to the block root that the beacon event told us about, so a
  // race between "finalized" state resolution and the SSE event -- where
  // Lodestar could return a slightly newer finalized block than the one
  // we announced -- is rejected instead of silently caching under the
  // wrong key.
  bytes32_t derived_root = {0};
  gloas_bootstrap_derive_block_root(ctx, bootstrap, derived_root);

  bool have_anchor = !bytes_all_zero(bytes(expected_block_root, 32));
  if (have_anchor && memcmp(derived_root, expected_block_root, 32) != 0) {
    safe_free(bootstrap.data);
    THROW_ERROR("c4_precompute_finalized_gloas_bootstrap: derived block root differs from event anchor");
  }

  bytes32_t cache_key = {0};
  c4_gloas_bootstrap_cache_key(derived_root, cache_key);

  // TTL = 5 epochs (~32 min). Comfortably covers the ~6.4 min per-epoch
  // finalization cadence so the next precompute refreshes the entry
  // before this one expires, even if a single finalization event is
  // missed. `c4_prover_cache_set` takes ownership of `bootstrap.data`;
  // `safe_free` is a macro, so we hand the raw `free` symbol to the cache.
  const uint64_t five_epochs_ms = 5ULL * 32ULL * 12ULL * 1000ULL;
  c4_prover_cache_set(ctx, cache_key, bootstrap.data, bootstrap.len,
                      five_epochs_ms, free);
  return C4_SUCCESS;
#endif
}
