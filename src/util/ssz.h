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

#ifndef ssz_h__
#define ssz_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "bytes.h"
#include "crypto.h"
#include "json.h"
#include "state.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// Forward declarations
typedef struct ssz_def       ssz_def_t;
typedef struct ssz_list      ssz_list_t;
typedef struct ssz_container ssz_container_t;
typedef uint64_t             gindex_t;

/** the available SSZ Types */
typedef enum {
  SSZ_TYPE_UINT       = 0, /**< Basic uint type */
  SSZ_TYPE_BOOLEAN    = 1, /**< Basic boolean type (true or false) */
  SSZ_TYPE_CONTAINER  = 2, /**< Container type */
  SSZ_TYPE_VECTOR     = 3, /**< Vector type wih a fixed length*/
  SSZ_TYPE_LIST       = 4, /**< List type with a variable length*/
  SSZ_TYPE_BIT_VECTOR = 5, /**< Bit vector type  with a fixed length*/
  SSZ_TYPE_BIT_LIST   = 6, /**< Bit list type with a variable length*/
  SSZ_TYPE_UNION      = 7, /**< Union type with a variable length*/
  SSZ_TYPE_NONE       = 8, /**< a NONE-Type (only used in unions) */
} ssz_type_t;

typedef enum {
  SSZ_FLAG_NONE         = 0,
  SSZ_FLAG_OPT_MASK     = 1, // value conains a bitmask of the fields to include in the JSON output
  SSZ_FLAG_ZSTD_ENCODED = 2,
  SSZ_FLAG_UINT         = 4, // Render bytes as uint in JSON (for boolean/numeric fields stored as bytes)
} ssz_flag_t;

/** a SSZ Type Definition */
struct ssz_def {
  const char* name;      /**< name of the property or SSZ Def*/
  uint8_t     type : 4;  /**< General SSZ type (4 bits, 0-8 fits) */
  uint8_t     flags : 4; /**< flags of the object (4 bits) */
  union {
    struct {
      uint32_t len;
    } uint; /**< basic uint definitions */
    struct ssz_container {
      const ssz_def_t* elements; /**< the elements in the container */
      uint32_t         len;      /**< the number of elements in the container or un*/
    } container;                 /**< container or union definitions */
    struct ssz_list {
      const ssz_def_t* type; /**< the type of the elements in the vector or list */
      uint32_t         len;  /**< either the fixed length of the vector or max length of the list.*/
    } vector;                /**< vector or list defintions */
  } def;
};

/** a SSZ Object which holds a reference to the definition of the object and the bytes of the object */
typedef struct {
  bytes_t          bytes; /**< the bytes of the object */
  const ssz_def_t* def;   /**< the definition of the object */
} ssz_ob_t;

/** creates a new ssz_ob_t object */
#define ssz_ob(typename, data) \
  (ssz_ob_t) { .bytes = data, .def = &typename }

#define ssz_builder_for_def(typename) \
  {.def = (ssz_def_t*) typename, .dynamic = {0}, .fixed = {0}}

/** gets the uint64 value of the object */
static inline uint64_t ssz_uint64(ssz_ob_t ob) {
  return bytes_as_le(ob.bytes);
}

/** gets the uint32 value of the object */
static inline uint32_t ssz_uint32(ssz_ob_t ob) {
  return (uint32_t) bytes_as_le(ob.bytes);
}

/** gets the length of the object */
uint32_t ssz_len(ssz_ob_t ob);

/** gets the bytes of the object */
static inline bytes_t ssz_bytes(ssz_ob_t ob) {
  return ob.bytes;
}

static inline bool ssz_is_error(ssz_ob_t ob) {
  return !ob.def || !ob.bytes.data;
}

/** gets the object of a list or vector at the index */
ssz_ob_t ssz_at(ssz_ob_t ob, uint32_t index);

/** gets the value of a field with the given name. If the data is not a container or union or if the field is not found, it will return an empty object */
ssz_ob_t ssz_get(ssz_ob_t* ob, const char* name);

