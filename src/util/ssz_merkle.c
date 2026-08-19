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

#include "crypto.h"
#include "ssz.h"
#include <stdlib.h>
#if defined(_MSC_VER)
#include <intrin.h> // Include for MSVC intrinsics
#endif
#include "logger.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
  buffer_t* witnesses;
  buffer_t* proof;
} merkle_proot_ctx_t;

typedef struct {
  ssz_ob_t            ob;
  int                 max_depth;
  int                 num_used_leafes;
  int                 num_leafes;
  gindex_t            root_gindex;
  gindex_t            last_gindex;
  merkle_proot_ctx_t* proof;
} merkle_ctx_t;

/**
 * Computes the ceiling of log2 for a given value.
 * Used to calculate the depth of Merkle trees.
 *
 * @param val The value to compute log2_ceil for
 * @return The ceiling of log2(val), or 0 if val < 2
 */
static inline uint32_t log2_ceil(uint32_t val) {
  if (val < 2) return 0;

  // Use MSVC intrinsic for counting leading zeros
  unsigned long index;
#if defined(_MSC_VER)
  _BitScanReverse(&index, val);
  uint32_t floor_log2 = index;
#else
  uint32_t floor_log2 = 31 - __builtin_clz(val);
#endif
  return (val & (val - 1)) == 0 ? floor_log2 : floor_log2 + 1;
}

/**
 * Checks if a type is a basic SSZ type (uint, boolean, or none).
 * Basic types are serialized directly without nested structure.
 */
static bool is_basic_type(const ssz_def_t* def) {
  return def->type == SSZ_TYPE_UINT || def->type == SSZ_TYPE_BOOLEAN || def->type == SSZ_TYPE_NONE;
}

/**
 * Computes the generalized index of a chunk within a progressive Merkle
 * tree (EIP-7916), relative to the progressive data root (excluding the
 * length / active_fields mix-in).
 *
 * The progressive tree is a chain of balanced subtrees with 4^level leaves:
 * chunk ranges 0..<1, 1..<5, 5..<21, 21..<85, ...
 *
 * @param chunk_i The chunk index
 * @return The gindex of the chunk relative to the progressive data root
 */
static gindex_t prog_chunk_gindex(uint64_t chunk_i) {
  gindex_t gindex = 1;
  uint32_t depth  = 0;
  while (chunk_i >= ((uint64_t) 1) << depth) {
    chunk_i -= ((uint64_t) 1) << depth;
    depth += 2;
    gindex = (gindex << 1) | 1;
  }
  return ((gindex << 1) << depth) + chunk_i;
}

gindex_t ssz_gindex(const ssz_def_t* def, int num_elements, ...) {
  if (!def || num_elements <= 0) return 0;
  gindex_t gindex = 1;

  va_list args;
  va_start(args, num_elements);

  for (int i = 0; i < num_elements; i++) {
    uint64_t leafes = 0;
    uint64_t idx    = 0;

    if (def->type == SSZ_TYPE_PROG_CONTAINER) {
      // progressive containers: the chunk index is the field position in the base
      // container (inactive positions are unreachable), the data tree sits at gindex 2
      // below the active_fields mix-in
      const char*      path_element = va_arg(args, const char*);
      const ssz_def_t* elements     = ssz_container_elements(def);
      uint32_t         base_len     = ssz_container_len(def);
      const ssz_def_t* found        = NULL;
      uint64_t         chunk_i      = 0;
      for (uint32_t n = 0; n < base_len; n++) {
        if (ssz_field_active(def, n) && strcmp(elements[n].name, path_element) == 0) {
          found   = elements + n;
          chunk_i = n;
          break;
        }
      }
      if (!found) {
        va_end(args);
        return 0;
      }
      def    = found;
      gindex = ssz_add_gindex(gindex, ssz_add_gindex(2, prog_chunk_gindex(chunk_i)));
      continue;
    }
    else if (def->type == SSZ_TYPE_PROG_LIST) {
      // like List, the index addresses the chunk for basic element types and the element otherwise,
      // the data tree sits at gindex 2 below the length mix-in
      idx    = (uint64_t) va_arg(args, int);
      def    = def->def.vector.type;
      gindex = ssz_add_gindex(gindex, ssz_add_gindex(2, prog_chunk_gindex(idx)));
      continue;
    }
    else if (def->type == SSZ_TYPE_CONTAINER) {
      const char* path_element = va_arg(args, const char*);
      for (int i = 0; i < def->def.container.len; i++) {
        if (strcmp(def->def.container.elements[i].name, path_element) == 0) {
          idx    = i;
          leafes = def->def.container.len;
          def    = def->def.container.elements + i;
          break;
        }
      }
    }
    else if (def->type == SSZ_TYPE_LIST) {
      leafes = is_basic_type(def->def.vector.type) ? ((def->def.vector.len * ssz_fixed_length(def->def.vector.type) + 31) >> 5) * 2 : def->def.vector.len * 2;
      idx    = (uint64_t) va_arg(args, int);
      def    = def->def.vector.type;
    }
    else if (def->type == SSZ_TYPE_VECTOR) {
      leafes = is_basic_type(def->def.vector.type)
                   ? ((def->def.vector.len * ssz_fixed_length(def->def.vector.type) + 31) >> 5)
                   : def->def.vector.len;
      idx    = (uint64_t) va_arg(args, int);
      def    = def->def.vector.type;
    }

    if (leafes == 0) {
      va_end(args);
      return 0;
    }

    uint32_t max_depth = log2_ceil(leafes);
    gindex             = ssz_add_gindex(gindex, (((gindex_t) 1) << max_depth) + idx);
  }

  va_end(args);
  return gindex;
}

