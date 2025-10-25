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
#include "json.h"
#include "logger.h"
#include "ssz.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/**
 * Finds a field definition by name within a container.
 *
 * @param def The container type definition to search
 * @param name Name of the field to find
 * @return Pointer to the field's type definition, or NULL if not found
 */
static const ssz_def_t* find_def(const ssz_def_t* def, const char* name) {
  if (def->type != SSZ_TYPE_CONTAINER) return NULL;
  for (int i = 0; i < def->def.container.len; i++) {
    if (strcmp(def->def.container.elements[i].name, name) == 0) return def->def.container.elements + i;
  }
  return NULL;
}
void ssz_add_dynamic_list_bytes(ssz_builder_t* buffer, int num_elements, bytes_t data) {
  const ssz_def_t* child_def = buffer->def->def.vector.type;
  if (ssz_is_dynamic(child_def)) {
    // For dynamic elements: add offset to fixed portion, data to dynamic portion
    uint32_t offset = SSZ_OFFSET_SIZE * num_elements + buffer->dynamic.data.len;
    ssz_add_uint32(buffer, offset);
    buffer_append(&buffer->dynamic, data);
  }
  else
    buffer_append(&buffer->fixed, data);
}

void ssz_add_builders(ssz_builder_t* buffer, const char* name, ssz_builder_t data) {
  const ssz_def_t* def = find_def(buffer->def, name);
  // Special handling for union types: add selector byte before data
  if (def && def->type == SSZ_TYPE_UNION) {
    bool found = false;
    for (int i = 0; i < def->def.container.len; i++) {
      if (def->def.container.elements + i == data.def || (data.def->type == SSZ_TYPE_CONTAINER && def->def.container.elements[i].def.container.elements == data.def->def.container.elements)) {
        uint8_t selector = i;
        found            = true;
        buffer_splice(&data.fixed, 0, 0, bytes(&selector, 1));
        break;
      }
    }
    if (!found) {
      log_error("ssz_add_builders: Uniontype %s not found in %s.%s\n", data.def->name, buffer->def->name, name);
      return;
    }
  }

  ssz_ob_t element = ssz_builder_to_bytes(&data);
  ssz_add_bytes(buffer, name, element.bytes);
  safe_free(element.bytes.data);
  element.bytes.data = NULL;
}
void ssz_add_dynamic_list_builders(ssz_builder_t* buffer, int num_elements, ssz_builder_t data) {
  ssz_ob_t element = ssz_builder_to_bytes(&data);
  ssz_add_dynamic_list_bytes(buffer, num_elements, element.bytes);
  safe_free(element.bytes.data);
}

void ssz_add_bytes(ssz_builder_t* buffer, const char* name, bytes_t data) {
  const ssz_def_t* def = find_def(buffer->def, name);
  if (!def) {
    fbprintf(stderr, "ssz_add_bytes: name %s not found in %s\n", name, buffer->def->name);
    return;
  }
  buffer_t* bytes        = &(buffer->fixed);
  size_t    fixed_length = 0;

  // check offset
  size_t offset = 0;
  for (int i = 0; i < buffer->def->def.container.len; i++) {
    if (buffer->def->def.container.elements + i == def) {
      if (offset != buffer->fixed.data.len) {
        fbprintf(stderr, "ssz_add_bytes: %d ( +%d ) %s\n", buffer->fixed.data.len, data.len, name);
        fbprintf(stderr, "ssz_add_bytes:    offset mismatch %l != %d\n", (uint64_t) offset, buffer->fixed.data.len);
      }
      break;
    }
    offset += ssz_fixed_length(buffer->def->def.container.elements + i);
  }

  if (ssz_is_dynamic(def)) {
    offset = 0;
    for (int i = 0; i < buffer->def->def.container.len; i++)
      offset += ssz_fixed_length(buffer->def->def.container.elements + i);
    ssz_add_uint32(buffer, offset + buffer->dynamic.data.len);
    bytes = &(buffer->dynamic);
  }
  else
    fixed_length = ssz_fixed_length(def);

  if (fixed_length && data.len < fixed_length)
    buffer_append(bytes, bytes(NULL, fixed_length - data.len));
  buffer_append(bytes, data);
}