/** gets the definition for the given name. If the data is not a container or union or if the field is not found, it will return NULL */
const ssz_def_t* ssz_get_def(const ssz_def_t* def, const char* name);

static inline uint64_t ssz_get_uint64(ssz_ob_t* ob, char* name) {
  return ssz_uint64(ssz_get(ob, name));
}

static inline uint32_t ssz_get_uint32(ssz_ob_t* ob, char* name) {
  return ssz_uint32(ssz_get(ob, name));
}
/** adds two gindexes so the gindex2 is a subtree of gindex1 */
gindex_t ssz_add_gindex(gindex_t gindex1, gindex_t gindex2);

/** gets the merkle proof for the given index and writes the root hash to out */
bool ssz_verify_multi_merkle_proof(bytes_t proof_data, bytes_t leafes, gindex_t* gindex, bytes32_t out);
void ssz_verify_single_merkle_proof(bytes_t proof_data, bytes32_t leaf, gindex_t gindex, bytes32_t out);
/** gets the value of a union. If the object is not a union, it will return an empty object. A Object with the type SSZ_TYPE_NONE will be returned if the union is empty */
ssz_ob_t ssz_union(ssz_ob_t ob);

// returns the length of the fixed part of the object
size_t ssz_fixed_length(const ssz_def_t* def);
/** dumps the object to a file */
void  ssz_dump_to_file(FILE* f, ssz_ob_t ob, bool include_name, bool write_unit_as_hex);
char* ssz_dump_to_str(ssz_ob_t ob, bool include_name, bool write_unit_as_hex);
void  ssz_dump_to_file_no_quotes(FILE* f, ssz_ob_t ob);

/** hashes the object */
void ssz_hash_tree_root(ssz_ob_t ob, uint8_t* out);

bytes_t  ssz_create_proof(ssz_ob_t root, bytes32_t root_hash, gindex_t gindex);
bytes_t  ssz_create_multi_proof(ssz_ob_t root, bytes32_t root_hash, int gindex_len, ...);
gindex_t ssz_gindex(const ssz_def_t* def, int num_elements, ...);
bytes_t  ssz_create_multi_proof_for_gindexes(ssz_ob_t root, bytes32_t root_hash, gindex_t* gindex, int gindex_len);

// checks if a definition has a dynamic length
bool ssz_is_dynamic(const ssz_def_t* def);
bool ssz_is_type(ssz_ob_t* ob, const ssz_def_t* def);
bool ssz_is_valid(ssz_ob_t ob, bool recursive, c4_state_t* state);

extern const ssz_def_t ssz_uint8;
extern const ssz_def_t ssz_uint32_def;
extern const ssz_def_t ssz_uint64_def;
extern const ssz_def_t ssz_uint256_def;
extern const ssz_def_t ssz_bytes32;
extern const ssz_def_t ssz_bls_pubky;
extern const ssz_def_t ssz_bytes_list;
extern const ssz_def_t ssz_string_def;
extern const ssz_def_t ssz_none;

#define SSZ_BOOLEAN(property)       \
  {                                 \
      .name     = property,         \
      .type     = SSZ_TYPE_BOOLEAN, \
      .def.uint = {.len = 1}}

#define SSZ_UINT(property, length)                                        \
  {                                                                       \
    .name = property, .type = SSZ_TYPE_UINT, .def.uint = {.len = length } \
  }
#define SSZ_LIST(property, typePtr, max_len)                                   \
  {                                                                            \
    .name = property, .type = SSZ_TYPE_LIST, .def.vector = {.len  = max_len,   \
                                                            .type = &typePtr } \
  }
#define SSZ_OPT_LIST(property, typePtr, max_len)                                                           \
  {                                                                                                        \
    .name = property, .type = SSZ_TYPE_LIST, .flags = SSZ_FLAG_OPTIONAL, .def.vector = {.len  = max_len,   \
                                                                                        .type = &typePtr } \
  }
#define SSZ_VECTOR(property, typePtr, length)                                    \
  {                                                                              \
    .name = property, .type = SSZ_TYPE_VECTOR, .def.vector = {.len  = length,    \
                                                              .type = &typePtr } \
  }