static int gindex_indexOf(buffer_t* index_list, gindex_t index) {
  int       len              = index_list->data.len / sizeof(gindex_t);
  gindex_t* index_list_array = (gindex_t*) index_list->data.data;
  for (int i = 0; i < len; i++) {
    if (index_list_array[i] == index) return i;
  }
  return -1;
}

static void gindex_add(buffer_t* index_list, gindex_t index) {
  int       len              = index_list->data.len / sizeof(gindex_t);
  gindex_t* index_list_array = (gindex_t*) index_list->data.data;
  for (int i = 0; i < len; i++) {
    if (index_list_array[i] < index) {
      buffer_splice(index_list, i * sizeof(gindex_t), 0, bytes((uint8_t*) &index, sizeof(gindex_t)));
      return;
    }
    if (index_list_array[i] == index) return;
  }

  buffer_append(index_list, bytes((uint8_t*) &index, sizeof(gindex_t)));
}

static void gindex_del(buffer_t* index_list, gindex_t index) {
  int       len              = index_list->data.len / sizeof(gindex_t);
  gindex_t* index_list_array = (gindex_t*) index_list->data.data;
  for (int i = 0; i < len; i++) {
    if (index_list_array[i] == index) {
      buffer_splice(index_list, i * sizeof(gindex_t), sizeof(gindex_t), NULL_BYTES);
      return;
    }
  }
}

static void ssz_add_multi_merkle_proof(gindex_t gindex, buffer_t* witnesses, buffer_t* calculated) {
  if (gindex == 1) return;
  while (gindex > 1) {
    gindex_del(witnesses, gindex);
    gindex_add(calculated, gindex);
    gindex_t witness = (gindex & 1) ? gindex - 1 : gindex + 1;
    if (gindex_indexOf(calculated, witness) != -1 || gindex_indexOf(witnesses, witness) != -1) break;
    gindex_add(witnesses, witness);
    gindex = gindex >> 1;
  }
}

// gets the value of a field from a container by base-container position index
static ssz_ob_t ssz_get_field(ssz_ob_t* ob, int index) {
  ssz_ob_t res = {0};
  // check if the object is valid
  if (!ob || !ob->def || !ssz_is_container_type(ob->def) || !ob->bytes.data || !ob->bytes.len || index < 0)
    return res;
  const ssz_def_t* elements = ssz_container_elements(ob->def);
  uint32_t         count    = ssz_container_len(ob->def);
  if ((uint32_t) index >= count || !ssz_field_active(ob->def, (uint32_t) index)) return res;

  // iterate over the (active) fields of the container
  size_t           pos = 0;
  const ssz_def_t* def = NULL;
  for (uint32_t i = 0; i < count; i++) {
    if (!ssz_field_active(ob->def, i)) continue; // inactive position of a progressive container
    def        = elements + i;
    size_t len = ssz_fixed_length(def);

    if (pos + len > ob->bytes.len) return res;

    if ((int) i == index) {
      res.def = def;
      if (ssz_is_dynamic(def)) {
        uint32_t offset = uint32_from_le(ob->bytes.data + pos);
        if (offset > ob->bytes.len) return res;
        res.bytes.data = ob->bytes.data + offset;
        res.bytes.len  = ob->bytes.len - offset;
        pos += len;

        // find next active dynamic offset
        for (uint32_t n = i + 1; n < count; n++) {
          if (!ssz_field_active(ob->def, n)) continue;
          if (ssz_is_dynamic(elements + n)) {
            if (pos + 4 > ob->bytes.len) return (ssz_ob_t) {0};

            offset = uint32_from_le(ob->bytes.data + pos);
            if (offset < ob->bytes.len)
              res.bytes.len = ob->bytes.data + offset - res.bytes.data;
            break;
          }
          pos += ssz_fixed_length(elements + n);
        }
      }
      else {
        res.bytes.len  = len;
        res.bytes.data = ob->bytes.data + pos;
      }
      if (def->type == SSZ_TYPE_UNION) {
        if (res.bytes.len && def->def.container.len > res.bytes.data[0]) {
          res.def = def->def.container.elements + res.bytes.data[0];
          res.bytes.len--;
          res.bytes.data++;
        }
        else
          return (ssz_ob_t) {0};
      }

      return res;
    }
    pos += len;
  }
  return res;
}

