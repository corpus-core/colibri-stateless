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

#include "state_proofs.h"
#include "beacon.h"
#include "crypto.h"
#include "eth_compute_units.h"
#include <stddef.h>
#include <string.h>

// SSZ type of the response returned by Lodestar's
// `/eth/v0/beacon/proof/state/{state_id}` endpoint. Kept local so it does not
// leak into fork-agnostic beacon type dispatchers. Limits mirror Lodestar
// (`CompactMultiProofType` in packages/api/src/beacon/routes/proof.ts).
static const ssz_def_t COMPACT_MULTI_PROOF_FIELDS[] = {
    SSZ_LIST("leaves", ssz_bytes32, 10000),
    SSZ_BYTES("descriptor", 2048)};
static const ssz_def_t COMPACT_MULTI_PROOF_CONTAINER =
    SSZ_CONTAINER("CompactMultiProof", COMPACT_MULTI_PROOF_FIELDS);

// :: Internal helpers

typedef struct {
  gindex_t gindex;
  uint8_t  bitlen; // number of bits in the gindex bitstring (>= 1)
} gindex_bs_t;

static inline uint8_t gindex_bitlen(gindex_t g) {
  uint8_t len = 0;
  while (g) {
    len++;
    g >>= 1;
  }
  return len;
}

// Access the bit at `bit_idx` (MSB-first within each byte) inside a byte buffer.
// Returns false if `bit_idx` is past the buffer.
static inline bool bit_at(const uint8_t* data, uint32_t total_bits, uint32_t bit_idx) {
  if (bit_idx >= total_bits) return false;
  return (data[bit_idx >> 3] >> (7 - (bit_idx & 7))) & 1;
}

// Appends one bit MSB-first to a growable buffer. `bit_len` tracks the total
// number of bits written so far (independent of buffer.data.len which grows in
// full-byte increments).
static void bit_append(buffer_t* buf, uint32_t* bit_len, bool bit) {
  uint32_t byte_idx = *bit_len >> 3;
  uint32_t bit_pos  = *bit_len & 7;
  if (byte_idx >= buf->data.len) {
    uint8_t zero = 0;
    buffer_append(buf, bytes(&zero, 1));
  }
  if (bit) buf->data.data[byte_idx] |= (uint8_t) (1u << (7 - bit_pos));
  (*bit_len)++;
}

// Bitstring lex compare (`'0' < '1'`; shorter prefix < longer).
// Matches JS `a.localeCompare(b)` for gindex bitstrings without leading zeros.
static int gindex_bs_cmp_lex(gindex_bs_t a, gindex_bs_t b) {
  uint8_t min_len = a.bitlen < b.bitlen ? a.bitlen : b.bitlen;
  for (uint8_t i = 0; i < min_len; i++) {
    int bit_a = (int) ((a.gindex >> (a.bitlen - 1 - i)) & 1);
    int bit_b = (int) ((b.gindex >> (b.bitlen - 1 - i)) & 1);
    if (bit_a != bit_b) return bit_a - bit_b;
  }
  return (int) a.bitlen - (int) b.bitlen;
}

// Linear-scan set operations. Sufficient for the small proof-set sizes we
// expect (at most a few dozen gindices for LightClientBootstrap/Update).
static bool set_contains(const buffer_t* set, gindex_bs_t val) {
  const gindex_bs_t* arr = (const gindex_bs_t*) set->data.data;
  uint32_t           n   = set->data.len / sizeof(gindex_bs_t);
  for (uint32_t i = 0; i < n; i++) {
    if (arr[i].gindex == val.gindex && arr[i].bitlen == val.bitlen) return true;
  }
  return false;
}

static void set_add(buffer_t* set, gindex_bs_t val) {
  if (set_contains(set, val)) return;
  buffer_append(set, bytes((uint8_t*) &val, sizeof(val)));
}

static void set_remove(buffer_t* set, gindex_bs_t val) {
  gindex_bs_t* arr = (gindex_bs_t*) set->data.data;
  uint32_t     n   = set->data.len / sizeof(gindex_bs_t);
  for (uint32_t i = 0; i < n; i++) {
    if (arr[i].gindex == val.gindex && arr[i].bitlen == val.bitlen) {
      buffer_splice(set, i * sizeof(gindex_bs_t), sizeof(gindex_bs_t), NULL_BYTES);
      return;
    }
  }
}

