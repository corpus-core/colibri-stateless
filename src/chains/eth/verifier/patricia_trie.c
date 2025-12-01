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

#include "bytes.h"
#include "crypto.h"
#include "patricia.h"
#include "rlp.h"
#include "ssz.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef bytes_t nibbles_t;
typedef enum {
  NODE_TYPE_LEAF,
  NODE_TYPE_EXTENSION,
  NODE_TYPE_BRANCH,
} node_type_t;

static inline nibbles_t remaining_nibbles(nibbles_t list, uint32_t offset) {
  return (nibbles_t) {.data = list.data + ((uint32_t) (offset)), .len = list.len - ((uint32_t) (offset))};
}
static inline nibbles_t slice_nibbles(nibbles_t list, uint32_t offset, uint32_t len) {
  return (nibbles_t) {.data = list.data + ((uint32_t) (offset)), .len = len};
}

typedef struct node {
  uint8_t      hash[32];
  uint32_t     hash_len;
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

static void rlp_add_child(buffer_t* buf, node_t* node) {
  if (!node)
    rlp_add_item(buf, NULL_BYTES);
  else if (node->hash_len == 32)
    rlp_add_item(buf, bytes(node->hash, 32));
  else {
    bytes_t decoded = {0};
    bytes_t data    = bytes(node->hash, node->hash_len);
    rlp_decode(&data, 0, &decoded);
    rlp_add_list(buf, decoded);
  }
}

static void serialize_node(node_t* node, buffer_t* buf) {
  if (node->type == NODE_TYPE_LEAF) {
    rlp_add_item(buf, node->values.leaf.path);
    rlp_add_item(buf, node->values.leaf.value);
  }
  else if (node->type == NODE_TYPE_EXTENSION) {
    rlp_add_item(buf, node->values.extension.path);
    rlp_add_child(buf, node->values.extension.child);
  }
  else if (node->type == NODE_TYPE_BRANCH) {
    for (int i = 0; i < 16; i++)
      rlp_add_child(buf, node->values.branch.children[i]);
    rlp_add_item(buf, node->values.branch.value);
  }
}

static void node_update_hash(node_t* node, bool follow_parent, ssz_builder_t* builder) {
  buffer_t buf = {0};
  serialize_node(node, &buf);
  rlp_to_list(&buf);
  node->hash_len = buf.data.len;
  if (builder) ssz_add_dynamic_list_bytes(builder, 0, buf.data);
  if (node->hash_len >= 32) {
    keccak(buf.data, node->hash);
    node->hash_len = 32;
  }
  else
    memcpy(node->hash, buf.data.data, node->hash_len);
  buffer_free(&buf);
  if (node->parent && follow_parent)
    node_update_hash(node->parent, true, NULL);
}

INTERNAL void patricia_node_free(node_t* node) {
  if (!node) return;
  if (node->type == NODE_TYPE_BRANCH) {
    for (int i = 0; i < 16; i++)
      patricia_node_free(node->values.branch.children[i]);
    if (node->values.branch.value.data)
      safe_free(node->values.branch.value.data);
  }
  else if (node->type == NODE_TYPE_EXTENSION) {
    if (node->values.extension.path.data)
      safe_free(node->values.extension.path.data);
    patricia_node_free(node->values.extension.child);
  }
  else if (node->type == NODE_TYPE_LEAF) {
    if (node->values.leaf.value.data)
      safe_free(node->values.leaf.value.data);
    if (node->values.leaf.path.data)
      safe_free(node->values.leaf.path.data);
  }
  safe_free(node);
}

static nibbles_t path_to_nibbles(bytes_t path, bool include_prefix) {
  int      odd         = include_prefix ? ((path.data[0] & 0x10) >> 4) : 0;
  int      nibbles_len = path.len * 2 - (include_prefix ? (odd ? 1 : 2) : 0);
  uint8_t* nibbles     = safe_calloc(nibbles_len, 1);
  for (int i = 0; i < nibbles_len; i++)
    nibbles[i] = path.data[(i + (include_prefix << 1) - odd) >> 1] >> ((i + odd) % 2 ? 0 : 4) & 0xf;
  return bytes(nibbles, nibbles_len);
}

static bytes_t nibbles_to_path(nibbles_t nibbles, bool is_leaf) {
  int      len  = (nibbles.len >> 1) + 1;
  uint8_t* path = safe_calloc(len + 1, 1);
#ifdef __clang_analyzer__
  if (!path) return NULL_BYTES;
  memset(path, 0, len + 1);
#endif

  path[0] = ((is_leaf << 1) + (nibbles.len & 1)) << 4;
  int pos = (nibbles.len & 1) ? 1 : 2;
  for (int i = 0; i < nibbles.len; i++, pos++) {
    int idx = pos >> 1;
    if (idx >= len) break; // Safety check
    if (pos % 2)
      path[idx] = path[idx] | nibbles.data[i];
    else
      path[idx] = path[idx] | (nibbles.data[i] << 4);
  }
  return bytes(path, len);
}

static node_t* create_leaf(node_t* parent, nibbles_t nibbles, bytes_t value) {
  node_t* leaf            = safe_calloc(1, sizeof(node_t));
  leaf->type              = NODE_TYPE_LEAF;
  leaf->values.leaf.path  = nibbles_to_path(nibbles, true);
  leaf->values.leaf.value = value;
  leaf->parent            = parent;
  return leaf;
}

static node_t* convert_to_branch(node_t* parent, nibbles_t path, int idx1, node_t* child1, int idx2, node_t* child2, bytes_t value) {
  node_t* branch = parent;
  if (path.len > 0) {
    parent->type                   = NODE_TYPE_EXTENSION;
    parent->values.extension.path  = nibbles_to_path(path, false);
    parent->values.extension.child = safe_calloc(1, sizeof(node_t));
    branch                         = parent->values.extension.child;
    branch->parent                 = parent;
  }

  for (int i = 0; i < 16; i++) branch->values.branch.children[i] = NULL;
  if (idx1 != -1) branch->values.branch.children[idx1] = child1;
  if (idx2 != -1) branch->values.branch.children[idx2] = child2;
  branch->values.branch.value = value;
  branch->type                = NODE_TYPE_BRANCH;

  if (child1) child1->parent = branch;
  if (child2) child2->parent = branch;
  if (child1) node_update_hash(child1, false, NULL);
  if (child2) node_update_hash(child2, false, NULL);
  node_update_hash(branch, true, NULL);
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
          safe_free(parent->values.branch.value.data);
        parent->values.branch.value = bytes_dup(value);
        node_update_hash(parent, true, NULL);
        return;
      }
      else if (parent->values.branch.children[nibbles.data[offset]] == NULL) {
        // create leaf
        node_t* leaf                                         = create_leaf(parent, remaining_nibbles(nibbles, offset + 1), bytes_dup(value));
        parent->values.branch.children[nibbles.data[offset]] = leaf;
        node_update_hash(leaf, true, NULL);
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
      if (same == leaf_nibble_len && same <= remaining.len && parent->type == NODE_TYPE_EXTENSION) { // the nibbles match thw whol extension
        offset += same;                                                                              // so we follow
        parent = parent->values.extension.child;                                                     // with the child
      }
      else if (same == leaf_nibble_len && same == remaining.len && parent->type == NODE_TYPE_LEAF) { // if it's a leaf and path mathes
        if (parent->values.leaf.value.data) safe_free(parent->values.leaf.value.data);               // clean up the old value
        parent->values.leaf.value = bytes_dup(value);                                                // we update the value
        node_update_hash(parent, true, NULL);
        return;
      }
      else {
        node_t*   old_leaf         = NULL;
        node_t*   new_leaf         = NULL;
        int       old_idx          = -1;
        int       new_idx          = -1;
        bytes_t   branch_value     = NULL_BYTES;
        nibbles_t leaf_nibbles     = path_to_nibbles(parent->values.leaf.path, true);
        int       current_node_len = leaf_nibbles.len;
        int       remaining_len    = remaining.len;
        int       matching         = same;

        // cleanup old path memory
        safe_free(parent->values.extension.path.data);

        // handle existing node
        if (current_node_len == matching && parent->type == NODE_TYPE_LEAF) {
          branch_value = parent->values.leaf.value;
        }
        else if (current_node_len == matching + 1) {
          if (parent->type == NODE_TYPE_EXTENSION) {
            old_leaf = parent->values.extension.child;
            old_idx  = leaf_nibbles.data[matching];
          }
          else {
            old_leaf = create_leaf(NULL, remaining_nibbles(leaf_nibbles, matching + 1), parent->values.leaf.value);
            old_idx  = leaf_nibbles.data[matching];
          }
        }
        else if (current_node_len > matching) {
          old_leaf                   = safe_malloc(sizeof(node_t)); // we create a new extension
          *old_leaf                  = *parent;                     // with the current values
          old_leaf->values.leaf.path =                              // just adjusting the path
              nibbles_to_path(remaining_nibbles(leaf_nibbles, same + 1), old_leaf->type == NODE_TYPE_LEAF);
          old_idx = leaf_nibbles.data[matching];
        }

        // handle new node
        if (remaining_len > matching) {
          new_leaf = create_leaf(NULL, remaining_nibbles(nibbles, offset + same + 1), bytes_dup(value));
          new_idx  = nibbles.data[offset + same];
        }
        else
          branch_value = bytes_dup(value);

        /*

        //        print_hex(stdout, leaf_nibbles, "leaf_nibbles: ", "\n");

        if (leaf_nibbles.len == same + 1) {            // the current extension is not needed anymore, since it would have a path length=0
         safe_free(parent->values.leaf.path.data);         // so we remove the old path
          if (parent->type == NODE_TYPE_EXTENSION)     // and put the child in the new branch
            old_leaf = parent->values.extension.child; // keeping value as they are
          else                                         //
            branch_value = parent->values.leaf.value;  // since this is a leaf, we put remove it and put the value in the branch
        }
        else if (leaf_nibbles.len == same && same < remaining.len) { // the path matches, but there is more to come
        }
        else {
          old_leaf  =safe_malloc(sizeof(node_t));    // we create a new extension
          *old_leaf = *parent;                   // with the current values
         safe_free(old_leaf->values.leaf.path.data); //
          old_leaf->values.leaf.path =           // just adjusting the path
              nibbles_to_path(remaining_nibbles(leaf_nibbles, same + 1), old_leaf->type == NODE_TYPE_LEAF);
        }

        //        print_hex(stdout, old_leaf->values.leaf.path, "old_leaf_path: ", "\n");

        node_t* leaf = NULL;
        if (offset + same == nibbles.len)
          branch_value = bytes_dup(value);
        else if (leaf_nibbles.len == same && parent->type == NODE_TYPE_LEAF)
          branch_value = parent->values.leaf.value;
        else
          leaf = create_leaf(NULL, remaining_nibbles(nibbles, offset + same + 1), bytes_dup(value));

          */
        convert_to_branch(parent, slice_nibbles(leaf_nibbles, 0, same),
                          old_idx, old_leaf, new_idx, new_leaf, branch_value);
        safe_free(leaf_nibbles.data);
        return;
      }
    }
  }
}