const ssz_def_t* ssz_get_def(const ssz_def_t* def, const char* name) {
  const ssz_def_t* elements = ssz_container_elements(def);
  uint32_t         count    = ssz_container_len(def);
  for (uint32_t i = 0; i < count; i++) {
    if (ssz_field_active(def, i) && strcmp(elements[i].name, name) == 0) return elements + i;
  }
  return NULL;
}

ssz_ob_t ssz_get(ssz_ob_t* ob, const char* name) {
  if (!ssz_is_container_type(ob->def)) return (ssz_ob_t) {0};
  const ssz_def_t* elements = ssz_container_elements(ob->def);
  uint32_t         count    = ssz_container_len(ob->def);
  for (uint32_t i = 0; i < count; i++) {
    if (ssz_field_active(ob->def, i) && strcmp(elements[i].name, name) == 0) return ssz_get_field(ob, (int) i);
  }
  log_error("ssz_get: %s not found in %s", name, ob->def->name);
  return (ssz_ob_t) {0};
}

#ifdef PRECOMPILE_ZERO_HASHES
#define MAX_DEPTH 30
static int     inited_zero_hashed = 0;
static uint8_t ZERO_HASHES[MAX_DEPTH][32];
static void    cached_zero_hash(int depth, uint8_t* out) {
  if (depth < 0) {
    memset(out, 0, 32);
    return;
  }
  while (inited_zero_hashed < depth + 1) {
    if (inited_zero_hashed == 0) {
      bytes32_t zeros = {0};
      sha256_merkle(bytes(zeros, 32), bytes(zeros, 32), ZERO_HASHES[inited_zero_hashed]);
    }
    else
      sha256_merkle(bytes(ZERO_HASHES[inited_zero_hashed - 1], 32), bytes(ZERO_HASHES[inited_zero_hashed - 1], 32), ZERO_HASHES[inited_zero_hashed]);
    inited_zero_hashed++;
  }

  memcpy(out, ZERO_HASHES[depth], 32);
}

#endif

static int calc_num_leafes(const ssz_ob_t* ob, bool only_used) {
  const ssz_def_t* def = ob->def;
  switch (def->type) {
    case SSZ_TYPE_CONTAINER:
    case SSZ_TYPE_PROG_CONTAINER: // all base-container field positions occupy a chunk (inactive positions merkleize as zero chunks)
      return (int) ssz_container_len(def);
    case SSZ_TYPE_PROG_LIST: { // progressive lists have no capacity, so the chunk count is always the used count
      uint32_t len = ssz_len(*ob);
      if (is_basic_type(def->def.vector.type))
        return (len * ssz_fixed_length(def->def.vector.type) + 31) >> 5;
      else
        return len;
    }
    case SSZ_TYPE_PROG_BIT_LIST:
      return (ssz_len(*ob) + (SSZ_BITS_PER_CHUNK - 1)) >> 8;
    case SSZ_TYPE_VECTOR:
      if (is_basic_type(def->def.vector.type))
        return (def->def.vector.len * ssz_fixed_length(def->def.vector.type) + 31) >> 5;
      else
        return def->def.vector.len;
    case SSZ_TYPE_LIST: {
      uint32_t len = only_used ? ssz_len(*ob) : def->def.vector.len;
      if (is_basic_type(def->def.vector.type))
        return (len * ssz_fixed_length(def->def.vector.type) + 31) >> 5;
      else
        return len;
    }
    case SSZ_TYPE_BIT_LIST:
      return (((only_used ? ssz_len(*ob) : def->def.vector.len) + (SSZ_BITS_PER_CHUNK - 1)) >> 8);
    case SSZ_TYPE_BIT_VECTOR:
      return (def->def.vector.len + (SSZ_BITS_PER_CHUNK - 1)) >> 8;
    default:
      return 1;
  }
}

