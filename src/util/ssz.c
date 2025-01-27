#include "ssz.h"
#include "crypto.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define BYTES_PER_CHUNK 32

// predefined types
const ssz_def_t ssz_uint8     = SSZ_UINT("", 1);
const ssz_def_t ssz_bytes32   = SSZ_BYTES32("bytes32");
const ssz_def_t ssz_bls_pubky = SSZ_BYTE_VECTOR("bls_pubky", 48);

/**
 * Liefert ceil(log2(val)), also den kleinsten n,
 * so dass 2^n >= val.
 *
 * Beispielwerte:
 *  val = 1   => 0  (denn 2^0 = 1)
 *  val = 2   => 1
 *  val = 3   => 2
 *  val = 4   => 2
 *  val = 5   => 3
 *  usw.
 *
 * Hinweis: Für val=0 ist das mathematisch nicht definiert.
 *          Hier returnen wir einfach 0 oder man könnte
 *          einen Assert machen, je nachdem.
 */
static inline uint32_t log2_ceil(uint32_t val) {
  if (val < 2) return 0;

  // floor(log2(val)):
  uint32_t floor_log2 = 31 - __builtin_clz(val);
  return (val & (val - 1)) == 0 ? floor_log2 : floor_log2 + 1;
}

// checks if a definition has a dynamic length
static bool is_dynamic(const ssz_def_t* def) {
  if (def->type == SSZ_TYPE_CONTAINER) {
    for (int i = 0; i < def->def.container.len; i++) {
      if (is_dynamic(def->def.container.elements + i))
        return true;
    }
  }

  return def->type == SSZ_TYPE_LIST || def->type == SSZ_TYPE_BIT_LIST || def->type == SSZ_TYPE_UNION;
}

// finds a definition by name within a container or a union
static const ssz_def_t* find_def(const ssz_def_t* def, char* name) {
  if (def->type != SSZ_TYPE_CONTAINER) return NULL;
  for (int i = 0; i < def->def.container.len; i++) {
    if (strcmp(def->def.container.elements[i].name, name) == 0) return def->def.container.elements + i;
  }
  return NULL;
}

// gets the length of a type for the fixed part.
static size_t get_fixed_length(const ssz_def_t* def) {
  if (is_dynamic(def))
    return 4;
  switch (def->type) {
    case SSZ_TYPE_UINT:
      return def->def.uint.len;
    case SSZ_TYPE_BOOLEAN:
      return 1;
    case SSZ_TYPE_CONTAINER: {
      size_t len = 0;
      for (int i = 0; i < def->def.container.len; i++)
        len += get_fixed_length(def->def.container.elements + i);
      return len;
    }
    case SSZ_TYPE_VECTOR:
      return def->def.vector.len * get_fixed_length(def->def.vector.type);
    case SSZ_TYPE_BIT_VECTOR:
      return (def->def.vector.len + 7) >> 3;
    default:
      return 0;
  }
}

static bool check_data(ssz_ob_t* ob) {
  switch (ob->def->type) {
    case SSZ_TYPE_BOOLEAN:
      return ob->bytes.len == 1 && ob->bytes.data[0] < 2;
    case SSZ_TYPE_VECTOR:
      return ob->bytes.len == ob->def->def.vector.len * get_fixed_length(ob->def->def.vector.type);
    case SSZ_TYPE_LIST:
      if (is_dynamic(ob->def->def.vector.type)) {
        if (ob->bytes.len == 0) return true;
        if (ob->bytes.len < 4) return false;
        uint32_t first_offset = uint32_from_le(ob->bytes.data);
        if (first_offset >= ob->bytes.len || first_offset < 4) return false;
        uint32_t offset = first_offset;
        for (int i = 4; i < first_offset; i += 4) {
          uint32_t next_offset = uint32_from_le(ob->bytes.data + i);
          if (next_offset >= ob->bytes.len || next_offset < offset) return false;
          offset = next_offset;
        }
        return true;
      }
      return ob->bytes.len % get_fixed_length(ob->def->def.vector.type) == 0 && ob->bytes.len <= ob->def->def.vector.len * get_fixed_length(ob->def->def.vector.type);
    case SSZ_TYPE_BIT_VECTOR:
      return ob->bytes.len == (ob->def->def.vector.len + 7) >> 3;
    case SSZ_TYPE_BIT_LIST:
      return ob->bytes.len <= (ob->def->def.vector.len + 7) >> 3;
    case SSZ_TYPE_UINT:
      return ob->bytes.len == ob->def->def.uint.len;
    case SSZ_TYPE_CONTAINER:
      return ob->bytes.len >= get_fixed_length(ob->def);
    case SSZ_TYPE_UNION:
      return ob->bytes.len > 0 && ob->bytes.data[0] < ob->def->def.container.len;
    default:
      return true;
  }
}

