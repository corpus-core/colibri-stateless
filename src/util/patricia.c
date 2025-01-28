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

int patricia_match_nibbles(uint8_t* a, uint8_t* b) {
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

uint8_t* patricia_to_nibbles(bytes_t p, bool prefix) {
  size_t   count   = 0;
  uint8_t* nibbles = malloc(1 + (p.len << 1));
  for (size_t i = 0; i < p.len; i++) {
    nibbles[count++] = p.data[i] >> 4;
    nibbles[count++] = p.data[i] & 0x0F;
    if (prefix && i == 0)
      nibbles[0] = nibbles[(count = nibbles[0] & 1 ? 1 : 0)];
  }

  nibbles[count] = 0xFF;
  return nibbles;
}

static int handle_node(bytes_t* raw, uint8_t** k, bytes_t* expected_val, int last_node, bytes_t* last_val, uint8_t* next_hash, size_t* depth) {
  bytes_t val;
  bytes_t node;

  if (MAX_DEPTH < (++(*depth))) return 0;

  rlp_decode(raw, 0, &node);
  switch ((int) rlp_decode(&node, -1, NULL)) {

    case NODE_BRANCH:
      if (**k == 0xFF) {
        if (!last_node || rlp_decode(&node, 16, &node) != RLP_ITEM) return 0;
        *last_val = node;
        return 1;
      }

      if (rlp_decode(&node, **k, &val) == RLP_LIST) {
        rlp_decode(&node, (**k) - 1, &node);
        *k += 1;
        node = bytes(node.data + node.len, val.data + val.len - node.data);
        return handle_node(&node, k, expected_val, *(*k + 1) == 0xFF, last_val, next_hash, depth);
      }
      else if (val.len != 32)
        memset(next_hash, 0, 32);
      else
        memcpy(next_hash, val.data, 32);
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
        free(pn);

        if (np_len != match) return expected_val == NULL && last_node;

        *k += np_len;
        if (rlp_decode(&node, 1, &val) == RLP_LIST) {
          rlp_decode(&node, 0, &node);
          node.data += node.len;
          node.len = val.data + val.len - node.data;

          return handle_node(&node, k, expected_val, k && *k && *(k + 1) == NULL, last_val, next_hash, depth);
        }
        else if (**k == 0xFF) {
          if (!last_node || (expected_val == NULL && is_leaf)) return 0;
        }
        else if (is_leaf && expected_val != NULL)
          return 0;
      }

      *last_val = val;
      memcpy(next_hash, val.data, (val.len >= 32) ? 32 : val.len);
      break;

    default:
      return (last_node && expected_val == NULL);
  }
  return 1;
}

int patricia_verify(bytes32_t root, bytes_t* p, ssz_ob_t proof, bytes_t* expected) {
  int       result     = 1;
  uint8_t*  nibbles    = patricia_to_nibbles(*p, 0);
  uint8_t*  key        = nibbles;
  bytes_t   last_value = NULL_BYTES;
  bytes32_t expected_hash;
  bytes32_t node_hash;

  //  memcpy(expected_hash, root->data, 32);

  uint32_t proof_len = ssz_len(proof);
  size_t   depth     = 0;
  for (uint32_t i = 0; i < proof_len; i++) {
    ssz_ob_t witness = ssz_at(proof, i);
    keccak(witness.bytes, node_hash);
    if (i == 0) {
      memcpy(expected_hash, node_hash, 32);
      memcpy(root, node_hash, 32);
    }
    else if (memcmp(expected_hash, node_hash, 32))
      break;
    if (!(result = handle_node(&witness.bytes, &key, expected, i + 1 == proof_len, &last_value, expected_hash, &depth))) break;
  }

  if (result && expected) {
    if (expected->data == NULL) {
      if (last_value.data) *expected = last_value;
    }
    else if (last_value.data == NULL || expected->len != last_value.len || memcmp(expected->data, last_value.data, last_value.len))
      result = 0;
  }

  if (nibbles) free(nibbles);
  return result;
}