static void hash_tree_root(ssz_ob_t ob, uint8_t* out, merkle_ctx_t* parent);
// the chunk index is 64 bit, since progressive trees pad the deepest subtree
// with virtual zero chunks beyond the used chunk count
static void set_leaf(ssz_ob_t ob, uint64_t index, uint8_t* out, merkle_ctx_t* ctx) {
  memset(out, 0, 32);
  const ssz_def_t* def = ob.def;
  switch (def->type) {
    case SSZ_TYPE_NONE: break;
    case SSZ_TYPE_CONTAINER:
    case SSZ_TYPE_PROG_CONTAINER: {
      // inactive positions of progressive containers merkleize as zero chunks
      if (index < ssz_container_len(def) && ssz_field_active(def, (uint32_t) index))
        hash_tree_root(ssz_get_field(&ob, (int) index), out, ctx);
      break;
    }
    case SSZ_TYPE_PROG_BIT_LIST:
    case SSZ_TYPE_BIT_LIST: {
      uint32_t bit_len = ssz_len(ob);
      uint32_t chunks  = (bit_len + (SSZ_BITS_PER_CHUNK - 1)) >> 8;
      if (index < chunks) {
        uint64_t byte_offset = index << 5; // index * 32
        // Buffer overflow protection
        if (byte_offset >= ob.bytes.len) return;
        uint32_t rest = ob.bytes.len - (uint32_t) byte_offset;
        if (bit_len % 8 == 0) rest--; // Account for sentinel byte
        if (rest > SSZ_BYTES_PER_CHUNK) rest = SSZ_BYTES_PER_CHUNK;
        memcpy(out, ob.bytes.data + byte_offset, rest);
        if (index == chunks - 1 && bit_len % 8)
          out[rest - 1] -= 1 << (bit_len % 8);
      }
      return;
    }
    case SSZ_TYPE_VECTOR:
    case SSZ_TYPE_LIST:
    case SSZ_TYPE_PROG_LIST:
    case SSZ_TYPE_BIT_VECTOR: {

      // handle complex types
      if (def->type != SSZ_TYPE_BIT_VECTOR && !is_basic_type(def->def.vector.type)) {
        uint32_t len = ssz_len(ob);
        if (index < len)
          hash_tree_root(ssz_at(ob, (uint32_t) index), out, ctx);
        return;
      }

      uint64_t offset = index * SSZ_BYTES_PER_CHUNK;
      if (offset < ob.bytes.len) {
        uint32_t len = ob.bytes.len - (uint32_t) offset;
        if (len > SSZ_BYTES_PER_CHUNK) len = SSZ_BYTES_PER_CHUNK;
        memcpy(out, ob.bytes.data + offset, len);
      }
      break;
    }
    case SSZ_TYPE_UINT:
    case SSZ_TYPE_BOOLEAN:
      if (ob.bytes.len <= SSZ_BYTES_PER_CHUNK)
        memcpy(out, ob.bytes.data, ob.bytes.len);
      break;
    case SSZ_TYPE_UNION:
      // TODO imoplement it
      break;
  }
}

/**
 * Records a node hash as proof witness if the given global gindex
 * is part of the requested witness set.
 *
 * @param ctx Merkle context (no-op if NULL or no proof is attached)
 * @param gindex global gindex of the node
 * @param value The node hash to record (32 bytes)
 */
static void record_witness_at(merkle_ctx_t* ctx, gindex_t gindex, const uint8_t* value) {
  if (!ctx || !ctx->proof) return;
  int pos = gindex_indexOf(ctx->proof->witnesses, gindex);
  log_debug_full("gindex: %l (r:%l) %s %x",
                 gindex, ctx->root_gindex, pos >= 0 ? "X" : " ", bytes(value, 32));
  if (pos >= 0) {
    buffer_grow(ctx->proof->proof, (ctx->proof->witnesses->data.len / sizeof(gindex_t)) * 32);
    ctx->proof->proof->data.len = ctx->proof->witnesses->data.len / sizeof(gindex_t) * 32;
    memcpy(ctx->proof->proof->data.data + pos * 32, value, 32);
  }
}

/**
 * Records a computed node hash as proof witness if its global gindex
 * is part of the requested witness set.
 *
 * @param ctx Merkle context (no-op if no proof is attached)
 * @param local_gindex gindex of the node relative to the object's data root
 * @param out The computed node hash (32 bytes)
 */
static void record_witness(merkle_ctx_t* ctx, gindex_t local_gindex, const uint8_t* out) {
  if (!ctx->proof) return;
  record_witness_at(ctx, ssz_add_gindex(ctx->root_gindex, local_gindex), out);
}