// :: Descriptor computation (ChainSafe-compatible)

// Cap the number of gindices per descriptor request. The set operations are
// O(count^2 * depth) worst case, and each gindex can add up to `depth` (<= 63)
// entries to the working sets. The Gloas bootstrap builder passes 1026 target
// gindices (1024 pubkey chunks + 2 aggregate chunks) in one request, so the
// cap needs headroom for `current + next` sync committee in future callers.
// Lodestar's own limit (`maxGindicesInProof = 512` on `descriptor.length/2 bytes`)
// stays the outermost bound; this cap is a defensive local guard on top of it.
#define MAX_COMPACT_DESCRIPTOR_GINDICES 2048u

bytes_t c4_ssz_compute_compact_descriptor(const gindex_t* indices, int count) {
  if (!indices || count <= 0 || count > MAX_COMPACT_DESCRIPTOR_GINDICES) return NULL_BYTES;

  buffer_t proof_set = {0};
  buffer_t path_set  = {0};

  for (int i = 0; i < count; i++) {
    gindex_t leaf = indices[i];
    if (leaf == 0) {
      buffer_free(&proof_set);
      buffer_free(&path_set);
      return NULL_BYTES;
    }
    uint8_t leaf_len = gindex_bitlen(leaf);
    // include the leaf itself in the proof set
    set_add(&proof_set, (gindex_bs_t) {.gindex = leaf, .bitlen = leaf_len});

    // walk from leaf to root, collecting path and sibling gindices
    gindex_t g   = leaf;
    uint8_t  len = leaf_len;
    while (len > 1) {
      // path: current node (excluding the input leaf itself)
      if (g != leaf)
        set_add(&path_set, (gindex_bs_t) {.gindex = g, .bitlen = len});
      // sibling: same depth, last bit flipped
      gindex_t sibling = g ^ 1;
      set_add(&proof_set, (gindex_bs_t) {.gindex = sibling, .bitlen = len});
      // move up
      g >>= 1;
      len--;
    }
  }

  // remove any path index that also lives in the proof set (a path node is
  // provably an ancestor of another leaf, so it does not need a witness).
  {
    gindex_bs_t* arr = (gindex_bs_t*) path_set.data.data;
    uint32_t     n   = path_set.data.len / sizeof(gindex_bs_t);
    for (uint32_t i = 0; i < n; i++) set_remove(&proof_set, arr[i]);
  }
  buffer_free(&path_set);

  // sort by bitstring lex (in-order tree traversal)
  {
    gindex_bs_t* arr = (gindex_bs_t*) proof_set.data.data;
    uint32_t     n   = proof_set.data.len / sizeof(gindex_bs_t);
    for (uint32_t i = 1; i < n; i++) {
      gindex_bs_t key = arr[i];
      int32_t     j   = (int32_t) i - 1;
      while (j >= 0 && gindex_bs_cmp_lex(arr[j], key) > 0) {
        arr[j + 1] = arr[j];
        j--;
      }
      arr[j + 1] = key;
    }
  }

  // encode: for each gindex, count trailing zeros `t` in the bitstring, emit
  // `t` zeros followed by a `1`.
  buffer_t descriptor_bits = {0};
  uint32_t bit_len         = 0;
  {
    gindex_bs_t* arr = (gindex_bs_t*) proof_set.data.data;
    uint32_t     n   = proof_set.data.len / sizeof(gindex_bs_t);
    for (uint32_t i = 0; i < n; i++) {
      uint8_t trailing_zeros = 0;
      // count trailing zeros in the bitstring (== count trailing zeros in
      // the gindex value, since gindex bitstrings have no trailing padding).
      for (uint8_t bit = 0; bit < arr[i].bitlen; bit++) {
        if (((arr[i].gindex >> bit) & 1) != 0) break;
        trailing_zeros++;
      }
      for (uint8_t k = 0; k < trailing_zeros; k++) bit_append(&descriptor_bits, &bit_len, false);
      bit_append(&descriptor_bits, &bit_len, true);
    }
  }
  buffer_free(&proof_set);

  return descriptor_bits.data;
}

// :: CompactMultiProof reconstruction

