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

#include "lcu_gloas.h"
#include "beacon.h"
#include "beacon_types.h"
#include "bootstrap_gloas.h"
#include "eth_compute_units.h"
#include "state_proofs.h"
#include <string.h>

// :: Sizing constants derived from the SSZ layout

// SyncCommittee layout: 512 pubkeys of 48 bytes + 1 aggregate pubkey of 48 bytes.
// Each 48-byte pubkey Merkleizes as 2 chunks (32 + 32, with the last 16 bytes
// zero-padded).
#define PUBKEY_CHUNKS           2u
#define SYNC_COMMITTEE_PUBKEYS  512u
#define AGGREGATE_PUBKEY_CHUNKS 2u
#define TOTAL_SYNC_COMMITTEE_CHUNKS \
  (SYNC_COMMITTEE_PUBKEYS * PUBKEY_CHUNKS + AGGREGATE_PUBKEY_CHUNKS) // 1026

// Compile-time layout guards. `_Static_assert` at file scope is rejected by
// MSVC's C frontend, so we use the portable typedef-negative-size trick.
// A false condition yields an "array with negative size" error at compile
// time on all supported compilers (Clang / GCC / MSVC).
typedef char c4_gloas_lcu_chunk_count_check
    [(TOTAL_SYNC_COMMITTEE_CHUNKS == 1026u) ? 1 : -1];

// Cross-check: the fixed-size LCU total is the sum of its parts. Catches
// a constant drift before it turns into a run-time surprise (see the
// defensive `off != C4_GLOAS_LCU_SSZ_SIZE` in c4_gloas_lcu_assemble).
typedef char c4_gloas_lcu_layout_total_check
    [(C4_GLOAS_LCU_HEADER_SIZE + C4_GLOAS_LCU_SYNC_COMMITTEE_SIZE + C4_GLOAS_LCU_NEXT_SC_BRANCH_SIZE + C4_GLOAS_LCU_HEADER_SIZE + C4_GLOAS_LCU_FINALITY_BRANCH_SIZE + C4_GLOAS_LCU_SYNC_AGGREGATE_SIZE + 8u == C4_GLOAS_LCU_SSZ_SIZE)
         ? 1
         : -1];

// BeaconBlockHeader = slot(8)+proposerIndex(8)+parentRoot(32)+stateRoot(32)+bodyRoot(32) = 112.
#define BEACON_BLOCK_HEADER_BYTES 112u
#define STATE_ROOT_OFFSET         48u

// Sync-committee-relative sub-gindices used for the compact-multi-proof
// descriptor. Kept in sync with `bootstrap_gloas.c` so both call sites hit
// the same 1026 leaves.
#define SC_SUB_GINDEX_PUBKEYS           2u
#define SC_SUB_GINDEX_AGGREGATE         3u
#define PUBKEYS_SUB_GINDEX_ELEMENT_BASE 512u
#define PUBKEY_SUB_GINDEX_CHUNK_BASE    2u

// Merkle branch depth from state_root to SyncCommittee root (Gloas gindex 2946
// has bitlen 12 -> 11 siblings). Same depth for both current & next SC.
#define GLOAS_STATE_ROOT_TO_SC_DEPTH 11u

// Merkle branch depth from state_root to finalized_checkpoint.root
// (Gloas gindex 735 has bitlen 10 -> 9 siblings).
#define GLOAS_STATE_ROOT_TO_FINALIZED_DEPTH 9u

// :: Assembler + Beacon-API wrapper