/**
 * Recursively computes a node in the Merkle tree.
 *
 * Traverses the tree depth-first, computing leaf values at the bottom
 * and hashing pairs of children to compute parent nodes.
 * Optionally records witness nodes for proof generation.
 *
 * @param ctx Merkle context with object data and proof state
 * @param index Index of the node at the current depth
 * @param depth Current depth in the tree (0 = root)
 * @param out Output buffer for the node hash (32 bytes)
 */
static void merkle_hash(merkle_ctx_t* ctx, int index, int depth, uint8_t* out) {
  uint8_t temp[64];

  // how many leafes do we have from depth?
  int      subtree_depth = ctx->max_depth - depth;
  gindex_t gindex        = (((gindex_t) 1) << depth) + index; // global gindex

  if (subtree_depth == 0) {
    if (ctx->proof) ctx->last_gindex = ssz_add_gindex(ctx->root_gindex, gindex); // global gindex
    set_leaf(ctx->ob, index, out, ctx);
    //    char* s = bprintf(NULL, " [%l] LEAF : %x \n", gindex, bytes(out, 32));
    //    printf("%s", s);
    //   safe_free(s);
  }
  else {

#ifdef PRECOMPILE_ZERO_HASHES

    int gindex_subtree_left_leaf = gindex << subtree_depth;                          // gindex of first leaf of the current subtree
    int gindex_last_used_leaf    = (1 << ctx->max_depth) + ctx->num_used_leafes - 1; // gindex of last leaf of the used leafes
    if (gindex_last_used_leaf < gindex_subtree_left_leaf && subtree_depth < MAX_DEPTH)
      cached_zero_hash(subtree_depth - 1, out);
    else {
#endif

      merkle_hash(ctx, index << 1, depth + 1, temp);
      merkle_hash(ctx, (index << 1) + 1, depth + 1, temp + 32);

      sha256(bytes(temp, 64), out);
#ifdef PRECOMPILE_ZERO_HASHES
    }
#endif
    //    char* s = bprintf(NULL, " [%l]  %x   <= %x  %x\n", gindex, bytes(out, 32), bytes(temp, 32), bytes(temp + 32, 32));
    //    printf("%s", s);
    //   safe_free(s);
  }

  record_witness(ctx, gindex, out);
}

/**
 * Computes a node of a balanced binary subtree within a progressive Merkle
 * tree (EIP-7916). The subtree rooted at this node covers 2^depth chunks
 * starting at chunk_index; chunks beyond the used chunk count are zero.
 *
 * @param ctx Merkle context with object data and proof state
 * @param chunk_index Index of the first chunk covered by this node
 * @param depth Remaining depth below this node
 * @param local_gindex gindex of this node relative to the object's data root
 * @param out Output buffer for the node hash (32 bytes)
 */
static void merkle_hash_prog_subtree(merkle_ctx_t* ctx, uint64_t chunk_index, uint32_t depth, gindex_t local_gindex, uint8_t* out) {
  if (depth == 0) {
    if (ctx->proof) ctx->last_gindex = ssz_add_gindex(ctx->root_gindex, local_gindex);
    set_leaf(ctx->ob, chunk_index, out, ctx);
  }
#ifdef PRECOMPILE_ZERO_HASHES
  else if (chunk_index >= (uint64_t) ctx->num_used_leafes && depth <= MAX_DEPTH)
    cached_zero_hash((int) depth - 1, out);
#endif
  else {
    uint8_t temp[64];
    merkle_hash_prog_subtree(ctx, chunk_index, depth - 1, local_gindex << 1, temp);
    merkle_hash_prog_subtree(ctx, chunk_index + (((uint64_t) 1) << (depth - 1)), depth - 1, (local_gindex << 1) | 1, temp + 32);
    sha256(bytes(temp, 64), out);
  }
  record_witness(ctx, local_gindex, out);
}

/**
 * Computes a chain node of a progressive Merkle tree (EIP-7916).
 *
 * The progressive tree is a chain of balanced binary subtrees with
 * 4^level leaves each: the left child of a chain node is the balanced
 * subtree covering the next 4^level chunks, the right child is the next
 * chain node. The chain terminates with a zero node once all used chunks
 * are covered.
 *
 * @param ctx Merkle context with object data and proof state
 * @param chunk_offset Index of the first chunk covered by this chain node
 * @param level Progressive level (the left subtree covers 4^level chunks)
 * @param local_gindex gindex of this chain node relative to the object's data root
 * @param out Output buffer for the node hash (32 bytes)
 */
