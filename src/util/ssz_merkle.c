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

gindex_t ssz_gindex(const ssz_def_t* def, int num_elements, ...) {
  if (!def || num_elements <= 0) return 0;
  gindex_t gindex = 1;

  va_list args;
  va_start(args, num_elements);

  for (int i = 0; i < num_elements; i++) {
    uint64_t leafes = 0;
    uint64_t idx    = 0;

    if (def->type == SSZ_TYPE_CONTAINER) {
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

// gets the value of a field from a container
static ssz_ob_t ssz_get_field(ssz_ob_t* ob, int index) {
  ssz_ob_t res = {0};
  // check if the object is valid
  if (!ob || !ob->def || ob->def->type != SSZ_TYPE_CONTAINER || !ob->bytes.data || !ob->bytes.len || index < 0 || index >= ob->def->def.container.len)
    return res;

  // iterate over the fields of the container
  size_t           pos = 0;
  const ssz_def_t* def = NULL;
  for (int i = 0; i < ob->def->def.container.len; i++) {
    def        = ob->def->def.container.elements + i;
    size_t len = ssz_fixed_length(def);

    if (pos + len > ob->bytes.len) return res;

    if (i == index) {
      res.def = def;
      if (ssz_is_dynamic(def)) {
        uint32_t offset = uint32_from_le(ob->bytes.data + pos);
        if (offset > ob->bytes.len) return res;
        res.bytes.data = ob->bytes.data + offset;
        res.bytes.len  = ob->bytes.len - offset;
        pos += len;

        // find next offset
        for (int n = i + 1; n < ob->def->def.container.len; n++) {
          if (ssz_is_dynamic(ob->def->def.container.elements + n)) {
            if (pos + 4 > ob->bytes.len) return (ssz_ob_t) {0};

            offset = uint32_from_le(ob->bytes.data + pos);
            if (offset < ob->bytes.len)
              res.bytes.len = ob->bytes.data + offset - res.bytes.data;
            break;
          }
          pos += ssz_fixed_length(ob->def->def.container.elements + n);
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
  for (int i = 0; i < def->def.container.len; i++) {
    if (strcmp(def->def.container.elements[i].name, name) == 0) return def->def.container.elements + i;
  }
  return NULL;
}

ssz_ob_t ssz_get(ssz_ob_t* ob, const char* name) {
  if (ob->def->type != SSZ_TYPE_CONTAINER) return (ssz_ob_t) {0};
  for (int i = 0; i < ob->def->def.container.len; i++) {
    if (strcmp(ob->def->def.container.elements[i].name, name) == 0) return ssz_get_field(ob, i);
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
      return def->def.container.len;
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
static void set_leaf(ssz_ob_t ob, int index, uint8_t* out, merkle_ctx_t* ctx) {
  memset(out, 0, 32);
  const ssz_def_t* def = ob.def;
  switch (def->type) {
    case SSZ_TYPE_NONE: break;
    case SSZ_TYPE_CONTAINER: {
      if (index < def->def.container.len)
        hash_tree_root(
            ssz_get(&ob, (char*) def->def.container.elements[index].name),
            out, ctx);
      break;
    }
    case SSZ_TYPE_BIT_LIST: {
      uint32_t bit_len = ssz_len(ob);
      uint32_t chunks  = (bit_len + (SSZ_BITS_PER_CHUNK - 1)) >> 8;
      if (index < chunks) {
        uint32_t byte_offset = index << 5; // index * 32
        // Buffer overflow protection
        if (byte_offset >= ob.bytes.len) return;
        uint32_t rest = ob.bytes.len - byte_offset;
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
    case SSZ_TYPE_BIT_VECTOR: {

      // handle complex types
      if ((def->type == SSZ_TYPE_VECTOR || def->type == SSZ_TYPE_LIST) && !is_basic_type(def->def.vector.type)) {
        uint32_t len = ssz_len(ob);
        if (index < len)
          hash_tree_root(ssz_at(ob, index), out, ctx);
        return;
      }

      int offset = index * SSZ_BYTES_PER_CHUNK;
      int len    = ob.bytes.len - offset;
      if (len > SSZ_BYTES_PER_CHUNK) len = SSZ_BYTES_PER_CHUNK;
      if (offset < ob.bytes.len)
        memcpy(out, ob.bytes.data + offset, len);
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

  if (ctx->proof) {
    gindex  = ssz_add_gindex(ctx->root_gindex, gindex); // global gindex
    int pos = gindex_indexOf(ctx->proof->witnesses, gindex);
    log_debug_full("gindex: %l (i: %d  d:%d  r:%l) %s %x",
                   gindex, index, depth, ctx->root_gindex, pos >= 0 ? "X" : " ", bytes(out, 32));
    //    fprintf(stderr, "gindex: %llu (i: %d  d:%d  r:%llu) %s", gindex, index, depth, ctx->root_gindex, pos >= 0 ? "X" : " ");
    //    print_hex(stderr, bytes(out, 32), " : ", "\n");
    if (pos >= 0) {
      buffer_grow(ctx->proof->proof, (ctx->proof->witnesses->data.len / sizeof(gindex_t)) * 32);
      ctx->proof->proof->data.len = ctx->proof->witnesses->data.len / sizeof(gindex_t) * 32;
      memcpy(ctx->proof->proof->data.data + pos * 32, out, 32);
    }
  }
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

  if (ctx && ctx->proof) {
    int pos = gindex_indexOf(ctx->proof->witnesses, ctx->root_gindex + 1);
    if (pos >= 0 && ctx->proof->proof->data.data)
      memcpy(ctx->proof->proof->data.data + pos * 32, length_bytes, 32);
  }
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
  merkle_ctx_t ctx = {0};
  ctx.root_gindex  = 1;
  calc_leafes(&ctx, ob);
  if (parent) {
    ctx.proof       = parent->proof;
    ctx.root_gindex = ob.def->type == SSZ_TYPE_LIST ? parent->last_gindex * 2 : parent->last_gindex;
  }

  if (ctx.num_leafes == 1)
    set_leaf(ob, 0, out, NULL);
  else
    merkle_hash(&ctx, 0, 0, out);

  // Mix in length for variable-length types (lists and bit lists)
  if (ob.def->type == SSZ_TYPE_LIST || ob.def->type == SSZ_TYPE_BIT_LIST)
    mix_in_length(out, ssz_len(ob), &ctx);
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

typedef struct {
  bytes_t   witnesses_data;
  gindex_t* witnesses_gindex;
  uint32_t  witnesses_len;

  bytes_t   leafes_data;
  gindex_t* leafes_gindex;
  uint32_t  leafes_len;
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
    log_debug_full("%l: %x", start, bytes(out, 32));

    /*
    fprintf(stderr, "s: %llu ", start);
    print_hex(stderr, bytes(out, 32), " : ", "\n");
    */
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

bool ssz_verify_multi_merkle_proof(bytes_t proof_data, bytes_t leafes, gindex_t* gindex, bytes32_t out) {
  buffer_t witnesses_gindex  = {0};
  buffer_t calculated_gindex = {0};
  for (uint32_t i = 0; i < leafes.len / 32; i++)
    ssz_add_multi_merkle_proof(gindex[i], &witnesses_gindex, &calculated_gindex);
  /*
  fprintf(stderr, "_______\nwitnesses_gindex:\n");
  for (uint32_t i = 0; i < witnesses_gindex.data.len / sizeof(gindex_t); i++) {
    fprintf(stderr, "witness gindex: %llu\n", ((gindex_t*) witnesses_gindex.data.data)[i]);
  }

  fprintf(stderr, "_______\ncalculated_gindex:\n");
  for (uint32_t i = 0; i < calculated_gindex.data.len / sizeof(gindex_t); i++) {
    fprintf(stderr, "path gindex: %llu\n", ((gindex_t*) calculated_gindex.data.data)[i]);
  }
  fprintf(stderr, "_______\nvalues:\n");
  */

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
  uint32_t depth = log2_ceil((uint32_t) gindex2 + 1) - 1;
  if (depth > 63) {
    log_error("gindex depth is too large: %u", depth);
    return 0;
  }
  return (gindex1 << depth) | (gindex2 & ((1 << depth) - 1));
}