// Sorted-input entry used by c4_ssz_compact_multi_extract to (a) sort caller
// gindices by bitstring lex (matches the descriptor's in-order traversal) and
// (b) remember each entry's original position so that `leaves_out` stays in
// caller order.
typedef struct {
  gindex_t gindex;
  uint32_t caller_idx;
  uint8_t  bitlen;
} sorted_gindex_entry_t;

// Shared reconstruction state. Passed by pointer through the recursion so the
// hot recursive function keeps a small argument list.
typedef struct {
  // Descriptor + leaves cursor
  bytes_t  descriptor;
  uint32_t descriptor_bits;
  uint32_t bit_idx;
  bytes_t  leaves;
  uint32_t leaf_idx;
  uint32_t leaves_count;

  // Multi-leaf extract (sorted by bitstring lex; caller order in caller_idx).
  // sorted_gindices == NULL disables extraction (also implies sorted_count==0).
  const sorted_gindex_entry_t* sorted_gindices;
  uint32_t                     sorted_count;
  uint32_t                     caller_leaf_next; // next sorted-gindex to match
  uint8_t*                     leaves_out;       // count * 32 bytes, or NULL

  // Subroot capture + branch collection.
  // subroot_gindex == 0 disables both.
  gindex_t subroot_gindex;
  bool     subroot_captured;
  uint8_t* subroot_hash_out; // 32 bytes, or NULL
  buffer_t branch_buf;       // heap-owned; moved to caller on success
} reconstruct_state_t;

// Recursively rebuild the compact-multi-proof tree.
//
// - Advances the descriptor bit cursor and leaves cursor in a preorder DFS.
// - Records extracted leaves into `leaves_out` when the current position hits
//   the next sorted caller gindex.
// - Records the subroot hash when the current position equals `subroot_gindex`.
// - Records the leaf-to-root sibling branch for `subroot_gindex` (when set),
//   via post-order append so `branch_buf` ends up in leaf-to-root order.
static bool reconstruct(reconstruct_state_t* st, gindex_t current_gindex, bool on_path, bytes32_t out) {
  if (st->bit_idx >= st->descriptor_bits) return false;
  bool is_leaf = bit_at(st->descriptor.data, st->descriptor_bits, st->bit_idx);
  st->bit_idx++;

  if (is_leaf) {
    if (st->leaf_idx >= st->leaves_count) return false;
    memcpy(out, st->leaves.data + st->leaf_idx * 32u, 32);
    st->leaf_idx++;

    // Multi-leaf extract: caller gindices come in sorted (lex) order and the
    // compact tree walks leaves in the same order, so a single monotonically
    // advancing cursor is sufficient.
    if (st->sorted_gindices && st->caller_leaf_next < st->sorted_count &&
        st->sorted_gindices[st->caller_leaf_next].gindex == current_gindex) {
      memcpy(st->leaves_out + st->sorted_gindices[st->caller_leaf_next].caller_idx * 32u, out, 32);
      st->caller_leaf_next++;
    }

    // Subroot as leaf: capture value directly.
    if (st->subroot_gindex != 0 && current_gindex == st->subroot_gindex) {
      if (st->subroot_hash_out) memcpy(st->subroot_hash_out, out, 32);
      st->subroot_captured = true;
    }
    return true;
  }

  // guard against 64-bit gindex overflow when descending
  if (current_gindex >= (((gindex_t) 1) << 62)) return false;

  gindex_t left  = current_gindex << 1;
  gindex_t right = left | 1;

  bool left_on_path  = false;
  bool right_on_path = false;
  if (on_path && st->subroot_gindex != 0) {
    uint8_t depth_current = gindex_bitlen(current_gindex);
    uint8_t depth_target  = gindex_bitlen(st->subroot_gindex);
    if (depth_target > depth_current) {
      uint8_t bit_pos = (uint8_t) (depth_target - depth_current - 1);
      // bit at (depth_target - depth_current - 1) of target selects next child:
      // 0 -> left child, 1 -> right child.
      bool go_right = ((st->subroot_gindex >> bit_pos) & 1) != 0;
      if (go_right)
        right_on_path = true;
      else
        left_on_path = true;
    }
  }

  bytes32_t left_hash  = {0};
  bytes32_t right_hash = {0};
  if (!reconstruct(st, left, left_on_path, left_hash)) return false;
  if (!reconstruct(st, right, right_on_path, right_hash)) return false;

  sha256_merkle(bytes(left_hash, 32), bytes(right_hash, 32), out);

  // Subroot as internal node: capture hash after children have been computed.
  if (st->subroot_gindex != 0 && current_gindex == st->subroot_gindex) {
    if (st->subroot_hash_out) memcpy(st->subroot_hash_out, out, 32);
    st->subroot_captured = true;
  }

  // record sibling AFTER both children have been computed. Deepest ancestor
  // finishes first, so entries are appended in leaf-to-root order.
  if (on_path && (left_on_path || right_on_path)) {
    const uint8_t* sibling = left_on_path ? right_hash : left_hash;
    buffer_append(&st->branch_buf, bytes((uint8_t*) sibling, 32));
  }
  return true;
}

