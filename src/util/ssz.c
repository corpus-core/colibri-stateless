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

#include "ssz.h"
#include "crypto.h"
#include "json.h"
#include "state.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// predefined types
const ssz_def_t ssz_uint8       = SSZ_UINT("", 1);
const ssz_def_t ssz_uint32_def  = SSZ_UINT("", 4);
const ssz_def_t ssz_uint64_def  = SSZ_UINT("", 8);
const ssz_def_t ssz_uint256_def = SSZ_UINT("", 32);
const ssz_def_t ssz_bytes32     = SSZ_BYTES32("bytes32");
const ssz_def_t ssz_bls_pubky   = SSZ_BYTE_VECTOR("bls_pubky", 48);
const ssz_def_t ssz_bytes_list  = SSZ_BYTES("bytes", 1024 << 8);
const ssz_def_t ssz_string_def  = SSZ_BYTES("bytes", 1024 << 8);
const ssz_def_t ssz_none        = SSZ_NONE;

/**
 * Checks if a type is a basic SSZ type.
 * Basic types are: uint, boolean, and none.
 * These types don't have nested structure and are serialized as-is.
 */
static bool is_basic_type(const ssz_def_t* def) {
  return def->type == SSZ_TYPE_UINT || def->type == SSZ_TYPE_BOOLEAN || def->type == SSZ_TYPE_NONE;
}

// checks if a definition has a dynamic length
bool ssz_is_dynamic(const ssz_def_t* def) {
  if (def->type == SSZ_TYPE_CONTAINER) {
    for (int i = 0; i < def->def.container.len; i++) {
      if (ssz_is_dynamic(def->def.container.elements + i))
        return true;
    }
  }

  return def->type == SSZ_TYPE_LIST || def->type == SSZ_TYPE_BIT_LIST || def->type == SSZ_TYPE_UNION;
}

size_t ssz_fixed_length(const ssz_def_t* def) {
  if (ssz_is_dynamic(def))
    return SSZ_OFFSET_SIZE;
  switch (def->type) {
    case SSZ_TYPE_UINT:
      return def->def.uint.len;
    case SSZ_TYPE_BOOLEAN:
      return 1;
    case SSZ_TYPE_CONTAINER: {
      size_t len = 0;
      for (int i = 0; i < def->def.container.len; i++)
        len += ssz_fixed_length(def->def.container.elements + i);
      return len;
    }
    case SSZ_TYPE_VECTOR:
      return def->def.vector.len * ssz_fixed_length(def->def.vector.type);
    case SSZ_TYPE_BIT_VECTOR:
      return (def->def.vector.len + 7) >> 3;
    default:
      return 0;
  }
}

/**
 * Helper function for error handling in validation.
 * Prints error message and returns false.
 */
static bool failure(const char* fnt) {
  fbprintf(stderr, "Invalid %s\n", fnt);
  return false;
}