INTERNAL void patricia_set_value(node_t** root, bytes_t path, bytes_t value) {
  nibbles_t nibbles = path_to_nibbles(path, false);
  if (*root == NULL) {
    *root                      = safe_calloc(1, sizeof(node_t));
    (*root)->type              = NODE_TYPE_LEAF;
    (*root)->values.leaf.path  = nibbles_to_path(nibbles, true);
    (*root)->values.leaf.value = bytes_dup(value);
    node_update_hash(*root, false, NULL);
  }
  else
    set_value(*root, nibbles, value);
  safe_free(nibbles.data);
}

INTERNAL ssz_ob_t patricia_create_merkle_proof(node_t* root, bytes_t path) {
  ssz_def_t     def     = SSZ_LIST("bytes", ssz_bytes_list, 1024);
  ssz_builder_t builder = {0};
  buffer_t      buf     = {0};
  nibbles_t     nibbles = path_to_nibbles(path, false);
  int           offset  = 0;
  int           len     = 0;
  builder.def           = &def;
  while (root && offset <= nibbles.len) {
    // add the node
    if (root->hash_len >= 32)
      node_update_hash(root, false, &builder);
    len++;

    if (offset == nibbles.len) break;
    if (root->type == NODE_TYPE_BRANCH) {
      root = root->values.branch.children[nibbles.data[offset]];
      offset++;
    }
    else if (root->type == NODE_TYPE_LEAF)
      break;
    else {
      nibbles_t remaining = remaining_nibbles(nibbles, offset);
      int       leaf_nibble_len;
      int       same = nibble_cmp(remaining, root->values.leaf.path, &leaf_nibble_len);
      root           = root->values.extension.child;
      offset += same;
    }
  }
  safe_free(nibbles.data);

  // fix offsets in builder
  if (builder.fixed.data.data) {
    for (int i = 0; i < len; i++)
      uint32_to_le(builder.fixed.data.data + i * 4, uint32_from_le(builder.fixed.data.data + i * 4) + len * 4);
  }

  return ssz_builder_to_bytes(&builder);
}