// Lex comparator for sorted_gindex_entry_t (matches gindex_bs_cmp_lex).
static int sorted_gindex_cmp_lex(sorted_gindex_entry_t a, sorted_gindex_entry_t b) {
  uint8_t min_len = a.bitlen < b.bitlen ? a.bitlen : b.bitlen;
  for (uint8_t i = 0; i < min_len; i++) {
    int bit_a = (int) ((a.gindex >> (a.bitlen - 1 - i)) & 1);
    int bit_b = (int) ((b.gindex >> (b.bitlen - 1 - i)) & 1);
    if (bit_a != bit_b) return bit_a - bit_b;
  }
  return (int) a.bitlen - (int) b.bitlen;
}

// Decode the descriptor bitlist to determine the effective number of bits.
// The descriptor byte string is padded with up to 7 zero bits at the end (JS
// `descriptorToBitlist` invariant). The bit count where `#1 > #0` marks the
// end of the tree encoding; a valid descriptor has zero-padding after that.
//
// Returns 0 on invalid input.
//
// Cap matches the SSZ container limit (`SSZ_BYTES("descriptor", 2048)` /
// Lodestar `CompactMultiProofType`). Enforcing it here as well means the
// test-visible entry point stays in lockstep with the wire-level cap when the
// input does not come through the SSZ validation layer.
#define MAX_DESCRIPTOR_BYTES 2048u
static uint32_t descriptor_valid_bits(bytes_t descriptor) {
  if (descriptor.len == 0 || descriptor.data == NULL) return 0;
  if (descriptor.len > MAX_DESCRIPTOR_BYTES) return 0;
  uint32_t max_bits = descriptor.len * 8u;
  uint32_t count0   = 0;
  uint32_t count1   = 0;
  for (uint32_t i = 0; i < max_bits; i++) {
    bool bit = bit_at(descriptor.data, max_bits, i);
    if (bit)
      count1++;
    else
      count0++;
    if (count1 > count0) {
      uint32_t body_bits = i + 1;
      // any remaining bits must be zero padding (< 8 bits total)
      uint32_t remaining = max_bits - body_bits;
      if (remaining >= 8) return 0;
      for (uint32_t j = body_bits; j < max_bits; j++) {
        if (bit_at(descriptor.data, max_bits, j)) return 0;
      }
      return body_bits;
    }
  }
  return 0;
}