bool c4_gloas_lcu_assemble(bytes_t  attested_header,
                           bytes_t  next_sync_committee,
                           bytes_t  next_sync_committee_branch,
                           bytes_t  finalized_header,
                           bytes_t  finality_branch,
                           bytes_t  sync_aggregate,
                           uint64_t signature_slot,
                           bytes_t* out_ssz) {
  if (!out_ssz) return false;
  *out_ssz = NULL_BYTES;
  if (attested_header.data == NULL ||
      attested_header.len != C4_GLOAS_LCU_HEADER_SIZE) return false;
  if (next_sync_committee.data == NULL ||
      next_sync_committee.len != C4_GLOAS_LCU_SYNC_COMMITTEE_SIZE) return false;
  if (next_sync_committee_branch.data == NULL ||
      next_sync_committee_branch.len != C4_GLOAS_LCU_NEXT_SC_BRANCH_SIZE) return false;
  if (finalized_header.data == NULL ||
      finalized_header.len != C4_GLOAS_LCU_HEADER_SIZE) return false;
  if (finality_branch.data == NULL ||
      finality_branch.len != C4_GLOAS_LCU_FINALITY_BRANCH_SIZE) return false;
  if (sync_aggregate.data == NULL ||
      sync_aggregate.len != C4_GLOAS_LCU_SYNC_AGGREGATE_SIZE) return false;

  uint8_t* buf = (uint8_t*) safe_malloc(C4_GLOAS_LCU_SSZ_SIZE);

  // Layout is entirely fixed-size, no SSZ offsets required. See lcu_gloas.h
  // for the exact byte offsets.
  uint32_t off = 0;
  memcpy(buf + off, attested_header.data, C4_GLOAS_LCU_HEADER_SIZE);
  off += C4_GLOAS_LCU_HEADER_SIZE;
  memcpy(buf + off, next_sync_committee.data, C4_GLOAS_LCU_SYNC_COMMITTEE_SIZE);
  off += C4_GLOAS_LCU_SYNC_COMMITTEE_SIZE;
  memcpy(buf + off, next_sync_committee_branch.data, C4_GLOAS_LCU_NEXT_SC_BRANCH_SIZE);
  off += C4_GLOAS_LCU_NEXT_SC_BRANCH_SIZE;
  memcpy(buf + off, finalized_header.data, C4_GLOAS_LCU_HEADER_SIZE);
  off += C4_GLOAS_LCU_HEADER_SIZE;
  memcpy(buf + off, finality_branch.data, C4_GLOAS_LCU_FINALITY_BRANCH_SIZE);
  off += C4_GLOAS_LCU_FINALITY_BRANCH_SIZE;
  memcpy(buf + off, sync_aggregate.data, C4_GLOAS_LCU_SYNC_AGGREGATE_SIZE);
  off += C4_GLOAS_LCU_SYNC_AGGREGATE_SIZE;

  // uint64 little-endian encoding for `signatureSlot`.
  uint64_to_le(buf + off, signature_slot);
  off += 8;

  if (off != C4_GLOAS_LCU_SSZ_SIZE) {
    // impossible unless the constants drift; fail safe.
    safe_free(buf);
    return false;
  }

  *out_ssz = bytes(buf, C4_GLOAS_LCU_SSZ_SIZE);
  return true;
}

bytes_t c4_gloas_lcu_wrap_beacon_response(bytes_t lcu_ssz,
                                          uint8_t fork_version[4]) {
  if (!lcu_ssz.data || lcu_ssz.len != C4_GLOAS_LCU_SSZ_SIZE || !fork_version)
    return NULL_BYTES;

  // Beacon-API `light_client/updates` wire format for one entry:
  //   [0 .. 8)  length = 4 + lcu_ssz.len (uint64 LE, big enough for both
  //                     current updates layout and future 2^64-1 extensions)
  //   [8 .. 12) fork_version (raw bytes)
  //   [12 ..)   payload
  const uint64_t length = 4u + (uint64_t) lcu_ssz.len;
  uint8_t*       buf    = (uint8_t*) safe_malloc(C4_GLOAS_LCU_WIRE_PREFIX_SIZE + lcu_ssz.len);

  uint64_to_le(buf, length);
  memcpy(buf + 8, fork_version, 4);
  memcpy(buf + C4_GLOAS_LCU_WIRE_PREFIX_SIZE, lcu_ssz.data, lcu_ssz.len);

  return bytes(buf, C4_GLOAS_LCU_WIRE_PREFIX_SIZE + lcu_ssz.len);
}

// :: LC-Header composition helper