static void merkle_hash_progressive(merkle_ctx_t* ctx, uint64_t chunk_offset, uint32_t level, gindex_t local_gindex, uint8_t* out) {
  if (chunk_offset >= (uint64_t) ctx->num_used_leafes)
    memset(out, 0, 32); // zero terminator of the subtree chain
  else {
    uint8_t temp[64];
    merkle_hash_prog_subtree(ctx, chunk_offset, 2 * level, local_gindex << 1, temp);
    merkle_hash_progressive(ctx, chunk_offset + (((uint64_t) 1) << (2 * level)), level + 1, (local_gindex << 1) | 1, temp + 32);
    sha256(bytes(temp, 64), out);
  }
  record_witness(ctx, local_gindex, out);
}

static inline void calc_leafes(merkle_ctx_t* ctx, ssz_ob_t ob) {
  ctx->max_depth       = log2_ceil(calc_num_leafes(&ob, false));
  ctx->num_used_leafes = calc_num_leafes(&ob, true);
  ctx->num_leafes      = 1 << ctx->max_depth;
  ctx->ob              = ob;
}

/**
 * Mixes in the length for list and bit list types.
 * This is part of the SSZ hash_tree_root algorithm for variable-length types.
 *
 * @param root The current root hash (will be modified in place)
 * @param length The length to mix in
 * @param ctx Merkle context for proof generation (can be NULL)
 */
static void mix_in_length(uint8_t* root, uint32_t length, merkle_ctx_t* ctx) {
  uint8_t length_bytes[32] = {0};
  uint64_to_le(length_bytes, (uint64_t) length);
  sha256_merkle(bytes(root, 32), bytes(length_bytes, 32), root);

  // the length node is the right sibling of the data root
  if (ctx) record_witness_at(ctx, ctx->root_gindex + 1, length_bytes);
}

/**
 * Mixes in the active_fields bitvector for progressive containers (EIP-7495).
 * The bitmask is stored directly in the type definition.
 *
 * @param root The current root hash (will be modified in place)
 * @param def The progressive container definition
 * @param ctx Merkle context for proof generation (can be NULL)
 */
static void mix_in_active_fields(uint8_t* root, const ssz_def_t* def, merkle_ctx_t* ctx) {
  uint8_t  active_fields[32] = {0};
  uint64_t mask              = def->def.progressive_container.active_fields;
  // serialize the mask as a 256-bit little-endian bitvector (only bits 0..63 are used)
  for (uint32_t i = 0; i < 8; i++)
    active_fields[i] = (uint8_t) ((mask >> (i * 8)) & 0xff);
  sha256_merkle(bytes(root, 32), bytes(active_fields, 32), root);

  // the active_fields node is the right sibling of the data root
  if (ctx) record_witness_at(ctx, ctx->root_gindex + 1, active_fields);
}

/**
 * Computes the hash tree root of an SSZ object.
 * Implements the SSZ Merkleization algorithm with optional proof generation.
 *
 * @param ob The SSZ object to hash
 * @param out Output buffer for the root hash (32 bytes)
 * @param parent Parent context for nested hashing (NULL for root level)
 */
static void hash_tree_root(ssz_ob_t ob, uint8_t* out, merkle_ctx_t* parent) {
  memset(out, 0, 32);
  if (!ob.def) return;
  merkle_ctx_t ctx         = {0};
  bool         progressive = ssz_is_progressive_type(ob.def);
  ctx.root_gindex          = 1;
  calc_leafes(&ctx, ob);
  if (parent) {
    ctx.proof = parent->proof;
    // lists and progressive types adjust root_gindex for their mix-in, so their data
    // tree becomes the left child of the object root (bit lists keep the legacy behavior)
    ctx.root_gindex = (ob.def->type == SSZ_TYPE_LIST || progressive) ? parent->last_gindex * 2 : parent->last_gindex;
  }

  if (progressive)
    merkle_hash_progressive(&ctx, 0, 0, 1, out);
  else if (ctx.num_leafes == 1)
    set_leaf(ob, 0, out, NULL);
  else
    merkle_hash(&ctx, 0, 0, out);

  // Mix in length for variable-length types (lists and bit lists)
  if (ssz_is_list_type(ob.def) || ssz_is_bit_list_type(ob.def))
    mix_in_length(out, ssz_len(ob), &ctx);
  // Mix in the active_fields bitvector for progressive containers
  else if (ob.def->type == SSZ_TYPE_PROG_CONTAINER)
    mix_in_active_fields(out, ob.def, &ctx);
}

void ssz_hash_tree_root(ssz_ob_t ob, uint8_t* out) {
  hash_tree_root(ob, out, NULL);
}

