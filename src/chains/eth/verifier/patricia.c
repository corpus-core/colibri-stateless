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

#include "patricia.h"
#include "crypto.h"
#include "rlp.h"
#include "ssz.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH           64
#define NODE_BRANCH         17
#define NODE_LEAF           2
#define NODE_EXTENSION      3
#define MPT_PROOF_MAX_NODES 65536

/* last_node argument of handle_node: ordered proof still in progress, last
 * ordered node, or hash-lookup walk (unordered multi-proof). */
#define NODE_MORE   0
#define NODE_LAST   1
#define NODE_LOOKUP 2

static int patricia_match_nibbles(uint8_t* a, uint8_t* b) {
  for (int i = 0;; i++) {
    if (a[i] == 0xff || b[i] == 0xff || a[i] != b[i]) return i;
  }
  return -1;
}

static int count_nibbles(uint8_t* a) {
  for (int n = 0;; n++) {
    if (a[n] == 0xFF) return n;
  }
}

static uint8_t* patricia_to_nibbles(bytes_t p, bool prefix) {
  size_t   count   = 0;
  uint8_t* nibbles = safe_calloc(1 + (p.len << 1), 1);
  for (size_t i = 0; i < p.len; i++) {
    nibbles[count++] = p.data[i] >> 4;
    nibbles[count++] = p.data[i] & 0x0F;
    if (prefix && i == 0)
      nibbles[0] = nibbles[(count = nibbles[0] & 1 ? 1 : 0)];
  }

  nibbles[count] = 0xFF;
  return nibbles;
}

// Result of walking one node of a proof. The values differ from `patricia_result_t`: a walk
// which still expects a node is no result of a verification, see `to_patricia_result`.
typedef enum {
  WALK_INVALID      = 0, // the node is malformed or does not fit the proof
  WALK_CONTINUE     = 1, // the walk continues with the node of the returned hash
  WALK_NOT_EXISTING = 2, // the path is proven absent
  WALK_FOUND        = 3  // the value of the path was found
} walk_result_t;

/**
 * Maps the result of a finished walk to the result of the verification.
 *
 * A walk which still expects a node belongs to a proof which is empty or ends before the
 * path is resolved. Such a proof neither proves the value nor its absence.
 *
 * @param result result of the last walked node
 * @return PATRICIA_FOUND, PATRICIA_NOT_EXISTING or PATRICIA_INVALID
 */
static patricia_result_t to_patricia_result(walk_result_t result) {
  switch (result) {
    case WALK_FOUND: return PATRICIA_FOUND;
    case WALK_NOT_EXISTING: return PATRICIA_NOT_EXISTING;
    default: return PATRICIA_INVALID;
  }
}

/**
 * Returns the complete RLP encoding (header and payload) of a node embedded in its parent.
 *
 * A node is only embedded when its encoding is shorter than 32 bytes, so its list header
 * is always the single byte right in front of the payload.
 *
 * @param parent payload of the parent node
 * @param child payload of the embedded list as decoded from the parent
 * @param embedded receives the encoded embedded node
 * @return true if the child is a well-formed embedded node inside the parent
 */
static bool embedded_node(bytes_t parent, bytes_t child, bytes_t* embedded) {
  if (!parent.data || !child.data || child.len >= 31) return false;
  if (child.data <= parent.data || child.data + child.len > parent.data + parent.len) return false;
  if (child.data[-1] != 0xc0 + child.len) return false;
  *embedded = bytes(child.data - 1, child.len + 1);
  return true;
}

/**
 * Walks one node of a Merkle Patricia proof.
 *
 * The node is not authenticated yet when it is the first one of a proof, so every length
 * and every decoding result is checked here.
 *
 * @param raw RLP encoded node
 * @param k remaining nibbles of the path, terminated by 0xFF; advanced by the consumed nibbles
 * @param last_node NODE_LAST if no further node follows in an ordered proof, NODE_MORE if one follows, NODE_LOOKUP for an unordered proof
 * @param last_val receives the value if the path ends in this node
 * @param next_hash receives the hash of the next node if the walk continues
 * @param depth number of nodes walked so far, limited to MAX_DEPTH
 * @return WALK_INVALID if the node is invalid, WALK_CONTINUE if the walk continues with next_hash, WALK_NOT_EXISTING if the path does not exist, WALK_FOUND if the value was found
 */