// Emits the 496-byte GLOAS_LIGHT_CLIENT_HEADER SSZ blob into `out_496`
// from a filled `eth_block_t`. All three components are fixed-size, so we
// just concatenate.
static bool compose_gloas_lc_header(eth_block_t* block, uint8_t out_496[C4_GLOAS_LCU_HEADER_SIZE]) {
  if (block->beacon.cl_header.bytes.data == NULL ||
      block->beacon.cl_header.bytes.len != BEACON_BLOCK_HEADER_BYTES) return false;
  if (block->beacon.block_hash_branch.data == NULL ||
      block->beacon.block_hash_branch.len != C4_GLOAS_LCU_NEXT_SC_BRANCH_SIZE) return false;

  memcpy(out_496, block->beacon.cl_header.bytes.data, BEACON_BLOCK_HEADER_BYTES);
  memcpy(out_496 + BEACON_BLOCK_HEADER_BYTES, block->el_block_hash, 32);
  memcpy(out_496 + BEACON_BLOCK_HEADER_BYTES + 32,
         block->beacon.block_hash_branch.data,
         C4_GLOAS_LCU_NEXT_SC_BRANCH_SIZE);
  return true;
}

// :: Gindex table for the 1026 SyncCommittee chunks (mirrors bootstrap_gloas.c)
//
// Order (must stay in lockstep with c4_gloas_bootstrap_chunks_to_sync_committee):
//   [0 .. 1024)   pubkey_i chunk_k = pubkey_0_c0, pubkey_0_c1, ..., pubkey_511_c1
//   [1024]        aggregate chunk 0
//   [1025]        aggregate chunk 1
static bool build_sync_committee_gindices(gindex_t sync_root_gindex,
                                          gindex_t out_gindices[TOTAL_SYNC_COMMITTEE_CHUNKS]) {
  for (uint32_t i = 0; i < SYNC_COMMITTEE_PUBKEYS; i++) {
    gindex_t elem_g = PUBKEYS_SUB_GINDEX_ELEMENT_BASE + i;
    for (uint32_t k = 0; k < PUBKEY_CHUNKS; k++) {
      gindex_t chunk_in_pubkey      = elem_g * 2u + k; // == ssz_add_gindex(elem_g, 2+k)
      gindex_t chunk_in_pubkeys_vec = ssz_add_gindex(SC_SUB_GINDEX_PUBKEYS, chunk_in_pubkey);
      if (chunk_in_pubkeys_vec == 0) return false;
      gindex_t abs = ssz_add_gindex(sync_root_gindex, chunk_in_pubkeys_vec);
      if (abs == 0) return false;
      out_gindices[i * PUBKEY_CHUNKS + k] = abs;
    }
  }
  for (uint32_t k = 0; k < AGGREGATE_PUBKEY_CHUNKS; k++) {
    gindex_t chunk_in_agg = ssz_add_gindex(SC_SUB_GINDEX_AGGREGATE, PUBKEY_SUB_GINDEX_CHUNK_BASE + k);
    if (chunk_in_agg == 0) return false;
    gindex_t abs = ssz_add_gindex(sync_root_gindex, chunk_in_agg);
    if (abs == 0) return false;
    out_gindices[SYNC_COMMITTEE_PUBKEYS * PUBKEY_CHUNKS + k] = abs;
  }
  return true;
}

// :: Orchestrator

