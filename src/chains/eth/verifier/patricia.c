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

#include "patricia.h"
#include "crypto.h"
#include "rlp.h"
#include "ssz.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH      64
#define NODE_BRANCH    17
#define NODE_LEAF      2
#define NODE_EXTENSION 3

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

static int handle_node(bytes_t* raw, uint8_t** k, int last_node, bytes_t* last_val, uint8_t* next_hash, size_t* depth) {
  bytes_t val;
  bytes_t node;

  if (MAX_DEPTH < (++(*depth))) return 0;

  rlp_decode(raw, 0, &node);
  switch ((int) rlp_decode(&node, -1, NULL)) {

    case NODE_BRANCH:
      if (**k == 0xFF) {
        if (!last_node || rlp_decode(&node, 16, &node) != RLP_ITEM) return 0;
        *last_val = node;
        return node.len == 0 ? 2 : 3;
      }

      if (rlp_decode(&node, **k, &val) == RLP_LIST) { // found embedded node in branch
        rlp_decode(&node, (**k) - 1, &node);
        *k += 1;
        node = bytes(node.data + node.len, val.data + val.len - node.data); // decode the embedded node
        return handle_node(&node, k, **k == 0xFF || *(*k + 1) == 0xFF, last_val, next_hash, depth);
      }
      else if (val.len != 32) // we got a NULL, which means value does not exists
        return 2;
      else
        memcpy(next_hash, val.data, 32); // the hash of the next node
      *k += 1;
      return 1;

    case NODE_LEAF:
      if (rlp_decode(&node, 0, &val) != RLP_ITEM)
        return 0;
      else {
        uint8_t* pn      = patricia_to_nibbles(val, 1);
        int      match   = patricia_match_nibbles(pn, *k);
        int      np_len  = count_nibbles(pn);
        int      is_leaf = *val.data & 32;
        safe_free(pn);

        if (match < np_len) return last_node ? 2 : 0;

        *k += np_len;
        if (rlp_decode(&node, 1, &val) == RLP_LIST) { // embedded
          rlp_decode(&node, 0, &node);
          node.data += node.len;
          node.len = val.data + val.len - node.data;

          return handle_node(&node, k, k && *k && *(k + 1) == NULL, last_val, next_hash, depth);
        }
        else if (**k == 0xFF) { // we reached the end of the path
          if (is_leaf) {
            *last_val = val;
            return last_node ? 3 : 0;
          }
          else if (last_node)
            return 0; // this is an extension node, but the path is not complete

          // follow the extension, which should lead to branch with the value.
          *last_val = val;
          memcpy(next_hash, val.data, (val.len >= 32) ? 32 : val.len);
          return 1;

          //          if (!last_node || (expected_val == NULL && is_leaf)) return 0;
        }
        else if (is_leaf)           // we found a leaf matching the path, but the patch is not complete yet.
          return last_node ? 2 : 0; // it is either a proof, that the account does not exist or an error.
        else {                      // it is an extension and we follow the extension to the branch.
          *last_val = val;
          memcpy(next_hash, val.data, (val.len >= 32) ? 32 : val.len);
          return 1;
        }
      }

    default:
      return 0;
  }
  return 1;
}

INTERNAL patricia_result_t patricia_verify(bytes32_t root, bytes_t path, ssz_ob_t proof, bytes_t* expected) {
  int       result     = 1;
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
      result = 0;
      break;
    }
    if (1 != (result = handle_node(&witness.bytes, &key, i + 1 == proof_len, &last_value, expected_hash, &depth))) break;
  }

  if (result && expected)
    *expected = result == 2 ? NULL_BYTES : last_value;

  if (nibbles) safe_free(nibbles);
  return result == 3 ? PATRICIA_FOUND : (patricia_result_t) result;
}
