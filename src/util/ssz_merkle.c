#include "crypto.h"
#include "ssz.h"
#if defined(_MSC_VER)
#include <intrin.h> // Include for MSVC intrinsics
#endif
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define BYTES_PER_CHUNK 32

typedef struct {
  char**    path;
  int       proof_gindex;
  uint32_t  path_len;
  buffer_t* proof;
} merkle_proot_ctx_t;

typedef struct {
  ssz_ob_t            ob;
  int                 max_depth;
  int                 num_used_leafes;
  int                 num_leafes;
  int                 proof_gindex;
  merkle_proot_ctx_t* proof;
} merkle_ctx_t;

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
static bool is_basic_type(const ssz_def_t* def) {
  return def->type == SSZ_TYPE_UINT || def->type == SSZ_TYPE_BOOLEAN || def->type == SSZ_TYPE_NONE;
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

      // check if the data are valid
      if (!ssz_is_valid(&res)) return (ssz_ob_t) {0};

      return res;
    }
    pos += len;
  }
  return res;
}

ssz_ob_t ssz_get(ssz_ob_t* ob, char* name) {
  if (ob->def->type != SSZ_TYPE_CONTAINER) return (ssz_ob_t) {0};
  for (int i = 0; i < ob->def->def.container.len; i++) {
    if (strcmp(ob->def->def.container.elements[i].name, name) == 0) return ssz_get_field(ob, i);
  }
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
      return (((only_used ? ssz_len(*ob) : def->def.vector.len) + 255) >> 8);
    case SSZ_TYPE_BIT_VECTOR:
      return (def->def.vector.len + 255) >> 8;
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
      uint32_t chunks  = (bit_len + 255) >> 8;
      if (index < chunks) {
        uint32_t rest = ob.bytes.len - (index << 5);
        memcpy(out, ob.bytes.data + (index << 5), rest > 32 ? 32 : rest);
        if (index == chunks - 1) {
          bit_len = bit_len % 256; // bits in the last chunk
          out[bit_len >> 3] &= ~(1 << bit_len % 8);
        }
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

      int offset = index * BYTES_PER_CHUNK;
      int len    = ob.bytes.len - offset;
      if (len > BYTES_PER_CHUNK) len = BYTES_PER_CHUNK;
      if (offset < ob.bytes.len)
        memcpy(out, ob.bytes.data + offset, len);
      break;
    }
    case SSZ_TYPE_UINT:
    case SSZ_TYPE_BOOLEAN:
      if (ob.bytes.len <= BYTES_PER_CHUNK)
        memcpy(out, ob.bytes.data, ob.bytes.len);
      break;
    case SSZ_TYPE_UNION:
      // TODO imoplement it
      break;
  }
}

static void merkle_hash(merkle_ctx_t* ctx, int index, int depth, uint8_t* out) {
  uint8_t temp[64];

  // how many leafes do we have from depth?
  int subtree_depth = ctx->max_depth - depth;
  int subtree_size  = 1 << subtree_depth;
  int gindex        = (1 << depth) + index; // current gindex
                                            //  char pre[100];
                                            // sprintf(pre, "## %d   (%d) :", gindex, ctx->proof_gindex);

  if (subtree_depth == 0) {
    set_leaf(ctx->ob, index, out, ctx->proof_gindex == gindex ? ctx : NULL);
    //    print_hex(stdout, bytes(out, 32), pre, "\n");

    //    if (ctx->proof && ctx->proof_gindex && (ctx->proof_gindex % 2 ? ctx->proof_gindex - 1 : ctx->proof_gindex + 1) == gindex)
    //      buffer_append(ctx->proof->proof, bytes(out, 32));

    return;
  }

#ifdef PRECOMPILE_ZERO_HASHES

  int gindex_subtree_left_leaf = gindex << subtree_depth;                          // gindex of first leaf of the current subtree
  int gindex_last_used_leaf    = (1 << ctx->max_depth) + ctx->num_used_leafes - 1; // gindex of last leaf of the used leafes
  if (gindex_last_used_leaf < gindex_subtree_left_leaf && subtree_depth < MAX_DEPTH) {
    cached_zero_hash(subtree_depth - 1, out);
    //    if (ctx->proof && ctx->proof_gindex && ctx->proof_gindex >> subtree_depth == gindex) {
    //      cached_zero_hash(subtree_depth - 2, temp);
    //      buffer_append(ctx->proof->proof, bytes(temp, 32));
    //    }
    return;
  }
#endif

  merkle_hash(ctx, index << 1, depth + 1, temp);
  merkle_hash(ctx, (index << 1) + 1, depth + 1, temp + 32);
  if (ctx->proof && ctx->proof_gindex && ctx->proof_gindex >> subtree_depth == gindex) {
    if ((ctx->proof_gindex >> (subtree_depth - 1)) % 2)
      buffer_append(ctx->proof->proof, bytes(temp, 32));
    else
      buffer_append(ctx->proof->proof, bytes(temp + 32, 32));
  }
  sha256(bytes(temp, 64), out);
  //    print_hex(stdout, bytes(out, 32), pre, "\n");
}