static walk_result_t handle_node(bytes_t* raw, uint8_t** k, int last_node, bytes_t* last_val, uint8_t* next_hash, size_t* depth) {
  bytes_t    val  = {0};
  bytes_t    node = {0};
  rlp_type_t type = RLP_NOT_FOUND;

  if (MAX_DEPTH < (++(*depth))) return WALK_INVALID;

  if (rlp_decode(raw, 0, &node) != RLP_LIST) return WALK_INVALID;
  switch ((int) rlp_decode(&node, -1, NULL)) {

    case NODE_BRANCH:
      if (**k == 0xFF) {
        if (!last_node || rlp_decode(&node, 16, &node) != RLP_ITEM) return WALK_INVALID;
        *last_val = node;
        return node.len == 0 ? WALK_NOT_EXISTING : WALK_FOUND;
      }

      type = rlp_decode(&node, **k, &val);
      if (type == RLP_LIST) { // found embedded node in branch
        // decode the embedded node
        bytes_t embedded = {0};
        if (!embedded_node(node, val, &embedded)) return WALK_INVALID;
        *k += 1;
        // an embedded node is part of the node holding it, so it is the last one if its holder is
        return handle_node(&embedded, k, last_node, last_val, next_hash, depth);
      }
      if (type != RLP_ITEM) return WALK_INVALID;
      if (val.len == 0) return WALK_NOT_EXISTING; // we got a NULL, which means value does not exists
      if (val.len != 32) return WALK_INVALID;     // a child is either empty, embedded or a hash
      memcpy(next_hash, val.data, 32);            // the hash of the next node
      *k += 1;
      return WALK_CONTINUE;

    case NODE_LEAF:
      if (rlp_decode(&node, 0, &val) != RLP_ITEM || val.len == 0) // the path holds at least its prefix
        return WALK_INVALID;
      else {
        uint8_t* pn      = patricia_to_nibbles(val, 1);
        int      match   = patricia_match_nibbles(pn, *k);
        int      np_len  = count_nibbles(pn);
        int      is_leaf = *val.data & 32;
        safe_free(pn);

        if (match < np_len) return last_node ? WALK_NOT_EXISTING : WALK_INVALID;

        *k += np_len;
        type = rlp_decode(&node, 1, &val);
        if (type == RLP_LIST) { // embedded
          bytes_t embedded = {0};
          if (!embedded_node(node, val, &embedded)) return WALK_INVALID;
          return handle_node(&embedded, k, last_node, last_val, next_hash, depth);
        }
        if (type != RLP_ITEM) return WALK_INVALID;
        if (**k == 0xFF) { // we reached the end of the path
          if (is_leaf) {
            *last_val = val;
            return last_node ? WALK_FOUND : WALK_INVALID;
          }
          else if (last_node == NODE_LAST)
            return WALK_INVALID; // ordered proof ended on an extension; value lives in the next node

          // follow the extension, which should lead to branch with the value.
          if (val.len != 32) return WALK_INVALID;
          memcpy(next_hash, val.data, 32);
          return WALK_CONTINUE;
        }
        else if (is_leaf)           // we found a leaf matching the path, but the patch is not complete yet.
          return last_node ? WALK_NOT_EXISTING : WALK_INVALID; // it is either a proof, that the account does not exist or an error.
        else {                      // it is an extension and we follow the extension to the branch.
          if (val.len != 32) return WALK_INVALID;
          memcpy(next_hash, val.data, 32);
          return WALK_CONTINUE;
        }
      }

    default:
      return WALK_INVALID;
  }
  return WALK_CONTINUE;
}

