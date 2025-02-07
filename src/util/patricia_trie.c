#include "bytes.h"
#include "crypto.h"
#include "patricia.h"
#include "rlp.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
typedef bytes_t nibbles_t;
typedef enum {
  NODE_TYPE_LEAF,
  NODE_TYPE_EXTENSION,
  NODE_TYPE_BRANCH,
} node_type_t;
#define remaining_nibbles(list, offset) \
  (nibbles_t) { nibbles.data + (offset), nibbles.len - (offset) }
typedef struct node {
  uint8_t      hash[32];
  struct node* parent;
  node_type_t  type;
  union {
    struct {
      struct node* children[16];
      bytes_t      value;
    } branch;

    struct {
      bytes_t path;
      bytes_t value;
    } leaf;

    struct {
      bytes_t      path;
      struct node* child;
    } extension;

  } values;
} node_t;

static void node_update_hash(node_t* node, bool follow_parent) {
  buffer_t buf = {0};
  if (node->type == NODE_TYPE_LEAF) {
    rlp_add_item(&buf, node->values.leaf.path);
    rlp_add_item(&buf, node->values.leaf.value);
  }
  else if (node->type == NODE_TYPE_EXTENSION) {
    rlp_add_item(&buf, node->values.extension.path);
    rlp_add_item(&buf, node->values.extension.child ? bytes(node->values.extension.child->hash, 32) : NULL_BYTES);
  }
  else if (node->type == NODE_TYPE_BRANCH) {
    for (int i = 0; i < 16; i++) {
      rlp_add_item(&buf, node->values.branch.children[i] != NULL
                             ? bytes(node->values.branch.children[i]->hash, 32)
                             : NULL_BYTES);
    }
    rlp_add_item(&buf, node->values.branch.value);
  }
  rlp_to_list(&buf);
  keccak_256(buf.data, node->hash);
  buffer_free(&buf);
  if (node->parent && follow_parent)
    node_update_hash(node->parent, true);
}

void patricia_node_free(node_t* node) {
  if (!node) return;
  if (node->type == NODE_TYPE_BRANCH) {
    for (int i = 0; i < 16; i++)
      patricia_node_free(node->values.branch.children[i]);
    if (node->values.branch.value.data)
      free(node->values.branch.value.data);
  }
  else if (node->type == NODE_TYPE_EXTENSION) {
    if (node->values.extension.path.data)
      free(node->values.extension.path.data);
    patricia_node_free(node->values.extension.child);
  }
  else if (node->type == NODE_TYPE_LEAF) {
    if (node->values.leaf.value.data)
      free(node->values.leaf.value.data);
    if (node->values.leaf.path.data)
      free(node->values.leaf.path.data);
  }
  free(node);
}

static nibbles_t path_to_nibbles(bytes_t path, bool include_prefix) {
  int      odd         = include_prefix ? (path.data[0] & 0x10) : 0;
  int      nibbles_len = path.len * 2 - include_prefix ? (odd ? 1 : 2) : 0;
  uint8_t* nibbles     = calloc(nibbles_len, 1);
  for (int i = 0; i < nibbles_len; i++)
    nibbles[i] = path.data[(i + (include_prefix << 1) - odd) >> 1] >> ((i + odd) % 2 ? 0 : 4) & 0xf;
  return bytes(nibbles, nibbles_len);
}

static bytes_t nibbles_to_path(nibbles_t nibbles, bool is_leaf) {
  uint8_t* path = calloc((nibbles.len >> 1) + 1, 1);
  for (int i = 0; i < nibbles.len; i++)
    path[(i + 1) >> 1] |= i % 2 ? nibbles.data[i] : nibbles.data[i] << 4;
  *path |= ((is_leaf >> 1) + nibbles.len % 2) << 4;
  return bytes(path, (nibbles.len >> 1) + 1);
}

static node_t* create_leaf(node_t* parent, nibbles_t nibbles, bytes_t value) {
  node_t* leaf            = calloc(1, sizeof(node_t));
  leaf->type              = NODE_TYPE_LEAF;
  leaf->values.leaf.path  = nibbles_to_path(nibbles, true);
  leaf->values.leaf.value = value;
  leaf->parent            = parent;
  return leaf;
}

