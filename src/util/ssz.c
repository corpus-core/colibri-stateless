#include "ssz.h"
#include "crypto.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define BYTES_PER_CHUNK 32

const ssz_def_t ssz_uint8 = SSZ_UINT("", 1);

static bool is_dynamic(const ssz_def_t* def) {
  if (def->type == SSZ_TYPE_CONTAINER) {
    for (int i = 0; i < def->def.container.len; i++) {
      if (is_dynamic(def->def.container.elements + i))
        return true;
    }
  }

  return def->type == SSZ_TYPE_LIST || def->type == SSZ_TYPE_BIT_LIST || def->type == SSZ_TYPE_UNION;
}

static const ssz_def_t* find_def(const ssz_def_t* def, char* name) {
  if (def->type != SSZ_TYPE_CONTAINER) return NULL;
  for (int i = 0; i < def->def.container.len; i++) {
    if (strcmp(def->def.container.elements[i].name, name) == 0) return def->def.container.elements + i;
  }
  return NULL;
}

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

ssz_ob_t ssz_get(ssz_ob_t* ob, char* name) {
  ssz_ob_t res = {0};
  if (!ob || !name || ob->def->type != SSZ_TYPE_CONTAINER || !ob->bytes.data || !ob->bytes.len)
    return res;

  size_t           pos = 0;
  const ssz_def_t* def = NULL;
  for (int i = 0; i < ob->def->def.container.len; i++) {
    def        = ob->def->def.container.elements + i;
    size_t len = get_fixed_length(def);

    if (pos + len > ob->bytes.len)
      return res;

    if (strcmp(def->name, name) == 0) {
      res.def = def;
      if (is_dynamic(def)) {
        uint32_t offset = uint32_from_le(ob->bytes.data + pos);
        if (offset >= ob->bytes.len) return res;
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
        if (res.bytes.len && def->def.container.len < res.bytes.data[0]) {
          res.def = def->def.container.elements + res.bytes.data[0];
          res.bytes.len--;
          res.bytes.data++;
        }
        else {
          // invalid union
          res.def        = NULL;
          res.bytes.len  = 0;
          res.bytes.data = NULL;
        }
      }
      return res;
    }
    pos += len;
  }
  return res;
}

uint32_t ssz_len(ssz_ob_t ob) {
  switch (ob.def->type) {
    case SSZ_TYPE_VECTOR: return ob.def->def.vector.len;
    case SSZ_TYPE_LIST: return ob.bytes.len / get_fixed_length(ob.def->def.vector.type);
    case SSZ_TYPE_CONTAINER: return ob.def->def.container.len;
    default: return 1;
  }
}

ssz_ob_t ssz_at(ssz_ob_t ob, uint32_t index) {
  ssz_ob_t res = {0};

  if (!ob.bytes.data || !ob.bytes.len || !ob.def)
    return res;

  uint32_t len = ssz_len(ob);
  if (index >= len)
    return res;

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
  if (include_name) fprintf(f, "\"%s\":", def->name);
  switch (def->type) {
    case SSZ_TYPE_UINT:
      switch (def->def.uint.len) {
        case 1: fprintf(f, "%d", ob.bytes.data[0]); break;
        case 2: fprintf(f, "%d", uint16_from_le(ob.bytes.data)); break;
        case 4: fprintf(f, "%d", uint32_from_le(ob.bytes.data)); break;
        case 8: fprintf(f, "%llu", uint64_from_le(ob.bytes.data)); break;
        default: print_hex(f, ob.bytes, "\"0x", "\"");
      }
      break;
    case SSZ_TYPE_BOOLEAN: fprintf(f, "%s", ob.bytes.data[0] ? "true" : "false"); break;
    case SSZ_TYPE_CONTAINER: {
      close_char = '}';
      fprintf(f, "{\n");
      for (int i = 0; i < def->def.container.len; i++) {
        ssz_dump(f, ssz_get(&ob, def->def.container.elements[i].name), true, intend + 2);
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
      if (def->def.vector.type->type == SSZ_TYPE_UINT && def->def.vector.type->def.uint.len == 1) {
        print_hex(f, ob.bytes, "\"0x", "\"");
      }
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
  bytes_buffer_t* bytes        = &(buffer->fixed);
  size_t          fixed_length = 0;

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

static int calc_num_leafes(const ssz_def_t* def) {
  switch (def->type) {
    case SSZ_TYPE_CONTAINER:
      return def->def.container.len;
    case SSZ_TYPE_VECTOR:
      return (def->def.vector.len * get_fixed_length(def->def.vector.type) + 31) >> 5;
    case SSZ_TYPE_LIST:
      return ((def->def.vector.len + 1) * get_fixed_length(def->def.vector.type) + 31) >> 5;
    case SSZ_TYPE_BIT_LIST:
      return (def->def.vector.len + 31) >> 5;
    case SSZ_TYPE_BIT_VECTOR:
      return ((def->def.vector.len + 1) + 31) >> 5;
    default:
      return 1;
  }
}

static void set_leaf(ssz_ob_t ob, int index, uint8_t* out) {
  memset(out, 0, 32);
  const ssz_def_t* def = ob.def;
  switch (def->type) {
    case SSZ_TYPE_CONTAINER: {
      if (index < def->def.container.len)
        ssz_hash_tree_root(ssz_get(&ob, def->def.container.elements[index].name), out);
      break;
    }
    case SSZ_TYPE_VECTOR:
    case SSZ_TYPE_LIST:
    case SSZ_TYPE_BIT_LIST:
    case SSZ_TYPE_BIT_VECTOR: {
      int offset = index * BYTES_PER_CHUNK;
      int len    = ob.bytes.len - offset;
      if (len > BYTES_PER_CHUNK) len = BYTES_PER_CHUNK;
      if (offset < ob.bytes.len)
        memcpy(out, ob.bytes.data + offset, len);
      if (def->type == SSZ_TYPE_LIST || def->type == SSZ_TYPE_BIT_LIST) {
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

static void merkle_hash(ssz_ob_t ob, int index, int depth, int num_leafes, uint8_t* out) {
  uint8_t temp[64];

  if (1 << depth >= num_leafes) {
    set_leaf(ob, index * 2, temp);
    set_leaf(ob, index * 2 + 1, temp + 32);
  }
  else {
    merkle_hash(ob, index * 2, depth + 1, num_leafes, temp);
    merkle_hash(ob, index * 2 + 1, depth + 1, num_leafes, temp + 32);
  }

  sha256(bytes(temp, 64), out);
}

void ssz_hash_tree_root(ssz_ob_t ob, uint8_t* out) {
  memset(out, 0, 32);
  int num_leafes = calc_num_leafes(ob.def);
  if (num_leafes == 1)
    set_leaf(ob, 0, out);
  else
    merkle_hash(ob, 0, 1, num_leafes, out);
}