INTERNAL patricia_result_t patricia_verify(bytes32_t root, bytes_t path, ssz_ob_t proof, bytes_t* expected) {
  walk_result_t result     = WALK_CONTINUE;
  uint8_t*  nibbles    = patricia_to_nibbles(path, 0);
  uint8_t*  key        = nibbles;
  bytes_t   last_value = NULL_BYTES;
  bytes32_t expected_hash;
  bytes32_t node_hash;

  uint32_t proof_len = ssz_len(proof);
  size_t   depth     = 0;
  for (uint32_t i = 0; i < proof_len; i++) {
    ssz_ob_t witness = ssz_at(proof, i);
    keccak(witness.bytes, node_hash);
    if (i == 0) {
      memcpy(expected_hash, node_hash, 32);
      memcpy(root, node_hash, 32);
    }
    else if (memcmp(expected_hash, node_hash, 32)) {
      result = WALK_INVALID;
      break;
    }
    result = handle_node(&witness.bytes, &key, i + 1 == proof_len ? NODE_LAST : NODE_MORE, &last_value, expected_hash, &depth);
    if (result != WALK_CONTINUE) break;
  }

  // A walk which still expects a node (empty or truncated proof) ends as PATRICIA_INVALID.
  patricia_result_t verified = to_patricia_result(result);
  if (verified != PATRICIA_INVALID && expected)
    *expected = verified == PATRICIA_NOT_EXISTING ? NULL_BYTES : last_value;

  if (nibbles) safe_free(nibbles);
  return verified;
}

static int mpt_find_hash(const mpt_proof_t* proof, const uint8_t* hash) {
  if (!proof || !proof->hashes || !hash) return -1;
  for (uint32_t i = 0; i < proof->nodes_len; i++) {
    if (memcmp(proof->hashes[i], hash, 32) == 0) return (int) i;
  }
  return -1;
}

INTERNAL void mpt_proof_init(mpt_proof_t* proof, ssz_ob_t nodes, bytes32_t root) {
  uint32_t n;

  if (!proof) return;
  memset(proof, 0, sizeof(*proof));
  proof->nodes = nodes;
  if (root) memcpy(proof->root, root, 32);
  if (!nodes.def || !nodes.bytes.data) return;
  if (!ssz_is_valid(nodes, true, NULL)) return;

  n = ssz_len(nodes);
  if (!n || n > MPT_PROOF_MAX_NODES) return;

  proof->hashes    = safe_malloc((size_t) n * sizeof(bytes32_t));
  proof->nodes_len = n;
  for (uint32_t i = 0; i < n; i++) {
    ssz_ob_t node = ssz_at(nodes, i);
    keccak(node.bytes, proof->hashes[i]);
  }
}

INTERNAL void mpt_proof_free(mpt_proof_t* proof) {
  if (!proof) return;
  safe_free(proof->hashes);
  proof->hashes    = NULL;
  proof->nodes_len = 0;
}

INTERNAL patricia_result_t patricia_verify_multi(mpt_proof_t* proof, bytes_t path, bytes_t* leaf) {
  walk_result_t result     = WALK_INVALID;
  uint8_t*  nibbles    = NULL;
  uint8_t*  key        = NULL;
  bytes_t   last_value = NULL_BYTES;
  bytes32_t next_hash;
  size_t    depth = 0;
  int       idx;

  if (!proof) return PATRICIA_INVALID;

  idx = mpt_find_hash(proof, proof->root);
  if (idx < 0) return PATRICIA_INVALID;

  nibbles = patricia_to_nibbles(path, 0);
  key     = nibbles;
  memcpy(next_hash, proof->root, 32);
  result = WALK_CONTINUE;

  while (result == WALK_CONTINUE) {
    ssz_ob_t witness = ssz_at(proof->nodes, (uint32_t) idx);
    if (!witness.bytes.data) {
      result = WALK_INVALID;
      break;
    }
    result = handle_node(&witness.bytes, &key, NODE_LOOKUP, &last_value, next_hash, &depth);
    if (result != WALK_CONTINUE) break;
    idx = mpt_find_hash(proof, next_hash);
    if (idx < 0) {
      result = WALK_INVALID;
      break;
    }
  }

  patricia_result_t verified = to_patricia_result(result);
  if (verified != PATRICIA_INVALID && leaf)
    *leaf = verified == PATRICIA_NOT_EXISTING ? NULL_BYTES : last_value;

  if (nibbles) safe_free(nibbles);
  return verified;
}