#define SSZ_BIT_LIST(property, max_length)                                          \
  {                                                                                 \
    .name = property, .type = SSZ_TYPE_BIT_LIST, .def.vector = {.len  = max_length, \
                                                                .type = NULL }      \
  }
#define SSZ_BIT_VECTOR(property, length)                                          \
  {                                                                               \
    .name = property, .type = SSZ_TYPE_BIT_VECTOR, .def.vector = {.len  = length, \
                                                                  .type = NULL }  \
  }
#define SSZ_CONTAINER(propname, children)                           \
  {                                                                 \
    .name          = propname,                                      \
    .type          = SSZ_TYPE_CONTAINER,                            \
    .def.container = {.elements = children,                         \
                      .len      = sizeof(children) / sizeof(ssz_def_t) } \
  }
#define SSZ_UNION(propname, children)                               \
  {                                                                 \
    .name          = propname,                                      \
    .type          = SSZ_TYPE_UNION,                                \
    .def.container = {.elements = children,                         \
                      .len      = sizeof(children) / sizeof(ssz_def_t) } \
  }
#define SSZ_OPT_MASK(property, length)                                                                \
  {                                                                                                   \
    .name = property, .type = SSZ_TYPE_UINT, .flags = SSZ_FLAG_OPT_MASK, .def.uint = {.len = length } \
  }

#define SSZ_BYTE                   ssz_uint8
#define SSZ_BYTES(name, limit)     SSZ_LIST(name, ssz_uint8, limit)
#define SSZ_BYTE_VECTOR(name, len) SSZ_VECTOR(name, ssz_uint8, len)
#define SSZ_BYTES32(name)          SSZ_BYTE_VECTOR(name, 32)
#define SSZ_ADDRESS(name)          SSZ_BYTE_VECTOR(name, 20)
#define SSZ_UINT64(name)           SSZ_UINT(name, 8)
#define SSZ_UINT256(name)          SSZ_UINT(name, 32)
#define SSZ_UINT32(name)           SSZ_UINT(name, 4)
#define SSZ_UINT16(name)           SSZ_UINT(name, 2)
#define SSZ_UINT8(name)            SSZ_UINT(name, 1)
#define SSZ_NONE                   {.name = "NONE", .type = SSZ_TYPE_NONE}

typedef struct {
  const ssz_def_t* def;
  buffer_t         fixed;
  buffer_t         dynamic;
} ssz_builder_t;

void     ssz_add_bytes(ssz_builder_t* buffer, const char* name, bytes_t data);
void     ssz_add_builders(ssz_builder_t* buffer, const char* name, ssz_builder_t data); // adds a builder to the buffer and frees the resources of the data builder. Itz also automaticly adds the union selector if the type is a union
void     ssz_add_dynamic_list_bytes(ssz_builder_t* buffer, int num_elements, bytes_t data);
void     ssz_add_dynamic_list_builders(ssz_builder_t* buffer, int num_elements, ssz_builder_t data);
void     ssz_add_uint256(ssz_builder_t* buffer, bytes_t data);
void     ssz_add_uint64(ssz_builder_t* buffer, uint64_t value);
void     ssz_add_uint32(ssz_builder_t* buffer, uint32_t value);
void     ssz_add_uint16(ssz_builder_t* buffer, uint16_t value);
void     ssz_add_uint8(ssz_builder_t* buffer, uint8_t value);
ssz_ob_t ssz_from_json(json_t json, const ssz_def_t* def, c4_state_t* state);
void     ssz_builder_free(ssz_builder_t* buffer);

static inline ssz_builder_t ssz_builder_from(ssz_ob_t val) {
  return (ssz_builder_t) {.def = val.def, .fixed = {.allocated = val.bytes.len, .data = val.bytes}, .dynamic = {0}};
}
// converts the ssz_buffer to bytes and the frees up the buffer
// make sure to free the returned after using
ssz_ob_t ssz_builder_to_bytes(ssz_builder_t* buffer);
#ifdef __cplusplus
}
#endif

#endif