ssz_ob_t ssz_union(ssz_ob_t ob) {
  ssz_ob_t res = {0};
  // check if the object is valid
  if (ob.def->type != SSZ_TYPE_UNION || !ob.bytes.data || !ob.bytes.len)
    return res;

  const uint8_t index = ob.bytes.data[0];
  if (index >= ob.def->def.container.len) return res;
  res.def = ob.def->def.container.elements + index;
  if (res.def->type == SSZ_TYPE_NONE) return res;
  res.bytes = bytes(ob.bytes.data + 1, ob.bytes.len - 1);
  return res;
}

// gets the value of a field from a container
ssz_ob_t ssz_get(ssz_ob_t* ob, char* name) {
  ssz_ob_t res = {0};
  // check if the object is valid
  if (!ob || !name || !ob->def || ob->def->type != SSZ_TYPE_CONTAINER || !ob->bytes.data || !ob->bytes.len)
    return res;

  // iterate over the fields of the container
  size_t           pos = 0;
  const ssz_def_t* def = NULL;
  for (int i = 0; i < ob->def->def.container.len; i++) {
    def        = ob->def->def.container.elements + i;
    size_t len = get_fixed_length(def);

    if (pos + len > ob->bytes.len) return res;

    if (strcmp(def->name, name) == 0) {
      res.def = def;
      if (is_dynamic(def)) {
        uint32_t offset = uint32_from_le(ob->bytes.data + pos);
        if (offset > ob->bytes.len) return res;
        res.bytes.data = ob->bytes.data + offset;
        res.bytes.len  = ob->bytes.len - offset;
        pos += len;

        // find next offset
        for (int n = i + 1; n < ob->def->def.container.len; n++) {
          if (is_dynamic(ob->def->def.container.elements + n)) {
            if (pos + 4 > ob->bytes.len) return (ssz_ob_t) {0};

            offset = uint32_from_le(ob->bytes.data + pos);
            if (offset < ob->bytes.len)
              res.bytes.len = ob->bytes.data + offset - res.bytes.data;
            break;
          }
          pos += get_fixed_length(ob->def->def.container.elements + n);
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
      if (!check_data(&res)) return (ssz_ob_t) {0};

      return res;
    }
    pos += len;
  }
  return res;
}

uint32_t ssz_len(ssz_ob_t ob) {
  switch (ob.def->type) {
    case SSZ_TYPE_VECTOR: return ob.def->def.vector.len;
    case SSZ_TYPE_LIST:
      return ob.bytes.len > 4 && is_dynamic(ob.def->def.vector.type)
                 ? uint32_from_le(ob.bytes.data) / 4
                 : ob.bytes.len / get_fixed_length(ob.def->def.vector.type);
    case SSZ_TYPE_BIT_VECTOR:
    case SSZ_TYPE_BIT_LIST:
      return ob.bytes.len * 8;
    default: return 0;
  }
}

ssz_ob_t ssz_at(ssz_ob_t ob, uint32_t index) {
  ssz_ob_t res = {0};

  if (!ob.bytes.data || !ob.bytes.len || !ob.def)
    return res;

  uint32_t len = ssz_len(ob);
  if (index >= len)
    return res;

  if (is_dynamic(ob.def->def.vector.type)) {
    uint32_t offset     = uint32_from_le(ob.bytes.data + index * 4);
    uint32_t end_offset = index < len - 1 ? uint32_from_le(ob.bytes.data + (index + 1) * 4) : ob.bytes.len;
    return (ssz_ob_t) {
        .def   = ob.def->def.vector.type,
        .bytes = bytes(ob.bytes.data + offset, end_offset - offset)};
  }

  size_t element_size = get_fixed_length(ob.def->def.vector.type);
  if (element_size * (index + 1) > ob.bytes.len)
    return res;

  return (ssz_ob_t) {
      .def   = ob.def->def.vector.type,
      .bytes = bytes(
          ob.bytes.data + index * element_size,
          element_size)};
}

void ssz_dump(FILE* f, ssz_ob_t ob, bool include_name, int intend) {
  const ssz_def_t* def        = ob.def;
  char             close_char = '\0';
  for (int i = 0; i < intend; i++) fprintf(f, " ");
  if (!def) {
    fprintf(f, "<invalid>");
    return;
  }
  if (include_name) fprintf(f, "\"%s\":", def->name);
  switch (def->type) {
    case SSZ_TYPE_UINT:
      switch (def->def.uint.len) {
        case 1: fprintf(f, "%d", ob.bytes.data[0]); break;
        case 2: fprintf(f, "%d", uint16_from_le(ob.bytes.data)); break;
        case 4: fprintf(f, "%d", uint32_from_le(ob.bytes.data)); break;
        case 8: fprintf(f, "%" PRIu64, uint64_from_le(ob.bytes.data)); break;
        default: print_hex(f, ob.bytes, "\"0x", "\"");
      }
      break;
    case SSZ_TYPE_NONE: fprintf(f, "null"); break;
    case SSZ_TYPE_BOOLEAN: fprintf(f, "%s", ob.bytes.data[0] ? "true" : "false"); break;
    case SSZ_TYPE_CONTAINER: {
      close_char = '}';
      fprintf(f, "{\n");
      for (int i = 0; i < def->def.container.len; i++) {
        ssz_dump(f, ssz_get(&ob, (char*) def->def.container.elements[i].name), true, intend + 2);
        if (i < def->def.container.len - 1) fprintf(f, ",\n");
      }
      break;
    }
    case SSZ_TYPE_BIT_VECTOR:
    case SSZ_TYPE_BIT_LIST:
      print_hex(f, ob.bytes, "\"0x", "\"");
      break;
    case SSZ_TYPE_VECTOR:
    case SSZ_TYPE_LIST: {
      if (def->def.vector.type->type == SSZ_TYPE_UINT && def->def.vector.type->def.uint.len == 1)
        print_hex(f, ob.bytes, "\"0x", "\"");
      else {
        fprintf(f, "[\n");
        for (int i = 0; i < ssz_len(ob); i++) {
          ssz_dump(f, ssz_at(ob, i), false, intend + 2);
          if (i < ssz_len(ob) - 1) fprintf(f, ",\n");
        }
        close_char = ']';
      }
      break;
    }
    case SSZ_TYPE_UNION:
      if (ob.bytes.len == 0 || ob.bytes.data[0] >= def->def.container.len)
        fprintf(f, "null");
      else if (def->def.container.elements[ob.bytes.data[0]].type == SSZ_TYPE_NONE)
        fprintf(f, "{\"selector\":%d,\"value\":null}", ob.bytes.data[0]);
      else {
        fprintf(f, "{ \"selector\":%d, \"value\":", ob.bytes.data[0]);
        ssz_dump(f, ssz_ob(def->def.container.elements[ob.bytes.data[0]], bytes(ob.bytes.data + 1, ob.bytes.len - 1)), false, intend + 2);
        close_char = '}';
      }
      break;
    default: fprintf(f, "%s", ob.bytes.data); break;
  }

  if (close_char) {
    fprintf(f, "\n");
    for (int i = 0; i < intend; i++) fprintf(f, " ");
    fprintf(f, "%c", close_char);
  }
}

void ssz_add_bytes(ssz_builder_t* buffer, char* name, bytes_t data) {
  const ssz_def_t* def = find_def(buffer->def, name);
  if (!def) return;
  buffer_t* bytes        = &(buffer->fixed);
  size_t    fixed_length = 0;

  if (is_dynamic(def)) {
    size_t offset = 0;
    for (int i = 0; i < buffer->def->def.container.len; i++)
      offset += get_fixed_length(buffer->def->def.container.elements + i);
    ssz_add_uint32(buffer, offset + buffer->dynamic.data.len);
    bytes = &(buffer->dynamic);
  }
  else
    fixed_length = get_fixed_length(buffer->def);

  buffer_append(bytes, data);
  if (fixed_length && bytes->data.len < fixed_length)
    buffer_append(bytes, bytes(NULL, fixed_length - bytes->data.len));
}

void ssz_add_uint64(ssz_builder_t* buffer, uint64_t value) {
  uint8_t tmp[8];
  tmp[0] = value & 0xFF;
  tmp[1] = (value >> 8) & 0xFF;
  tmp[2] = (value >> 16) & 0xFF;
  tmp[3] = (value >> 24) & 0xFF;
  tmp[4] = (value >> 32) & 0xFF;
  tmp[5] = (value >> 40) & 0xFF;
  tmp[6] = (value >> 48) & 0xFF;
  tmp[7] = (value >> 56) & 0xFF;

  buffer_append(&buffer->fixed, bytes(tmp, 8));
}

void ssz_add_uint32(ssz_builder_t* buffer, uint32_t value) {
  uint8_t tmp[4];
  tmp[0] = value & 0xFF;
  tmp[1] = (value >> 8) & 0xFF;
  tmp[2] = (value >> 16) & 0xFF;
  tmp[3] = (value >> 24) & 0xFF;
  buffer_append(&buffer->fixed, bytes(tmp, 4));
}

void ssz_add_uint16(ssz_builder_t* buffer, uint16_t value) {
  uint8_t tmp[2];
  tmp[0] = value & 0xFF;
  tmp[1] = value >> 8 & 0xFF;
  buffer_append(&buffer->fixed, bytes(tmp, 2));
}

void ssz_add_uint8(ssz_builder_t* buffer, uint8_t value) {
  buffer_append(&buffer->fixed, bytes(&value, 1));
}

void ssz_buffer_free(ssz_builder_t* buffer) {
  buffer_free(&buffer->fixed);
  buffer_free(&buffer->dynamic);
}

ssz_ob_t ssz_builder_to_bytes(ssz_builder_t* buffer) {
  buffer_append(&buffer->fixed, buffer->dynamic.data);
  buffer_free(&buffer->dynamic);
  return (ssz_ob_t) {.def = buffer->def, .bytes = buffer->fixed.data};
}

static bool is_basic_type(const ssz_def_t* def) {
  return def->type == SSZ_TYPE_UINT || def->type == SSZ_TYPE_BOOLEAN || def->type == SSZ_TYPE_NONE;
}

// ------------------------ merkle --------------------------------

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
        return (def->def.vector.len * get_fixed_length(def->def.vector.type) + 31) >> 5;
      else
        return def->def.vector.len;
    case SSZ_TYPE_LIST: {
      uint32_t len = only_used ? ssz_len(*ob) : def->def.vector.len;
      if (is_basic_type(def->def.vector.type))
        return (len * get_fixed_length(def->def.vector.type) + 31) >> 5;
      else
        return len;
    }
    case SSZ_TYPE_BIT_LIST:
      return (def->def.vector.len + 256) >> 8;
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
    case SSZ_TYPE_VECTOR:
    case SSZ_TYPE_LIST:
    case SSZ_TYPE_BIT_LIST:
    case SSZ_TYPE_BIT_VECTOR: {

      // handle complex types
      if ((def->type == SSZ_TYPE_VECTOR || def->type == SSZ_TYPE_LIST) && !is_basic_type(def->def.vector.type)) {
        uint32_t len = ssz_len(ob);
        if (index < len)
          hash_tree_root(ssz_at(ob, index), out, ctx);
        //        else if (index == len && def->type == SSZ_TYPE_LIST)
        //          *out = 1;
        return;
      }

      int offset = index * BYTES_PER_CHUNK;
      int len    = ob.bytes.len - offset;
      if (len > BYTES_PER_CHUNK) len = BYTES_PER_CHUNK;
      if (offset < ob.bytes.len)
        memcpy(out, ob.bytes.data + offset, len);
      if (/*def->type == SSZ_TYPE_LIST || */ def->type == SSZ_TYPE_BIT_LIST) {
        if (offset < ob.bytes.len && len < BYTES_PER_CHUNK)
          out[len] = 1;
        else if (offset == ob.bytes.len)
          out[0] = 1;
      }
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
                                            //  sprintf(pre, "## %d   (%d) :", gindex, ctx->proof_gindex);

  if (subtree_depth == 0) {
    set_leaf(ctx->ob, index, out, ctx->proof_gindex == gindex ? ctx : NULL);
    //  if (ctx->proof_gindex) print_hex(stdout, bytes(out, 32), pre, "\n");

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
  // if (ctx->proof_gindex) print_hex(stdout, bytes(out, 32), pre, "\n");
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

  if (ctx.num_leafes == 1) {
    set_leaf(ob, 0, out, NULL);
    return;
  }
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

  merkle_hash(&ctx, 0, 0, out);

  // mix_in_length
  if (ob.def->type == SSZ_TYPE_LIST) {
    uint8_t length[32];
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

bool ssz_is_type(ssz_ob_t* ob, const ssz_def_t* def) {
  if (!ob || !ob->def || !def) return false;
  if (ob->def == def) return true;
  if (ob->def->type == SSZ_TYPE_UNION) {
    ssz_ob_t union_ob = ssz_union(*ob);
    return ssz_is_type(&union_ob, def);
  }
  if (ob->def->type == SSZ_TYPE_CONTAINER) return ob->def->def.container.elements == def;
  switch (def->type) {
    case SSZ_TYPE_UINT:
      return def->type == SSZ_TYPE_UINT && ob->def->def.uint.len == def->def.uint.len;
    case SSZ_TYPE_BOOLEAN:
      return def->type == SSZ_TYPE_BOOLEAN;
    case SSZ_TYPE_BIT_LIST:
      return def->type == SSZ_TYPE_BIT_LIST && ob->def->def.uint.len == def->def.uint.len;
    case SSZ_TYPE_BIT_VECTOR:
      return def->type == SSZ_TYPE_BIT_VECTOR && ob->def->def.uint.len == def->def.uint.len;
    case SSZ_TYPE_CONTAINER:
      return ob->def->def.container.elements == def;
    case SSZ_TYPE_VECTOR: {
      ssz_ob_t el = {.def = ob->def->def.vector.type, .bytes = ob->bytes};
      return def->type == SSZ_TYPE_VECTOR && ob->def->def.uint.len == def->def.uint.len && ssz_is_type(&el, def->def.vector.type);
    }
    case SSZ_TYPE_LIST: {
      ssz_ob_t el = {.def = ob->def->def.vector.type, .bytes = ob->bytes};
      return def->type == SSZ_TYPE_LIST && ob->def->def.uint.len == def->def.uint.len && ssz_is_type(&el, def->def.vector.type);
    }
    default:
      return false;
  }
}