bool c4_ssz_compact_multi_extract(bytes_t         leaves,
                                  bytes_t         descriptor,
                                  const gindex_t* gindices,
                                  uint32_t        count,
                                  bytes32_t       expected_root,
                                  bytes_t         leaves_out,
                                  gindex_t        subroot_gindex,
                                  bytes32_t       subroot_hash_out,
                                  bytes_t*        subroot_branch_out) {
  // Always reset the branch out-parameter first so callers can safely reuse
  // the pointer without accidentally leaking a prior allocation.
  if (subroot_branch_out) *subroot_branch_out = NULL_BYTES;

  // Basic input validation.
  if (leaves.len == 0 || leaves.data == NULL || (leaves.len % 32) != 0) return false;
  if (descriptor.data == NULL) return false;
  if (count > 0) {
    if (!gindices) return false;
    if (leaves_out.data == NULL || leaves_out.len < (uint64_t) count * 32u) return false;
  }
  if (subroot_gindex != 0) {
    // Both hash and branch outputs are mandatory when subroot capture is on.
    // Silent NULL-drop would let the caller believe capture happened.
    if (!subroot_branch_out || !subroot_hash_out) return false;
  }

  uint32_t descriptor_bits = descriptor_valid_bits(descriptor);
  if (descriptor_bits == 0) return false;

  uint32_t leaves_count = leaves.len / 32;
  // Full binary tree invariant: nodes = 2 * leaves - 1.
  if (descriptor_bits != leaves_count * 2u - 1u) return false;

  // Sort caller gindices by bitstring lex (in-order tree traversal).
  // Also enforce gindex >= 1 and reject duplicates.
  sorted_gindex_entry_t* sorted = NULL;
  if (count > 0) {
    if (count > MAX_COMPACT_DESCRIPTOR_GINDICES) return false;
    sorted = (sorted_gindex_entry_t*) safe_malloc(sizeof(sorted_gindex_entry_t) * count);
    for (uint32_t i = 0; i < count; i++) {
      if (gindices[i] == 0) {
        safe_free(sorted);
        return false;
      }
      sorted[i].gindex     = gindices[i];
      sorted[i].caller_idx = i;
      sorted[i].bitlen     = gindex_bitlen(gindices[i]);
    }
    // insertion sort (count <= 2048 in practice)
    for (uint32_t i = 1; i < count; i++) {
      sorted_gindex_entry_t key = sorted[i];
      int32_t               j   = (int32_t) i - 1;
      while (j >= 0 && sorted_gindex_cmp_lex(sorted[j], key) > 0) {
        sorted[j + 1] = sorted[j];
        j--;
      }
      sorted[j + 1] = key;
    }
    // Reject duplicates (make the leaves_out mapping well-defined).
    for (uint32_t i = 1; i < count; i++) {
      if (sorted[i - 1].gindex == sorted[i].gindex) {
        safe_free(sorted);
        return false;
      }
    }
  }

  reconstruct_state_t st = {
      .descriptor       = descriptor,
      .descriptor_bits  = descriptor_bits,
      .bit_idx          = 0,
      .leaves           = leaves,
      .leaf_idx         = 0,
      .leaves_count     = leaves_count,
      .sorted_gindices  = sorted,
      .sorted_count     = count,
      .caller_leaf_next = 0,
      .leaves_out       = count > 0 ? leaves_out.data : NULL,
      .subroot_gindex   = subroot_gindex,
      .subroot_captured = false,
      .subroot_hash_out = subroot_gindex != 0 ? subroot_hash_out : NULL,
      .branch_buf       = {0},
  };

  bytes32_t root = {0};
  if (!reconstruct(&st, /* current_gindex */ 1, /* on_path */ true, root)) {
    buffer_free(&st.branch_buf);
    safe_free(sorted);
    return false;
  }

  // strict: all bits and leaves must have been consumed
  if (st.bit_idx != descriptor_bits || st.leaf_idx != leaves_count) {
    buffer_free(&st.branch_buf);
    safe_free(sorted);
    return false;
  }

  // root must match the caller-supplied anchor
  if (memcmp(root, expected_root, 32) != 0) {
    buffer_free(&st.branch_buf);
    safe_free(sorted);
    return false;
  }

  // all caller gindices must have been matched to a compact-tree leaf
  if (count > 0 && st.caller_leaf_next != count) {
    buffer_free(&st.branch_buf);
    safe_free(sorted);
    return false;
  }

  safe_free(sorted);

  if (subroot_gindex != 0) {
    // subroot must have been visited during the descent
    if (!st.subroot_captured) {
      buffer_free(&st.branch_buf);
      return false;
    }
    // branch length must equal depth(subroot) = bitlen(subroot) - 1
    uint32_t expected_branch_bytes = ((uint32_t) gindex_bitlen(subroot_gindex) - 1u) * 32u;
    if (st.branch_buf.data.len != expected_branch_bytes) {
      buffer_free(&st.branch_buf);
      return false;
    }
    *subroot_branch_out = st.branch_buf.data;
  }
  else {
    // no subroot -> no branch, but the descent must not have appended anything
    buffer_free(&st.branch_buf);
  }
  return true;
}

