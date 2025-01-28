#include "crypto.h"
#include "ssz.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// finds a definition by name within a container or a union
static const ssz_def_t* find_def(const ssz_def_t* def, char* name) {
  if (def->type != SSZ_TYPE_CONTAINER) return NULL;
  for (int i = 0; i < def->def.container.len; i++) {
    if (strcmp(def->def.container.elements[i].name, name) == 0) return def->def.container.elements + i;
  }
  return NULL;
}
void ssz_add_dynamic_list_bytes(ssz_builder_t* buffer, int num_elements, bytes_t data) {
  const ssz_def_t* child_def = buffer->def->def.vector.type;
  if (ssz_is_dynamic(child_def)) {
    uint32_t offset = 4 * num_elements + buffer->dynamic.data.len;
    ssz_add_uint32(buffer, offset);
    buffer_append(&buffer->dynamic, data);
  }
  else
    buffer_append(&buffer->fixed, data);
}
void ssz_add_builders(ssz_builder_t* buffer, char* name, ssz_builder_t* data) {
  ssz_ob_t element = ssz_builder_to_bytes(data);
  ssz_add_bytes(buffer, name, element.bytes);
  free(element.bytes.data);
}

void ssz_add_bytes(ssz_builder_t* buffer, char* name, bytes_t data) {
  const ssz_def_t* def = find_def(buffer->def, name);
  if (!def) return;
  buffer_t* bytes        = &(buffer->fixed);
  size_t    fixed_length = 0;

  if (ssz_is_dynamic(def)) {
    size_t offset = 0;
    for (int i = 0; i < buffer->def->def.container.len; i++)
      offset += ssz_fixed_length(buffer->def->def.container.elements + i);
    ssz_add_uint32(buffer, offset + buffer->dynamic.data.len);
    bytes = &(buffer->dynamic);
  }
  else
    fixed_length = ssz_fixed_length(buffer->def);

  if (fixed_length && bytes->data.len < fixed_length)
    buffer_append(bytes, bytes(NULL, fixed_length - bytes->data.len));
  buffer_append(bytes, data);
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