static node_t* convert_to_branch(node_t* parent, nibbles_t path, int idx1, node_t* child1, int idx2, node_t* child2) {
  node_t* branch = parent;
  if (path.len > 0) {
    parent->type                   = NODE_TYPE_EXTENSION;
    parent->values.extension.path  = nibbles_to_path(path, false);
    parent->values.extension.child = calloc(1, sizeof(node_t));
    branch                         = parent->values.extension.child;
    branch->parent                 = parent;
  }

  for (int i = 0; i < 16; i++) branch->values.branch.children[i] = NULL;
  branch->values.branch.children[idx1] = child1;
  branch->values.branch.children[idx2] = child2;
  branch->values.branch.value          = NULL_BYTES;

  child1->parent = branch;
  child2->parent = branch;
  node_update_hash(child1, false);
  node_update_hash(child2, false);
  node_update_hash(branch, true);
  return branch;
}

static int nibble_cmp(nibbles_t nibbles, bytes_t path, int* path_n_len) {
  int odd         = (path.data[0] & 0x10) >> 4;
  int nibbles_len = path.len * 2 - (odd ? 1 : 2);
  *path_n_len     = nibbles_len;
  for (int i = 0; i < nibbles_len; i++) {
    uint8_t pn = path.data[(i + 2 - odd) >> 1] >> ((i + odd) % 2 ? 0 : 4) & 0xf;
    if (nibbles.len == i || nibbles.data[i] != pn)
      return i;
  }
  return nibbles_len;
}

static void set_value(node_t* parent, nibbles_t nibbles, bytes_t value) {
  int offset = 0;
  while (offset <= nibbles.len) {
    if (parent->type == NODE_TYPE_BRANCH) {
      // create leaf
      if (offset == nibbles.len) {
        // update the value of the branch
        if (parent->values.branch.value.data)
          free(parent->values.branch.value.data);
        parent->values.branch.value = bytes_dup(value);
        node_update_hash(parent, true);
        return;
      }
      else if (parent->values.branch.children[nibbles.data[offset]] == NULL) {
        // create leaf
        node_t* leaf                                         = create_leaf(parent, remaining_nibbles(nibbles, offset + 1), bytes_dup(value));
        parent->values.branch.children[nibbles.data[offset]] = leaf;
        node_update_hash(leaf, true);
        return;
      }
      else
        // follow the branch
        parent = parent->values.branch.children[nibbles.data[offset++]];
    }
    else {
      nibbles_t remaining = remaining_nibbles(nibbles, offset);
      int       leaf_nibble_len;
      int       same = nibble_cmp(remaining, parent->values.leaf.path, &leaf_nibble_len);
      if (same == leaf_nibble_len && same < remaining.len) {
        if (parent->type == NODE_TYPE_EXTENSION) {
          offset += same;
          parent = parent->values.extension.child;
        }
        else {
          // TODO: create a branch, but this should never happen, if the length of the keys are the same.
        }
      }
      else if (same == leaf_nibble_len) {
        // TODO: check if the leaf is a extension, which should never happem, if the length of all keys are the same.!
        // update leaf value
        if (parent->values.leaf.value.data)
          free(parent->values.leaf.value.data);
        parent->values.leaf.value = bytes_dup(value);
        node_update_hash(parent, true);
        return;
      }
      else {
        nibbles_t leaf_nibbles = path_to_nibbles(parent->values.leaf.path, true);
        node_t*   old_leaf     = malloc(sizeof(node_t));
        *old_leaf              = *parent;
        free(old_leaf->values.leaf.path.data);
        old_leaf->values.leaf.path = nibbles_to_path(remaining_nibbles(leaf_nibbles, same + 1), old_leaf->type == NODE_TYPE_LEAF);
        node_t* leaf               = create_leaf(NULL, remaining_nibbles(nibbles, offset + same + 1), bytes_dup(value));
        convert_to_branch(parent, remaining_nibbles(leaf_nibbles, same), leaf_nibbles.data[same], old_leaf, nibbles.data[offset + same], leaf);
        free(leaf_nibbles.data);
        return;
      }
    }
  }
}

void patricia_set_value(node_t** root, bytes_t path, bytes_t value) {
  nibbles_t nibbles = path_to_nibbles(path, false);
  if (*root == NULL) {
    *root                      = calloc(1, sizeof(node_t));
    (*root)->type              = NODE_TYPE_LEAF;
    (*root)->values.leaf.path  = nibbles_to_path(nibbles, true);
    (*root)->values.leaf.value = bytes_dup(value);
    node_update_hash(*root, false);
  }
  else
    set_value(*root, nibbles, value);
  free(nibbles.data);
}