bytes_t ssz_create_multi_proof_for_gindexes(ssz_ob_t root, bytes32_t root_hash, gindex_t* gindex, int gindex_len) {

  buffer_t witnesses  = {0};
  buffer_t calculated = {0};
  buffer_t proof      = {0};

  for (int i = 0; i < gindex_len; i++)
    ssz_add_multi_merkle_proof(gindex[i], &witnesses, &calculated);

  buffer_free(&calculated);

  merkle_proot_ctx_t proof_ctx = {
      .proof     = &proof,
      .witnesses = &witnesses,
  };

  merkle_ctx_t ctx = {0};
  ctx.proof        = &proof_ctx;
  ctx.root_gindex  = 1;
  ctx.last_gindex  = 1;

  hash_tree_root(root, root_hash, &ctx);

  buffer_free(&witnesses);
  return proof.data;
}

bytes_t ssz_create_multi_proof(ssz_ob_t root, bytes32_t root_hash, int gindex_len, ...) {

  gindex_t* gindex = safe_malloc(gindex_len * sizeof(gindex_t));
  va_list   args;
  va_start(args, gindex_len);
  for (int i = 0; i < gindex_len; i++)
    gindex[i] = va_arg(args, gindex_t);
  va_end(args);

  bytes_t proof = ssz_create_multi_proof_for_gindexes(root, root_hash, gindex, gindex_len);
  safe_free(gindex);
  return proof;
}

bytes_t ssz_create_proof(ssz_ob_t root, bytes32_t root_hash, gindex_t gindex) {
  return ssz_create_multi_proof(root, root_hash, 1, gindex);
}

/**
 * Resolves a compound gindex against the two-level body/EP tree cache.
 * Returns a pointer to the 32-byte hash, or NULL if the gindex falls outside the cache.
 */
static const uint8_t* resolve_cached_node(
    const bytes32_t* body_tree, uint32_t body_tree_size,
    const bytes32_t* ep_tree, uint32_t ep_tree_size,
    gindex_t ep_body_gindex,
    gindex_t gi) {

  if (gi == 0) return NULL;

  // Check if gi is within the EP subtree by finding k where gi >> k == ep_body_gindex
  uint32_t ep_body_depth = 0;
  {
    gindex_t tmp = ep_body_gindex;
    while (tmp > 1) {
      tmp >>= 1;
      ep_body_depth++;
    }
  }

  gindex_t probe = gi;
  while (probe > ep_body_gindex) probe >>= 1;

  if (probe == ep_body_gindex && gi >= ep_body_gindex) {
    // gi is in the EP subtree - compute depth within EP
    uint32_t gi_depth = 0;
    {
      gindex_t tmp = gi;
      while (tmp > 1) {
        tmp >>= 1;
        gi_depth++;
      }
    }
    if (gi_depth < ep_body_depth || (gi_depth - ep_body_depth) >= 64)
      return NULL;
    uint32_t k        = gi_depth - ep_body_depth;
    gindex_t ep_local = (((gindex_t) 1) << k) | (gi & ((((gindex_t) 1) << k) - 1));
    if (ep_local > 0 && ep_local < ep_tree_size)
      return ep_tree[ep_local];
    return NULL;
  }

  // Body-level node
  if (gi > 0 && gi < body_tree_size)
    return body_tree[gi];
  return NULL;
}

bytes_t ssz_create_multi_proof_from_tree_cache(
    const bytes32_t* body_tree, uint32_t body_tree_size,
    const bytes32_t* ep_tree, uint32_t ep_tree_size,
    gindex_t        ep_body_gindex,
    bytes32_t       root_hash,
    const gindex_t* gindex, int gindex_len) {

  buffer_t witnesses  = {0};
  buffer_t calculated = {0};

  for (int i = 0; i < gindex_len; i++)
    ssz_add_multi_merkle_proof(gindex[i], &witnesses, &calculated);

  buffer_free(&calculated);

  int       witness_count = witnesses.data.len / sizeof(gindex_t);
  gindex_t* witness_list  = (gindex_t*) witnesses.data.data;

  uint8_t* proof_data = safe_calloc(witness_count, 32);
  for (int i = 0; i < witness_count; i++) {
    const uint8_t* node = resolve_cached_node(body_tree, body_tree_size, ep_tree, ep_tree_size,
                                              ep_body_gindex, witness_list[i]);
    if (!node) {
      safe_free(proof_data);
      buffer_free(&witnesses);
      return NULL_BYTES;
    }
    memcpy(proof_data + i * 32, node, 32);
  }

  memcpy(root_hash, body_tree[1], 32);

  buffer_free(&witnesses);
  return bytes(proof_data, witness_count * 32);
}

typedef struct {
  bytes_t   witnesses_data;
  gindex_t* witnesses_gindex;
  uint32_t  witnesses_len;

  bytes_t         leafes_data;
  const gindex_t* leafes_gindex;
  uint32_t        leafes_len;
} merkle_proof_data_t;