static inline void calc_leafes(merkle_ctx_t* ctx, ssz_ob_t ob) {
  ctx->max_depth       = log2_ceil(calc_num_leafes(&ob, false));
  ctx->num_used_leafes = calc_num_leafes(&ob, true);
  ctx->num_leafes      = 1 << ctx->max_depth;
  ctx->ob              = ob;
}

static void hash_tree_root(ssz_ob_t ob, uint8_t* out, merkle_ctx_t* parent) {
  memset(out, 0, 32);
  if (!ob.def) return;
  merkle_ctx_t ctx = {0};
  calc_leafes(&ctx, ob);

  // if proof is not null, we are in the proof generation phase
  if (parent && parent->proof && parent->proof->path_len && ob.def->type == SSZ_TYPE_CONTAINER) {
    int index = -1;
    for (int i = 0; i < ob.def->def.container.len; i++) {
      if (strcmp(ob.def->def.container.elements[i].name, parent->proof->path[0]) == 0) {
        index = i;
        break;
      }
    }
    if (index >= 0) {
      ctx.proof_gindex = (1 << ctx.max_depth) + index;
      parent->proof->path++;
      parent->proof->path_len--;
      ctx.proof               = parent->proof;
      ctx.proof->proof_gindex = ctx.proof->proof_gindex ? ssz_add_gindex(ctx.proof->proof_gindex, ctx.proof_gindex) : ctx.proof_gindex;
    }
  }
  if (ctx.num_leafes == 1)
    set_leaf(ob, 0, out, NULL);
  else
    merkle_hash(&ctx, 0, 0, out);

  // mix_in_length
  if (ob.def->type == SSZ_TYPE_LIST || ob.def->type == SSZ_TYPE_BIT_LIST) {
    uint8_t length[32] = {0};
    uint64_to_le(length, (uint64_t) ssz_len(ob));
    sha256_merkle(bytes(out, 32), bytes(length, 32), out);
  }
}

void ssz_hash_tree_root(ssz_ob_t ob, uint8_t* out) {
  hash_tree_root(ob, out, NULL);
}

bool ssz_create_proof(ssz_ob_t root, char** path, uint32_t path_len, buffer_t* proof, uint32_t* gindex) {
  bytes32_t          tmp;
  merkle_proot_ctx_t proof_ctx = {
      .path         = path,
      .path_len     = path_len,
      .proof_gindex = 0,
      .proof        = proof};

  merkle_ctx_t ctx = {0};
  ctx.proof        = &proof_ctx;

  hash_tree_root(root, tmp, &ctx);

  *gindex = proof_ctx.proof_gindex;
  return true;
}

static uint32_t get_depth(uint32_t gindex) {
  uint32_t depth = 0;
  while (gindex > 1) {
    gindex = gindex >> 1;
    depth++;
  }
  return depth;
}

uint32_t ssz_get_gindex(ssz_ob_t* ob, const char* name) {
  uint32_t num_leafes = calc_num_leafes(ob, false);
  uint32_t index      = 0xffffffff;
  for (int i = 0; i < ob->def->def.container.len; i++) {
    if (strcmp(ob->def->def.container.elements[i].name, name) == 0) {
      index = i;
      break;
    }
  }
  if (index == 0xffffffff) return 0;
  uint32_t depth = 0;
  while (num_leafes > 1) {
    num_leafes = num_leafes >> 1;
    depth++;
  }
  return index + (1 << depth);
}

void ssz_verify_merkle_proof(bytes_t proof_data, bytes32_t leaf, uint32_t gindex, bytes32_t out) {
  memset(out, 0, 32);
  uint32_t depth = get_depth(gindex);
  uint32_t index = gindex % (1 << depth);

  // check potential extra data to make sure they are all zero
  if (proof_data.len >> 5 > depth) {
    uint32_t num_extra = (proof_data.len >> 5) - depth;
    for (uint32_t i = 0; i < num_extra; i++) {
      if (!bytes_all_zero(proof_data)) return;
    }
  }

  if ((proof_data.len >> 5) < depth) return;

  memcpy(out, leaf, 32);

  for (uint32_t i = 0; i < depth; i++) {
    if ((index / (1 << i)) % 2 == 1)
      sha256_merkle(bytes_slice(proof_data, (i << 5), 32), bytes(out, 32), out);
    else
      sha256_merkle(bytes(out, 32), bytes_slice(proof_data, (i << 5), 32), out);
  }
}

uint32_t ssz_add_gindex(uint32_t gindex1, uint32_t gindex2) {
  uint32_t depth = get_depth(gindex1) + 1;
  return (gindex1 << depth) | (gindex2 & ((1 << depth) - 1));
}