void ssz_add_uint256(ssz_builder_t* buffer, bytes_t data) {
  buffer_grow(&buffer->fixed, buffer->fixed.data.len + 32);
  uint8_t* ptr = buffer->fixed.data.data + buffer->fixed.data.len;
  for (int i = 0; i < data.len; i++, ptr++) {
    *ptr = data.data[data.len - i - 1];
  }
  if (data.len < 32)
    memset(ptr, 0, 32 - data.len);

  buffer->fixed.data.len += 32;
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
uint8_t ssz_union_selector(const ssz_def_t* union_types, size_t union_types_len, const char* name, const ssz_def_t** def) {
  *def = NULL;
  for (int i = 0; i < union_types_len; i++) {
    if (union_types[i].name == name || strcmp(union_types[i].name, name) == 0) {
      *def = union_types + i;
      return i;
    }
  }
  return 0;
}

void ssz_builder_free(ssz_builder_t* buffer) {
  if (!buffer) return;
  buffer_free(&buffer->fixed);
  buffer_free(&buffer->dynamic);
}

ssz_ob_t ssz_builder_to_bytes(ssz_builder_t* buffer) {
  buffer_append(&buffer->fixed, buffer->dynamic.data);
  buffer_free(&buffer->dynamic);
  return (ssz_ob_t) {.def = buffer->def, .bytes = buffer->fixed.data};
}

ssz_ob_t ssz_from_json(json_t json, const ssz_def_t* def, c4_state_t* state) {
  ssz_builder_t buf = {0};
  buf.def           = def;
  switch (def->type) {
    case SSZ_TYPE_CONTAINER: {
      // Container: iterate over all fields, convert JSON to SSZ
      // Handle optional fields (opt_mask) and CamelCase/snake_case field names
      uint64_t optmask     = 0;
      int      optmask_len = 0;
      int      optmask_idx = -1;
      for (int i = 0; i < def->def.container.len; i++) {
        if (def->def.container.elements[i].flags & SSZ_FLAG_OPT_MASK) {
          optmask_idx = buf.fixed.data.len;
          optmask_len = def->def.container.elements[i].def.uint.len;
          ssz_add_bytes(&buf, def->def.container.elements[i].name, NULL_BYTES);
          continue;
        }
        json_t element = json_get(json, def->def.container.elements[i].name);
        if (element.type == JSON_TYPE_NOT_FOUND) {
          // Field not found: try converting CamelCase to snake_case
          // e.g., "blockNumber" -> "block_number"
          buffer_t pascal_name = {0};
          for (const char* c = def->def.container.elements[i].name; *c; c++) {
            char cc = *c;
            if (cc >= 'A' && cc <= 'Z') {
              if (pascal_name.data.len && pascal_name.data.data[pascal_name.data.len - 1] != '_')
                buffer_add_chars(&pascal_name, "_");
              cc += 32; // make it lowercase
            }
            buffer_append(&pascal_name, bytes(&cc, 1));
          }
          buffer_grow(&pascal_name, pascal_name.data.len + 1);
          pascal_name.data.data[pascal_name.data.len] = '\0';
          element                                     = json_get(json, (const char*) pascal_name.data.data);
          buffer_free(&pascal_name);
        }
        if (element.type == JSON_TYPE_NOT_FOUND) {
          if (optmask_idx != -1) {
            ssz_add_bytes(&buf, def->def.container.elements[i].name, NULL_BYTES);
            continue;
          }
          char* error = bprintf(NULL, "ssz_from_json: %s.%s not found", def->name, def->def.container.elements[i].name);
          c4_state_add_error(state, error);
          free(error);
        }
        optmask |= 1 << i;
        ssz_ob_t ob = ssz_from_json(element, def->def.container.elements + i, state);
        ssz_add_bytes(&buf, def->def.container.elements[i].name, ob.bytes);
        safe_free(ob.bytes.data);
      }
      if (optmask_len == 4 && buf.fixed.data.data)
        uint32_to_le(buf.fixed.data.data + optmask_idx, (uint32_t) optmask);
      else if (optmask_len == 8 && buf.fixed.data.data)
        uint64_to_le(buf.fixed.data.data + optmask_idx, optmask);
      return ssz_builder_to_bytes(&buf);
    }
    case SSZ_TYPE_UINT: {
      // Uint: convert JSON number to little-endian bytes
      buffer_append(&buf.fixed, bytes(NULL, def->def.uint.len));
      if (def->def.uint.len <= 8) {
        uint64_t val = json_as_uint64(json);
        for (int i = 0; i < def->def.uint.len; i++)
          buf.fixed.data.data[i] = (uint8_t) ((val >> (i * 8)) & 0xff);
      }
      else {
        memset(buf.fixed.data.data, 0, def->def.uint.len);
        bytes_t bytes = json_as_bytes(json, &buf.dynamic);
        if (bytes.len > def->def.uint.len) bytes.len = def->def.uint.len;
        // switch order
        for (int i = 0; i < bytes.len; i++)
          buf.fixed.data.data[bytes.len - i - 1] = bytes.data[i];
        buffer_free(&buf.dynamic);
      }
      return (ssz_ob_t) {.def = def, .bytes = buf.fixed.data};
    }
    case SSZ_TYPE_BOOLEAN: {
      // Boolean: convert JSON boolean to 0 or 1
      buffer_grow(&buf.fixed, 1);
      buf.fixed.data.data[0] = json_as_bool(json) ? 1 : 0;
      return (ssz_ob_t) {.def = def, .bytes = buf.fixed.data};
    }
    case SSZ_TYPE_NONE: {
      return (ssz_ob_t) {.def = def, .bytes = {0}};
    }
    case SSZ_TYPE_VECTOR: {
      // Vector: convert JSON array to fixed-length SSZ vector
      if (def->def.vector.type->type == SSZ_TYPE_UINT && def->def.vector.type->def.uint.len == 1) {
        bytes_t bytes = json_as_bytes(json, &buf.fixed);
        if (bytes.len > def->def.vector.len)
          buf.fixed.data.len = def->def.vector.len;
        else if (bytes.len < def->def.vector.len)
          buffer_append(&buf.fixed, bytes(NULL, def->def.vector.len - bytes.len));
        return (ssz_ob_t) {.def = def, .bytes = buf.fixed.data};
      }
      else {
        for (int i = 0; i < def->def.vector.len; i++) {
          ssz_ob_t ob = ssz_from_json(json_at(json, i), def->def.vector.type, state);
          buffer_append(&buf.fixed, ob.bytes);
          safe_free(ob.bytes.data);
        }
        return (ssz_ob_t) {.def = def, .bytes = buf.fixed.data};
      }
    }
    case SSZ_TYPE_LIST: {
      // List: convert JSON array to variable-length SSZ list
      if (def->def.vector.type->type == SSZ_TYPE_UINT && def->def.vector.type->def.uint.len == 1)
        return (ssz_ob_t) {.def = def, .bytes = json_as_bytes(json, &buf.fixed)};
      uint32_t len = json_len(json);
      if (ssz_is_dynamic(def->def.vector.type))
        for (uint32_t i = 0; i < len; i++) {
          ssz_ob_t ob = ssz_from_json(json_at(json, i), def->def.vector.type, state);
          ssz_add_uint32(&buf, 4 * len + buf.dynamic.data.len);
          buffer_append(&buf.dynamic, ob.bytes);
          safe_free(ob.bytes.data);
        }
      else {
        buffer_grow(&buf.fixed, len * ssz_fixed_length(def->def.vector.type));
        for (int i = 0; i < len; i++) {
          ssz_ob_t ob = ssz_from_json(json_at(json, i), def->def.vector.type, state);
          buffer_append(&buf.fixed, ob.bytes);
          safe_free(ob.bytes.data);
        }
      }
      return ssz_builder_to_bytes(&buf);
    }
    case SSZ_TYPE_BIT_VECTOR: {
      bytes_t bytes = json_as_bytes(json, &buf.fixed);
      if (bytes.len > def->def.vector.len / 8)
        buf.fixed.data.len = def->def.vector.len / 8;
      else if (bytes.len < def->def.vector.len / 8)
        buffer_append(&buf.fixed, bytes(NULL, def->def.vector.len / 8 - bytes.len));
      return (ssz_ob_t) {.def = def, .bytes = buf.fixed.data};
    }
    case SSZ_TYPE_BIT_LIST:
      return (ssz_ob_t) {.def = def, .bytes = json_as_bytes(json, &buf.fixed)};
    default:
      return (ssz_ob_t) {.def = def, .bytes = {0}};
  }
}