static bytes_t merkle_get_data(merkle_proof_data_t* proof, gindex_t idx) {
  for (uint32_t i = 0; i < proof->leafes_len; i++) {
    if (proof->leafes_gindex[i] == idx)
      return bytes_slice(proof->leafes_data, i * SSZ_BYTES_PER_CHUNK, SSZ_BYTES_PER_CHUNK);
  }
  for (uint32_t i = 0; i < proof->witnesses_len; i++) {
    if (proof->witnesses_gindex[i] == idx)
      return bytes_slice(proof->witnesses_data, i * SSZ_BYTES_PER_CHUNK, SSZ_BYTES_PER_CHUNK);
  }
  return NULL_BYTES;
}

/**
 * Verifies a Merkle proof by reconstructing nodes from a leaf to the root.
 *
 * Starts at a leaf (identified by start gindex) and walks up the tree to
 * the root (end gindex), using witness nodes from the proof to compute
 * parent hashes along the way.
 *
 * @param proof Proof data containing witnesses and leaf values
 * @param start Starting gindex (leaf to verify)
 * @param end Ending gindex (typically 1 for root)
 * @param out Output buffer for the computed root hash
 * @return true if proof is valid, false if a required witness is missing
 */
static bool merkle_proof(merkle_proof_data_t* proof, gindex_t start, gindex_t end, bytes32_t out) {
  bytes32_t tmp        = {0};
  bytes_t   start_data = merkle_get_data(proof, start);
  if (start_data.len != 32) return false;
  memcpy(out, start_data.data, 32);

  while (start > end) {
    gindex_t witness      = start & 1 ? start - 1 : start + 1;
    bytes_t  witness_data = merkle_get_data(proof, witness);
    if (witness_data.data == NULL) {
      // how do we find the start for calculating this witness?
      for (int i = 0; i < proof->leafes_len && witness_data.data == NULL; i++) {
        gindex_t path = proof->leafes_gindex[i];
        for (; path > 1; path >>= 1) {
          if (path == witness && merkle_proof(proof, proof->leafes_gindex[i], witness, tmp)) {
            witness_data = bytes(tmp, 32);
            break;
          }
        }
      }
      if (witness_data.data == NULL) return false;
    }
    if (start & 1)
      sha256_merkle(witness_data, bytes(out, 32), out);
    else
      sha256_merkle(bytes(out, 32), witness_data, out);
    start >>= 1;
  }
  return true;
}

bool ssz_verify_multi_merkle_proof(bytes_t proof_data, bytes_t leafes, const gindex_t* gindex, bytes32_t out) {
  buffer_t witnesses_gindex  = {0};
  buffer_t calculated_gindex = {0};
  for (uint32_t i = 0; i < leafes.len / 32; i++)
    ssz_add_multi_merkle_proof(gindex[i], &witnesses_gindex, &calculated_gindex);

  buffer_free(&calculated_gindex);

  merkle_proof_data_t data = {
      .leafes_gindex    = gindex,
      .leafes_data      = leafes,
      .leafes_len       = leafes.len / 32,
      .witnesses_data   = proof_data,
      .witnesses_gindex = (gindex_t*) witnesses_gindex.data.data,
      .witnesses_len    = witnesses_gindex.data.len / sizeof(gindex_t),
  };

  if (data.witnesses_len != proof_data.len / 32) {
    buffer_free(&witnesses_gindex);
    return false;
  }

  // find the highest gindex since we want to start with that.
  gindex_t start = 0;
  for (uint32_t i = 0; i < data.leafes_len; i++) {
    if (data.leafes_gindex[i] > start) start = data.leafes_gindex[i];
  }

  bool result = merkle_proof(&data, start, 1, out);
  buffer_free(&witnesses_gindex);
  return result;
}

void ssz_verify_single_merkle_proof(bytes_t proof_data, bytes32_t leaf, gindex_t gindex, bytes32_t out) {
  ssz_verify_multi_merkle_proof(proof_data, bytes(leaf, 32), &gindex, out);
}

gindex_t ssz_add_gindex(gindex_t gindex1, gindex_t gindex2) {
  if (gindex1 == 0 || gindex2 == 0) return 0;
  // 64-bit floor(log2(gindex)) = depth of the gindex in the tree
  uint32_t depth1 = 0;
  for (gindex_t g = gindex1 >> 1; g; g >>= 1) depth1++;
  uint32_t depth2 = 0;
  for (gindex_t g = gindex2 >> 1; g; g >>= 1) depth2++;
  // combined depth must fit into 64 bits, otherwise the gindex would silently wrap
  if (depth1 + depth2 > 63) return 0;
  return (gindex1 << depth2) | (gindex2 & ((((gindex_t) 1) << depth2) - 1));
}