bool c4_ssz_compact_to_branch(bytes_t   leaves,
                              bytes_t   descriptor,
                              gindex_t  gindex,
                              bytes32_t expected_root,
                              bytes_t*  branch_out) {
  if (!branch_out) return false;
  if (gindex < 2) {
    *branch_out = NULL_BYTES;
    return false;
  }
  // Route through the multi-extract entry point so both paths share the same
  // descriptor validation, root check, and branch length invariants.
  bytes32_t hash_placeholder = {0};
  return c4_ssz_compact_multi_extract(
      leaves, descriptor,
      /* gindices */ NULL, /* count */ 0,
      expected_root,
      /* leaves_out */ NULL_BYTES,
      /* subroot_gindex */ gindex, hash_placeholder, branch_out);
}

// :: Public API

c4_status_t c4_state_proofs_beacon_fetch(prover_ctx_t* ctx,
                                         bytes32_t     state_root,
                                         bytes_t       descriptor,
                                         ssz_ob_t*     leaves_out,
                                         ssz_ob_t*     descriptor_out) {
  if (!ctx || !leaves_out || !descriptor_out) return C4_ERROR;
  if (descriptor.len == 0 || descriptor.data == NULL)
    THROW_ERROR("c4_state_proofs_beacon_fetch: empty descriptor");
  // Bound the outbound descriptor to the same size limit we enforce on the
  // response side. Prevents runaway `bprintf` growth if a future caller passes
  // an unvalidated descriptor (defense-in-depth against DoS / memory bomb).
  if (descriptor.len > MAX_DESCRIPTOR_BYTES)
    THROW_ERROR("c4_state_proofs_beacon_fetch: descriptor exceeds max size");

  char     path[128] = {0};
  buffer_t query     = {0};
  sbprintf(path, "eth/v0/beacon/proof/state/0x%x", bytes(state_root, 32));
  bprintf(&query, "format=0x%x", descriptor);

  ssz_ob_t    response = {0};
  c4_status_t status   = c4_send_beacon_ssz_with_client_type(
      ctx, path, (char*) query.data.data,
      &COMPACT_MULTI_PROOF_CONTAINER, DEFAULT_TTL, &response,
      BEACON_CLIENT_LODESTAR);
  buffer_free(&query);
  if (status != C4_SUCCESS) return status;

  ssz_ob_t leaves_ob     = ssz_get(&response, "leaves");
  ssz_ob_t descriptor_ob = ssz_get(&response, "descriptor");

  // Defense-in-depth: the response descriptor must exactly match what we
  // asked for. The root check downstream is still the primary anchor of trust,
  // but bailing out early on a descriptor mismatch gives a clearer error path.
  if (descriptor_ob.bytes.len != descriptor.len ||
      memcmp(descriptor_ob.bytes.data, descriptor.data, descriptor.len) != 0)
    THROW_ERROR("c4_state_proofs_beacon_fetch: response descriptor does not match request");

  *leaves_out     = leaves_ob;
  *descriptor_out = descriptor_ob;
  return C4_SUCCESS;
}

c4_status_t c4_create_state_proof(prover_ctx_t* ctx,
                                  bytes32_t     state_root,
                                  gindex_t      gindex,
                                  bytes_t*      proof_result) {
  if (!ctx || !proof_result) return C4_ERROR;
  *proof_result = NULL_BYTES;
  if (gindex < 2) THROW_ERROR("c4_create_state_proof: gindex must be >= 2");

  // Build a single-gindex descriptor. Deterministic across pending re-entries.
  bytes_t descriptor = c4_ssz_compute_compact_descriptor(&gindex, 1);
  if (descriptor.len == 0) THROW_ERROR("c4_create_state_proof: failed to build descriptor");

  ssz_ob_t leaves_ob     = {0};
  ssz_ob_t descriptor_ob = {0};

  c4_status_t status = c4_state_proofs_beacon_fetch(ctx, state_root, descriptor,
                                                    &leaves_ob, &descriptor_ob);
  safe_free(descriptor.data);
  if (status != C4_SUCCESS) return status;

  bytes_t branch = NULL_BYTES;
  if (!c4_ssz_compact_to_branch(leaves_ob.bytes, descriptor_ob.bytes, gindex, state_root, &branch))
    THROW_ERROR("c4_create_state_proof: compact multi-proof did not reconstruct to state_root");

  eth_cu_add_proof(ctx);
  *proof_result = branch;
  return C4_SUCCESS;
}