c4_status_t c4_create_gloas_lcu(prover_ctx_t* ctx,
                                uint64_t      target_period,
                                bytes_t*      out_lcu_ssz) {
  if (!ctx || !out_lcu_ssz) return C4_ERROR;
  *out_lcu_ssz = NULL_BYTES;

  // ---------------------------------------------------------------------------
  // 1. Fetch head + parent: sig_block carries the sync aggregate, data_block
  //    is the attested one whose state root we anchor the two state proofs
  //    against.
  // ---------------------------------------------------------------------------
  ssz_ob_t  sig_block  = {0};
  ssz_ob_t  data_block = {0};
  bytes32_t data_root  = {0};
  TRY_ASYNC(c4_eth_get_signblock_and_parent(ctx, NULL, NULL, &sig_block, &data_block, data_root));

  uint64_t signature_slot = ssz_get_uint64(&sig_block, "slot");
  uint64_t attested_slot  = ssz_get_uint64(&data_block, "slot");

  // Fork gate: only Gloas is supported. Explicit identity check so any future
  // post-Gloas fork with a different LC layout is rejected instead of
  // silently opting into this builder.
  const chain_spec_t* chain = c4_eth_get_chain_spec(ctx->chain_id);
  if (!chain) THROW_ERROR("c4_create_gloas_lcu: unknown chain");
  fork_id_t fork = c4_chain_fork_id(ctx->chain_id, epoch_for_slot(attested_slot, chain));
  if (fork != C4_FORK_GLOAS)
    THROW_ERROR("c4_create_gloas_lcu: only Gloas fork is supported");

  // Spec `validate_light_client_update`: `signature_slot > attested_slot`.
  if (signature_slot <= attested_slot)
    THROW_ERROR("c4_create_gloas_lcu: signature_slot must be > attested_slot");

  // Period-mismatch skip-warn: bail out early when head already advanced
  // past the requested period. This branch is expected to be handled as a
  // warning by the server-side scheduler (`c4_ps_build_lcu`).
  if (period_for_slot(attested_slot, chain) != target_period)
    THROW_ERROR("c4_create_gloas_lcu: attested_slot period does not match target_period");
  // Sync-committee period consistency: signature_slot must live in the same
  // period as attested_slot -- otherwise the syncAggregate would attest with
  // a different committee than the one advertised by the LCU.
  if (period_for_slot(signature_slot, chain) != target_period)
    THROW_ERROR("c4_create_gloas_lcu: signature_slot period does not match target_period");

  // ---------------------------------------------------------------------------
  // 2. Fill the attested `eth_block_t` (cl_header, execution branch, el hash,
  //    sync_aggregate). Reuses the existing beacon.c helper.
  // ---------------------------------------------------------------------------
  eth_block_t attested = {0};
  TRY_ASYNC(c4_beacon_fill_becaon_block_from_eth(ctx, &attested, data_root, data_block, sig_block));
  if (attested.beacon.block_hash_branch.len != C4_GLOAS_LCU_NEXT_SC_BRANCH_SIZE)
    THROW_ERROR("c4_create_gloas_lcu: unexpected attested executionBranch depth (not Gloas)");
  if (attested.beacon.sync_aggregate.bytes.len != C4_GLOAS_LCU_SYNC_AGGREGATE_SIZE)
    THROW_ERROR("c4_create_gloas_lcu: unexpected syncAggregate size");

  bytes32_t attested_state_root = {0};
  memcpy(attested_state_root,
         attested.beacon.cl_header.bytes.data + STATE_ROOT_OFFSET, 32);

  // ---------------------------------------------------------------------------
  // 3. Fetch the finalized block. `state_id = "finalized"` is safe against
  //    Lodestar's regen guard (analogous to the bootstrap precompute path,
  //    Lodestar issue #7780).
  // ---------------------------------------------------------------------------
  eth_block_t finalized = {0};
  TRY_ASYNC(c4_beacon_get_block_for_eth(ctx, json_parse("\"finalized\""), &finalized));
  if (finalized.proof_type != C4_BLOCK_PROOF_TYPE_BEACON)
    THROW_ERROR("c4_create_gloas_lcu: finalized fetch did not yield a beacon block");
  fork_id_t fin_fork = c4_chain_fork_id(ctx->chain_id, epoch_for_slot(finalized.slot, chain));
  if (fin_fork != C4_FORK_GLOAS)
    THROW_ERROR("c4_create_gloas_lcu: finalized block is not Gloas");
  if (finalized.beacon.block_hash_branch.len != C4_GLOAS_LCU_NEXT_SC_BRANCH_SIZE)
    THROW_ERROR("c4_create_gloas_lcu: unexpected finalized executionBranch depth");
  if (finalized.slot > attested_slot)
    THROW_ERROR("c4_create_gloas_lcu: finalized_slot must be <= attested_slot");

  // ---------------------------------------------------------------------------
  // 4. State proof #1 -- 1026 next_sync_committee chunks + subroot at 2946.
  // ---------------------------------------------------------------------------
  gindex_t next_sc_gindex = c4_next_sync_committee_gindex(ctx->chain_id, attested_slot);
  if (next_sc_gindex == 0)
    THROW_ERROR("c4_create_gloas_lcu: no next_sync_committee gindex for attested slot");

  // Sanity-check the branch depth matches the gindex depth. bitlen-1 is the
  // classical Merkle branch depth.
  {
    uint8_t bl = 0;
    for (gindex_t g = next_sc_gindex; g; g >>= 1) bl++;
    if ((uint8_t) (bl - 1) != GLOAS_STATE_ROOT_TO_SC_DEPTH)
      THROW_ERROR("c4_create_gloas_lcu: unexpected next_sync_committee gindex depth");
  }

  gindex_t sc_gindices[TOTAL_SYNC_COMMITTEE_CHUNKS] = {0};
  if (!build_sync_committee_gindices(next_sc_gindex, sc_gindices))
    THROW_ERROR("c4_create_gloas_lcu: gindex composition overflow");

  bytes_t sc_descriptor = c4_ssz_compute_compact_descriptor(sc_gindices, TOTAL_SYNC_COMMITTEE_CHUNKS);
  if (sc_descriptor.len == 0)
    THROW_ERROR("c4_create_gloas_lcu: failed to build sync-committee compact descriptor");

  ssz_ob_t    sc_leaves_ob     = {0};
  ssz_ob_t    sc_descriptor_ob = {0};
  c4_status_t status           = cl_get_state_proof(
      ctx, attested_state_root, sc_descriptor,
      &sc_leaves_ob, &sc_descriptor_ob);
  safe_free(sc_descriptor.data);
  sc_descriptor.data = NULL;
  sc_descriptor.len  = 0;
  if (status != C4_SUCCESS) return status;

  uint8_t   chunk_leaves[TOTAL_SYNC_COMMITTEE_CHUNKS * 32u] = {0};
  bytes32_t sc_captured_root                                = {0};
  bytes_t   next_sync_committee_branch                      = NULL_BYTES;

  bool ok = c4_ssz_compact_multi_extract(
      sc_leaves_ob.bytes, sc_descriptor_ob.bytes,
      sc_gindices, TOTAL_SYNC_COMMITTEE_CHUNKS, attested_state_root,
      bytes(chunk_leaves, sizeof(chunk_leaves)),
      next_sc_gindex, sc_captured_root, &next_sync_committee_branch);
  if (!ok)
    THROW_ERROR("c4_create_gloas_lcu: sync-committee state-proof reconstruction failed");

  if (next_sync_committee_branch.len != C4_GLOAS_LCU_NEXT_SC_BRANCH_SIZE) {
    safe_free(next_sync_committee_branch.data);
    THROW_ERROR("c4_create_gloas_lcu: unexpected next_sync_committee branch length");
  }

  // Reassemble the SyncCommittee raw bytes and cross-check against the
  // captured subroot. The primary anchor (root check inside multi_extract)
  // already guarantees correctness; this second check localizes any
  // chunk-reassembly bug to a clear error message.
  uint8_t next_sync_committee_bytes[C4_GLOAS_LCU_SYNC_COMMITTEE_SIZE] = {0};
  if (!c4_gloas_bootstrap_chunks_to_sync_committee(
          bytes(chunk_leaves, sizeof(chunk_leaves)),
          bytes(next_sync_committee_bytes, sizeof(next_sync_committee_bytes)))) {
    safe_free(next_sync_committee_branch.data);
    THROW_ERROR("c4_create_gloas_lcu: non-canonical SSZ pubkey chunk padding");
  }

  static const ssz_def_t SYNC_COMMITTEE_CONTAINER =
      SSZ_CONTAINER("SyncCommittee", SYNC_COMMITTEE);
  ssz_ob_t sc_ob = {
      .def   = &SYNC_COMMITTEE_CONTAINER,
      .bytes = bytes(next_sync_committee_bytes, sizeof(next_sync_committee_bytes)),
  };
  bytes32_t computed_sc_root = {0};
  ssz_hash_tree_root(sc_ob, computed_sc_root);
  if (memcmp(computed_sc_root, sc_captured_root, 32) != 0) {
    safe_free(next_sync_committee_branch.data);
    THROW_ERROR("c4_create_gloas_lcu: next_sync_committee root mismatch after reassembly");
  }

  // ---------------------------------------------------------------------------
  // 5. State proof #2 -- finality branch (single leaf, gindex 735, depth 9).
  //    `c4_create_state_proof` verifies the reconstructed root against the
  //    provided state_root, so a corrupted leaf would already fail there.
  // ---------------------------------------------------------------------------
  gindex_t finalized_gindex = c4_finalized_root_gindex(ctx->chain_id, attested_slot);
  if (finalized_gindex == 0) {
    safe_free(next_sync_committee_branch.data);
    THROW_ERROR("c4_create_gloas_lcu: no finalized_root gindex for attested slot");
  }
  {
    uint8_t bl = 0;
    for (gindex_t g = finalized_gindex; g; g >>= 1) bl++;
    if ((uint8_t) (bl - 1) != GLOAS_STATE_ROOT_TO_FINALIZED_DEPTH) {
      safe_free(next_sync_committee_branch.data);
      THROW_ERROR("c4_create_gloas_lcu: unexpected finalized_root gindex depth");
    }
  }

  bytes_t finality_branch = NULL_BYTES;
  status                  = c4_create_state_proof(ctx, attested_state_root, finalized_gindex, &finality_branch);
  if (status != C4_SUCCESS) {
    safe_free(next_sync_committee_branch.data);
    return status;
  }
  if (finality_branch.len != C4_GLOAS_LCU_FINALITY_BRANCH_SIZE) {
    safe_free(next_sync_committee_branch.data);
    safe_free(finality_branch.data);
    THROW_ERROR("c4_create_gloas_lcu: unexpected finality branch length");
  }

  // Cross-check: the finality branch anchors `finalized_checkpoint.root`
  // (= hash_tree_root(finalized.beacon)) under `attested.state_root`. Since
  // the Lodestar leaf is opaque bytes, verify by rebuilding the root from
  // (finalized_header_root, finality_branch, 735) and comparing against
  // `attested_state_root`.
  bytes32_t finalized_header_root = {0};
  {
    ssz_ob_t hdr = {
        .def   = eth_ssz_type_for_denep(ETH_SSZ_BEACON_BLOCK_HEADER, ctx->chain_id),
        .bytes = bytes(finalized.beacon.cl_header.bytes.data, BEACON_BLOCK_HEADER_BYTES),
    };
    ssz_hash_tree_root(hdr, finalized_header_root);
  }
  bytes32_t recomputed_state_root = {0};
  ssz_verify_single_merkle_proof(finality_branch, finalized_header_root,
                                 finalized_gindex, recomputed_state_root);
  if (memcmp(recomputed_state_root, attested_state_root, 32) != 0) {
    safe_free(next_sync_committee_branch.data);
    safe_free(finality_branch.data);
    THROW_ERROR("c4_create_gloas_lcu: finality branch does not match finalized header");
  }

  // ---------------------------------------------------------------------------
  // 6. Compose the two Gloas LC-headers (496 bytes each).
  // ---------------------------------------------------------------------------
  uint8_t attested_header[C4_GLOAS_LCU_HEADER_SIZE]  = {0};
  uint8_t finalized_header[C4_GLOAS_LCU_HEADER_SIZE] = {0};
  if (!compose_gloas_lc_header(&attested, attested_header) ||
      !compose_gloas_lc_header(&finalized, finalized_header)) {
    safe_free(next_sync_committee_branch.data);
    safe_free(finality_branch.data);
    THROW_ERROR("c4_create_gloas_lcu: LC header composition failed");
  }

  // ---------------------------------------------------------------------------
  // 7. Assemble the final SSZ bytes.
  // ---------------------------------------------------------------------------
  bytes_t lcu       = NULL_BYTES;
  bool    assembled = c4_gloas_lcu_assemble(
      bytes(attested_header, sizeof(attested_header)),
      bytes(next_sync_committee_bytes, sizeof(next_sync_committee_bytes)),
      next_sync_committee_branch,
      bytes(finalized_header, sizeof(finalized_header)),
      finality_branch,
      attested.beacon.sync_aggregate.bytes,
      signature_slot,
      &lcu);

  safe_free(next_sync_committee_branch.data);
  safe_free(finality_branch.data);

  if (!assembled)
    THROW_ERROR("c4_create_gloas_lcu: SSZ assembly failed");

  // Account for the reconstruction cost. Per-request CU was already charged
  // by cl_get_state_proof -> c4_send_beacon_ssz_*.
  eth_cu_add_proof(ctx);

  *out_lcu_ssz = lcu;
  return C4_SUCCESS;
}