#define THROW_INVALID(fmt, ...)                       \
  do {                                                \
    if (!state) return failure(fmt);                  \
    buffer_t buf = {0};                               \
    state->error = bprintf(&buf, fmt, ##__VA_ARGS__); \
    return failure(fmt);                              \
  } while (0)
#define RETURN_VALID_IF(condition, msg, ...)                       \
  do {                                                             \
    if (!(condition)) {                                            \
      if (state) state->error = bprintf(NULL, msg, ##__VA_ARGS__); \
      return failure(msg);                                         \
    }                                                              \
    return true;                                                   \
  } while (0)

bool ssz_is_valid(ssz_ob_t ob, bool recursive, c4_state_t* state) {
  // Global size limit to prevent integer overflows in multiplications
  if (ob.bytes.len > SSZ_MAX_BYTES)
    THROW_INVALID("SSZ object exceeds maximum size");

  switch (ob.def->type) {
    case SSZ_TYPE_BOOLEAN:
      // Boolean must be exactly 1 byte with value 0 or 1
      if (ob.bytes.len != 1 || ob.bytes.data[0] > 1) THROW_INVALID("invalid boolean value");
      return true;
    case SSZ_TYPE_VECTOR: {
      // Vector has fixed length, bytelength must be exactly len * element_size
      size_t element_size = ssz_fixed_length(ob.def->def.vector.type);
      size_t expected_len = ob.def->def.vector.len * element_size;
      if (ob.bytes.len != expected_len) THROW_INVALID("Invalid bytelength for vector");
      if (recursive && ob.def->def.vector.type->type != SSZ_TYPE_UINT) {
        for (int i = 0; i < ob.def->def.vector.len; i++) {
          ssz_ob_t el = ssz_at(ob, i);
          if (!ssz_is_valid(el, recursive, state)) return false;
        }
      }
      return true;
    }
    case SSZ_TYPE_LIST: {
      // List with dynamic elements: offset array followed by data
      // List with fixed elements: concatenated elements
      if (ssz_is_dynamic(ob.def->def.vector.type)) {
        if (ob.bytes.len == 0) return true;
        if (ob.bytes.len < SSZ_OFFSET_SIZE) THROW_INVALID("Invalid bytelength for list");
        uint32_t first_offset = uint32_from_le(ob.bytes.data);
        // First offset must be aligned and within bounds
        if (first_offset >= ob.bytes.len || first_offset < SSZ_OFFSET_SIZE || first_offset % SSZ_OFFSET_SIZE != 0)
          THROW_INVALID("Invalid first offset for list");
        uint32_t offset = first_offset;
        for (int i = SSZ_OFFSET_SIZE; i < first_offset; i += SSZ_OFFSET_SIZE) {
          uint32_t next_offset = uint32_from_le(ob.bytes.data + i);
          if (next_offset >= ob.bytes.len || next_offset < offset) THROW_INVALID("Invalid  offset for list");
          if (recursive && !ssz_is_valid(ssz_ob(*ob.def->def.vector.type, bytes(ob.bytes.data + offset, next_offset - offset)), recursive, state)) return false;
          offset = next_offset;
        }
        if (recursive && !ssz_is_valid(ssz_ob(*ob.def->def.vector.type, bytes(ob.bytes.data + offset, ob.bytes.len - offset)), recursive, state)) return false;
        return true;
      }
      // Fixed-size elements: total length must be multiple of element size
      size_t fixed_length = ssz_fixed_length(ob.def->def.vector.type);
      if (fixed_length == 0 || ob.bytes.len % fixed_length != 0 ||
          ob.bytes.len > ob.def->def.vector.len * fixed_length) THROW_INVALID("Invalid length for list");
      if (recursive && ob.def->type != SSZ_TYPE_UINT) {
        for (int i = 0; i < ob.bytes.len; i += fixed_length) {
          if (!ssz_is_valid(ssz_ob(*ob.def->def.vector.type, bytes(ob.bytes.data + i, fixed_length)), recursive, state)) return false;
        }
      }
      return true;
    }
    case SSZ_TYPE_BIT_VECTOR:
      // Bit vector length must match definition exactly
      RETURN_VALID_IF(ob.bytes.len == (ob.def->def.vector.len + 7) >> 3, "Invalid length for bit vector");
    case SSZ_TYPE_BIT_LIST:
      // Bit list length can be up to max length
      RETURN_VALID_IF(ob.bytes.len <= (ob.def->def.vector.len + 7) >> 3, "Invalid length for bit list");
    case SSZ_TYPE_UINT:
      // Uint length must match definition
      RETURN_VALID_IF(ob.bytes.len == ob.def->def.uint.len, "Invalid length for uint");
    case SSZ_TYPE_CONTAINER: {
      // Container with mixed fixed/dynamic fields
      if (ssz_is_dynamic(ob.def) ? (ob.bytes.len < ssz_fixed_length(ob.def)) : (ob.bytes.len != ssz_fixed_length(ob.def))) THROW_INVALID("Invalid length for container");
      if (recursive) {
        ssz_ob_t last_ob     = {0};
        uint32_t last_offset = 0;
        uint32_t pos         = 0;
        for (int i = 0; i < ob.def->def.container.len; i++) {
          const ssz_def_t* def = ob.def->def.container.elements + i;
          if (ssz_is_dynamic(def)) {
            uint32_t offset = uint32_from_le(ob.bytes.data + pos);
            // Validate offset: must be within bounds, after fixed portion, and monotonically increasing
            if (offset > ob.bytes.len || offset < pos + SSZ_OFFSET_SIZE || (last_offset > 0 && last_offset > offset))
              THROW_INVALID("Invalid offset for container");
            if (last_ob.def) {
              last_ob.bytes = bytes(ob.bytes.data + last_offset, offset - last_offset);
              if (!ssz_is_valid(last_ob, recursive, state)) return false;
            }
            last_ob.def = def;
            last_offset = offset;
            pos += SSZ_OFFSET_SIZE;
          }
          else {
            uint32_t len = ssz_fixed_length(def);
            if (!ssz_is_valid(ssz_ob(*def, bytes(ob.bytes.data + pos, len)), recursive, state)) return false;
            pos += len;
          }
        }
        if (last_ob.def) {
          last_ob.bytes = bytes(ob.bytes.data + last_offset, ob.bytes.len - last_offset);
          if (!ssz_is_valid(last_ob, recursive, state)) return false;
        }
      }
      return true;
    }
    case SSZ_TYPE_UNION:
      // Union: first byte is selector, remaining bytes are the selected variant
      if (ob.bytes.len == 0 || ob.bytes.data[0] >= ob.def->def.container.len) THROW_INVALID("Invalid selector for union");
      if (recursive && ob.def->def.container.elements[ob.bytes.data[0]].type != SSZ_TYPE_NONE && !ssz_is_valid(ssz_ob(ob.def->def.container.elements[ob.bytes.data[0]], bytes(ob.bytes.data + 1, ob.bytes.len - 1)), recursive, state)) return false;
      return true;
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

uint32_t ssz_len(ssz_ob_t ob) {
  switch (ob.def->type) {
    case SSZ_TYPE_VECTOR: return ob.def->def.vector.len;
    case SSZ_TYPE_LIST: {
      size_t fixed_length = ssz_fixed_length(ob.def->def.vector.type);
      if (fixed_length == 0) return 0;
      return ob.bytes.len > SSZ_OFFSET_SIZE && ssz_is_dynamic(ob.def->def.vector.type)
                 ? uint32_from_le(ob.bytes.data) / SSZ_OFFSET_SIZE
                 : ob.bytes.len / fixed_length;
    }
    case SSZ_TYPE_BIT_VECTOR:
      return ob.bytes.len * 8;
    case SSZ_TYPE_BIT_LIST: {
      uint8_t last_bit = ob.bytes.data[ob.bytes.len - 1];
      if (last_bit == 1) return ob.bytes.len * 8 - 8;
      for (int i = 7; i >= 0; i--) {
        if (last_bit & (1 << i))
          return ((ob.bytes.len - 1) * 8) + i;
      }
      return ob.bytes.len * 8; // this should never happen, since the spec requires to set a bit in the last byte
    }
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

  if (ssz_is_dynamic(ob.def->def.vector.type)) {
    uint32_t offset     = uint32_from_le(ob.bytes.data + index * SSZ_OFFSET_SIZE);
    uint32_t end_offset = index < len - 1 ? uint32_from_le(ob.bytes.data + (index + 1) * SSZ_OFFSET_SIZE) : ob.bytes.len;
    return (ssz_ob_t) {
        .def   = ob.def->def.vector.type,
        .bytes = bytes(ob.bytes.data + offset, end_offset - offset)};
  }

  size_t element_size = ssz_fixed_length(ob.def->def.vector.type);
  if (element_size * (index + 1) > ob.bytes.len)
    return res;

  return (ssz_ob_t) {
      .def   = ob.def->def.vector.type,
      .bytes = bytes(
          ob.bytes.data + index * element_size,
          element_size)};
}

bool ssz_is_type(ssz_ob_t* ob, const ssz_def_t* def) {
  if (!ob || !ob->def || !def) return false;
  if (ob->def == def) return true;
  if (ob->def->type == SSZ_TYPE_UNION) {
    ssz_ob_t union_ob = ssz_union(*ob);
    return ssz_is_type(&union_ob, def);
  }
  if (ob->def->type == SSZ_TYPE_CONTAINER) return ob->def->def.container.elements == def;
  if (ob->def->type == SSZ_TYPE_LIST) return ob->def->def.vector.type == def;
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

typedef struct {
  buffer_t buf;
  bool     write_unit_as_hex;
  bool     no_quotes;
} ssz_dump_t;

/**
 * Main function for JSON serialization of SSZ objects.
 * Recursively converts SSZ data to JSON format with pretty-printing.
 *
 * @param ctx Context with output buffer and formatting flags
 * @param ob The SSZ object to serialize
 * @param name Optional property name for container fields (can be NULL)
 * @param intend Indentation depth for pretty-printing
 */
static void dump(ssz_dump_t* ctx, ssz_ob_t ob, const char* name, int intend) {
  const ssz_def_t* def        = ob.def;
  buffer_t*        buf        = &ctx->buf;
  char             close_char = '\0';
  for (int i = 0; i < intend; i++) buffer_add_chars(buf, " ");
  if (!def) {
    buffer_add_chars(buf, "<invalid>");
    return;
  }
  if (name) bprintf(buf, "\"%s\":", name);
  switch (def->type) {
    case SSZ_TYPE_UINT:
      if (ctx->write_unit_as_hex) { // eth rpc requires hex representation of uints, represented as a bigendian without leading zeros
        // Buffer overflow protection: max uint size is 32 bytes
        if (def->def.uint.len > SSZ_MAX_UINT_SIZE || def->def.uint.len > ob.bytes.len) {
          buffer_add_chars(buf, ctx->no_quotes ? "0x0" : "\"0x0\"");
          break;
        }
        bytes32_t tmp = {0};
        for (int i = 0; i < def->def.uint.len; i++) tmp[i] = ob.bytes.data[def->def.uint.len - 1 - i];
        bprintf(buf, ctx->no_quotes ? "0x%u" : "\"0x%u\"", bytes(tmp, def->def.uint.len));
      }
      else
        switch (def->def.uint.len) {
          case 1: bprintf(buf, "%d", (uint32_t) ob.bytes.data[0]); break;
          case 2: bprintf(buf, "%d", (uint32_t) uint16_from_le(ob.bytes.data)); break;
          case 4: bprintf(buf, "%d", (uint32_t) uint32_from_le(ob.bytes.data)); break;
          case 8: bprintf(buf, "%l", uint64_from_le(ob.bytes.data)); break;
          case 32: {
            bytes32_t tmp = {0};
            for (int i = 0; i < 32; i++)
              tmp[i] = ob.bytes.data[31 - i];
            bprintf(buf, ctx->no_quotes ? "0x%x" : "\"0x%x\"", bytes_remove_leading_zeros(bytes(tmp, 32)));
            break;
          }
          default: bprintf(buf, ctx->no_quotes ? "0x%x" : "\"0x%x\"", ob.bytes);
        }
      break;
    case SSZ_TYPE_NONE:
      // None type renders as JSON null
      buffer_add_chars(buf, "null");
      break;
    case SSZ_TYPE_BOOLEAN:
      // Boolean renders as JSON true/false
      buffer_add_chars(buf, ob.bytes.data[0] ? "true" : "false");
      break;
    case SSZ_TYPE_CONTAINER: {
      // Container: iterate through fields, respect optional field mask
      uint64_t mask  = 0;
      ctx->no_quotes = false;
      close_char     = '}';
      bool first     = true;
      buffer_add_chars(buf, "{\n");
      for (int i = 0; i < def->def.container.len; i++) {
        ssz_ob_t val = ssz_get(&ob, (char*) def->def.container.elements[i].name);
        if (val.def->flags & SSZ_FLAG_OPT_MASK) {
          mask |= bytes_as_le(val.bytes);
          continue;
        }
        if (mask && (mask & (1 << i)) == 0) continue;
        if (first)
          first = false;
        else
          buffer_add_chars(buf, ",\n");
        dump(ctx, val, def->def.container.elements[i].name, intend + 2);
      }
      break;
    }
    case SSZ_TYPE_BIT_VECTOR:
    case SSZ_TYPE_BIT_LIST: {
      // Bit vectors/lists render as hex strings
      bprintf(buf, ctx->no_quotes ? "0x%x" : "\"0x%x\"", ob.bytes);
      break;
    }
    case SSZ_TYPE_VECTOR:
    case SSZ_TYPE_LIST: {
      // Lists/vectors: special handling for byte arrays, strings, and complex types
      if (def == &ssz_string_def || def->flags & SSZ_FLAG_STRING)
        bprintf(buf, ctx->no_quotes ? "%J" : "\"%J\"", (json_t) {.type = JSON_TYPE_OBJECT, .start = (char*) ob.bytes.data, .len = ob.bytes.len});
      else if (def->def.vector.type->type == SSZ_TYPE_UINT && def->def.vector.type->def.uint.len == 1) { // byte array
        if (def->flags & SSZ_FLAG_UINT) {
          bytes32_t tmp = {0};
          for (int i = 0; i < ob.bytes.len; i++) tmp[i] = ob.bytes.data[ob.bytes.len - 1 - i];
          bprintf(buf, ctx->no_quotes ? "0x%u" : "\"0x%u\"", bytes(tmp, ob.bytes.len));
        }
        else // Render as hex bytes
          bprintf(buf, ctx->no_quotes ? "0x%x" : "\"0x%x\"", ob.bytes);
      }
      else {
        ctx->no_quotes = false;
        buffer_add_chars(buf, "[\n");
        for (int i = 0; i < ssz_len(ob); i++) {
          ssz_ob_t val = ssz_at(ob, i);
          if (val.def && val.def->type == SSZ_TYPE_UNION) val = ssz_union(val);
          if (!val.def || val.def->type == SSZ_TYPE_NONE) continue;
          dump(ctx, val, false, intend + 2);
          if (i < ssz_len(ob) - 1) buffer_add_chars(buf, ",\n");
        }
        close_char = ']';
      }
      break;
    }
    case SSZ_TYPE_UNION:
      // Union: serialize with selector and value
      if (ob.bytes.len == 0 || ob.bytes.data[0] >= def->def.container.len)
        buffer_add_chars(buf, "null");
      else if (def->def.container.elements[ob.bytes.data[0]].type == SSZ_TYPE_NONE)
        bprintf(buf, "{\"selector\":%d,\"value\":null}", ob.bytes.data[0]);
      else {
        bprintf(buf, "{ \"selector\":%d, \"value\":", ob.bytes.data[0]);
        dump(ctx, ssz_ob(def->def.container.elements[ob.bytes.data[0]], bytes(ob.bytes.data + 1, ob.bytes.len - 1)), false, intend + 2);
        close_char = '}';
      }
      break;
    default: buffer_add_chars(buf, (char*) ob.bytes.data); break;
  }

  if (close_char) {
    buffer_add_chars(buf, "\n");
    for (int i = 0; i < intend; i++) buffer_add_chars(buf, " ");
    bprintf(buf, "%c", close_char);
  }
}
char* ssz_dump_to_str(ssz_ob_t ob, bool include_name, bool write_unit_as_hex) {
  ssz_dump_t ctx = {
      .buf               = {0},
      .write_unit_as_hex = write_unit_as_hex,
  };
  dump(&ctx, ob, include_name ? ob.def->name : NULL, 0);
  return (char*) ctx.buf.data.data;
}

void ssz_dump_to_file(FILE* f, ssz_ob_t ob, bool include_name, bool write_unit_as_hex) {
  ssz_dump_t ctx = {
      .buf               = {0},
      .write_unit_as_hex = write_unit_as_hex,

  };
  dump(&ctx, ob, include_name ? ob.def->name : NULL, 0);
  bytes_write(ctx.buf.data, f, false);
  buffer_free(&ctx.buf);
}
void ssz_dump_to_file_no_quotes(FILE* f, ssz_ob_t ob) {
  ssz_dump_t ctx = {
      .buf               = {0},
      .write_unit_as_hex = true,
      .no_quotes         = true,
  };
  dump(&ctx, ob, NULL, 0);
  bytes_write(ctx.buf.data, f, false);
  buffer_free(&ctx.buf);
}