INTERNAL bytes_t patricia_get_root(node_t* node) {
  return bytes(node->hash, 32);
}

static node_t* patricia_clone_node(node_t* node, node_t* parent) {
  if (node == NULL) return NULL;
  node_t* new_node = safe_malloc(sizeof(node_t));
  *new_node        = *node;
  new_node->parent = parent;
  switch (node->type) {
    case NODE_TYPE_LEAF:
      new_node->values.leaf.path  = bytes_dup(node->values.leaf.path);
      new_node->values.leaf.value = bytes_dup(node->values.leaf.value);
      break;
    case NODE_TYPE_EXTENSION:
      new_node->values.extension.path  = bytes_dup(node->values.extension.path);
      new_node->values.extension.child = patricia_clone_node(node->values.extension.child, new_node);
      break;
    case NODE_TYPE_BRANCH:
      new_node->values.branch.value = bytes_dup(node->values.branch.value);
      for (int i = 0; i < 16; i++)
        new_node->values.branch.children[i] = patricia_clone_node(node->values.branch.children[i], new_node);
      break;
  }

  return new_node;
}

INTERNAL node_t* patricia_clone_tree(node_t* node) {
  return patricia_clone_node(node, NULL);
}

#if defined(TEST) && defined(DEBUG)
static void rlp_dump(node_t* node) {
  ssz_def_t     def     = SSZ_LIST("bytes", ssz_bytes_list, 1024);
  ssz_builder_t builder = {0};
  builder.def           = &def;
  node_update_hash(node, false, &builder);
  print_hex(stdout, builder.dynamic.data, "     rlp: 0x", "\n");
  buffer_free(&builder.dynamic);
  buffer_free(&builder.fixed);
}
static void dump_node(node_t* node, int level, int idx) {
  for (int i = 0; i < level; i++) fbprintf(stdout, "  ");
  if (idx < 16) fbprintf(stdout, "%dx: ", idx);
  if (!node)
    fbprintf(stdout, "-\n");
  else if (node->type == NODE_TYPE_LEAF) {
    fbprintf(stdout, "Leaf ( %s", (node->values.leaf.path.data[0] & 16) ? "odd" : "even");
    print_hex(stdout, node->values.leaf.path, " path: ", ", ");
    print_hex(stdout, node->values.leaf.value, "value: ", " )");
    rlp_dump(node);
  }
  else if (node->type == NODE_TYPE_EXTENSION) {
    fbprintf(stdout, "Extension ( %s", (node->values.extension.path.data[0] & 16) ? "odd" : "even");
    print_hex(stdout, node->values.extension.path, " path: ", ")");
    rlp_dump(node);
    dump_node(node->values.extension.child, level + 1, 16);
  }
  else if (node->type == NODE_TYPE_BRANCH) {
    print_hex(stdout, node->values.branch.value, "Branch ( value: ", ")");
    rlp_dump(node);
    for (int i = 0; i < 16; i++) {
      if (node->values.branch.children[i] == NULL) {
        int n = i + 1;
        for (; n < 16; n++) {
          if (node->values.branch.children[n] != NULL) break;
        }
        if (n > i + 1) {
          for (int c = 0; c < level + 1; c++) fbprintf(stdout, "  ");
          fbprintf(stdout, "%dx: - (... %dx)\n", i, n - 1);
          i = n - 1;
          continue;
        }
      }
      dump_node(node->values.branch.children[i], level + 1, i);
    }
  }
}
void patricia_dump(node_t* root) {
  dump_node(root, 0, 16);
}
#endif