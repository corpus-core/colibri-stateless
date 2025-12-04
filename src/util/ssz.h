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

// : APIs

// :: Internal APIs

// ::: ssz.h
// ssz implementation for building and reading ssz encoded data.
//

// SSZ Constants
#define SSZ_OFFSET_SIZE     4                    /**< Size in bytes for offsets in dynamic structures */
#define SSZ_BYTES_PER_CHUNK 32                   /**< Bytes per Merkle chunk */
#define SSZ_BITS_PER_CHUNK  256                  /**< Bits per Merkle chunk (32 * 8) */
#define SSZ_MAX_UINT_SIZE   32                   /**< Maximum size for uint types in bytes */
#define SSZ_MAX_BYTES       (1024 * 1024 * 1024) /**< Maximum SSZ object size (1GB) to prevent integer overflows */

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

/**
 * Flags to control SSZ type behavior and serialization.
 * These flags can be combined using bitwise OR.
 */
typedef enum {
  SSZ_FLAG_NONE     = 0, /**< No special flags */
  SSZ_FLAG_OPT_MASK = 1, /**< Marks a field containing a bitmask indicating which optional fields are present in the container */
  SSZ_FLAG_UINT     = 2, /**< Render bytes as uint in JSON output (for numeric fields stored as bytes) */
  SSZ_FLAG_STRING   = 4, /**< Render bytes as string in JSON output (for text fields stored as bytes) */
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

/**
 * Creates a new ssz_ob_t object from a type definition and raw bytes.
 *
 * @param typename The SSZ type definition (without &)
 * @param data The bytes_t containing the SSZ-encoded data
 *
 * Example:
 * ```c
 * bytes_t block_bytes = fetch_block_data();
 * ssz_ob_t block = ssz_ob(BeaconBlock, block_bytes);
 * ```
 */
#define ssz_ob(typename, data) \
  (ssz_ob_t) { .bytes = data, .def = &typename }

/**
 * Initializes an SSZ builder for a given type definition.
 *
 * @param typename Pointer to the SSZ type definition
 *
 * Example:
 * ```c
 * ssz_builder_t builder = ssz_builder_for_def(&BlockHeader);
 * ```
 */
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

/**
 * Gets the number of elements in a list, vector, or bit container.
 *
 * For SSZ_TYPE_VECTOR: Returns the fixed length defined in the type definition.
 * For SSZ_TYPE_LIST: Returns the actual number of elements in the list by analyzing
 *                    the offset array (dynamic types) or dividing total bytes by element size (fixed types).
 * For SSZ_TYPE_BIT_VECTOR: Returns the number of bits (total bytes * 8).
 * For SSZ_TYPE_BIT_LIST: Returns the number of bits by finding the sentinel bit in the last byte.
 *
 * @param ob The SSZ object to get the length from
 * @return The number of elements, or 0 if the type doesn't support length
 *
 * Example:
 * ```c
 * ssz_ob_t transactions = ssz_get(&block, "transactions");
 * uint32_t tx_count = ssz_len(transactions);
 * printf("Block contains %u transactions\n", tx_count);
 * ```
 */
uint32_t ssz_len(ssz_ob_t ob);

/** gets the bytes of the object */
static inline bytes_t ssz_bytes(ssz_ob_t ob) {
  return ob.bytes;
}

static inline bool ssz_is_error(ssz_ob_t ob) {
  return !ob.def || !ob.bytes.data;
}

/**
 * Retrieves an element from a list or vector at the specified index.
 *
 * For lists/vectors with dynamic element types: Uses the offset array to locate the element.
 * For lists/vectors with fixed element types: Calculates the position by multiplying index with element size.
 *
 * @param ob The SSZ list or vector object
 * @param index Zero-based index of the element to retrieve
 * @return An ssz_ob_t containing the element, or an error object (with NULL def/data) if index is out of bounds
 *
 * Example:
 * ```c
 * ssz_ob_t logs = ssz_get(&receipt, "logs");
 * for (uint32_t i = 0; i < ssz_len(logs); i++) {
 *     ssz_ob_t log = ssz_at(logs, i);
 *     ssz_ob_t address = ssz_get(&log, "address");
 *     // Process log...
 * }
 * ```
 */
ssz_ob_t ssz_at(ssz_ob_t ob, uint32_t index);

/**
 * Retrieves a field value from a container by name.
 *
 * Searches through the container's field definitions to find a matching name,
 * then extracts and returns the corresponding value. Handles both fixed and
 * dynamic fields, computing offsets as needed.
 * if the field is a union, the union value will be returned.
 *
 * @param ob Pointer to the container SSZ object
 * @param name Name of the field to retrieve
 * @return An ssz_ob_t containing the field value, or an error object if not found or invalid type
 *
 * Example:
 * ```c
 * ssz_ob_t block = ssz_ob(BeaconBlock, block_bytes);
 * uint64_t slot = ssz_get_uint64(&block, "slot");
 * ssz_ob_t body = ssz_get(&block, "body");
 * bytes32_t parent_root = ssz_get(&block, "parent_root").bytes;
 * ```
 */
ssz_ob_t ssz_get(ssz_ob_t* ob, const char* name);

/**
 * Gets the type definition for a named field within a container.
 *
 * @param def The container type definition
 * @param name Name of the field to find
 * @return Pointer to the field's type definition, or NULL if not found
 */
const ssz_def_t* ssz_get_def(const ssz_def_t* def, const char* name);

static inline uint64_t ssz_get_uint64(ssz_ob_t* ob, char* name) {
  return ssz_uint64(ssz_get(ob, name));
}

static inline uint32_t ssz_get_uint32(ssz_ob_t* ob, char* name) {
  return ssz_uint32(ssz_get(ob, name));
}
/**
 * Combines two generalized indices where gindex2 represents a subtree of gindex1.
 *
 * This is used to navigate deeper into the Merkle tree structure by appending
 * one path to another. The depth of gindex2 is determined and then combined with gindex1.
 *
 * @param gindex1 The base generalized index
 * @param gindex2 The subtree generalized index to append
 * @return The combined generalized index, or 0 on error (depth too large)
 */
gindex_t ssz_add_gindex(gindex_t gindex1, gindex_t gindex2);

/**
 * Verifies a multi-Merkle proof for multiple leaves and computes the root hash.
 *
 * Takes a proof (witness nodes), the leaf values, and their generalized indices,
 * then reconstructs the Merkle tree to verify the proof is valid and compute the root.
 *
 * IMPORTANT: This function may use recursion for complex proofs and does NOT
 * allocate heap memory during verification. Ensure adequate stack size for
 * proofs with many leaves or deep tree structures.
 *
 * @param proof_data The proof witness nodes (32 bytes each)
 * @param leafes The leaf values to verify (32 bytes each)
 * @param gindex Array of generalized indices for each leaf (length must match leafes.len/32)
 * @param out Output buffer for the computed root hash (32 bytes)
 * @return true if the proof is valid, false otherwise
 */
bool ssz_verify_multi_merkle_proof(bytes_t proof_data, bytes_t leafes, gindex_t* gindex, bytes32_t out);

/**
 * Verifies a single-leaf Merkle proof and computes the root hash.
 *
 * IMPORTANT: This function may use recursion and does NOT allocate heap memory
 * during verification. Ensure adequate stack size for proofs in deep tree structures.
 *
 * @param proof_data The proof witness nodes (32 bytes each)
 * @param leaf The leaf value to verify (32 bytes)
 * @param gindex The generalized index of the leaf
 * @param out Output buffer for the computed root hash (32 bytes)
 */
void ssz_verify_single_merkle_proof(bytes_t proof_data, bytes32_t leaf, gindex_t gindex, bytes32_t out);

/**
 * Extracts the active variant from a union object.
 *
 * A union in SSZ is serialized as [selector_byte][data...] where the selector
 * indicates which variant is active. This function reads the selector and returns
 * the corresponding variant as an ssz_ob_t.
 *
 * @param ob The union object to unwrap
 * @return An ssz_ob_t containing the active variant, or an empty object if invalid.
 *         Returns an object with SSZ_TYPE_NONE if the union represents a null/empty value.
 *
 * Example:
 * ```c
 * ssz_ob_t payload = ssz_get(&block, "execution_payload");
 * ssz_ob_t actual_payload = ssz_union(payload);
 * if (actual_payload.def->type != SSZ_TYPE_NONE) {
 *     // Process the actual payload variant
 *     uint64_t gas_used = ssz_get_uint64(&actual_payload, "gas_used");
 * }
 * ```
 */
ssz_ob_t ssz_union(ssz_ob_t ob);

/**
 * Calculates the size of the fixed portion of an SSZ type in bytes.
 *
 * For dynamic types (lists, unions, bit lists): Returns SSZ_OFFSET_SIZE (4 bytes) for the offset.
 * For fixed types: Returns the actual size in bytes.
 * For containers: Sums the fixed lengths of all fields.
 *
 * @param def The SSZ type definition
 * @return Size in bytes of the fixed portion
 */
size_t ssz_fixed_length(const ssz_def_t* def);

/**
 * Serializes an SSZ object to JSON format and writes to a file.
 *
 * @param f File handle to write to
 * @param ob The SSZ object to dump
 * @param include_name If true, includes the type name in output
 * @param write_unit_as_hex If true, renders uint values as hex strings
 */
void ssz_dump_to_file(FILE* f, ssz_ob_t ob, bool include_name, bool write_unit_as_hex);

/**
 * Serializes an SSZ object to JSON string.
 *
 * @param ob The SSZ object to dump
 * @param include_name If true, includes the type name in output
 * @param write_unit_as_hex If true, renders uint values as hex strings
 * @return Allocated string containing JSON representation (caller must free)
 */
char* ssz_dump_to_str(ssz_ob_t ob, bool include_name, bool write_unit_as_hex);

/**
 * Serializes an SSZ object to JSON without quotes around hex values.
 * Used for RPC-compatible output.
 *
 * @param f File handle to write to
 * @param ob The SSZ object to dump
 */
void ssz_dump_to_file_no_quotes(FILE* f, ssz_ob_t ob);

/**
 * Computes the SSZ hash tree root of an object.
 *
 * Implements the SSZ hash_tree_root algorithm by:
 * 1. Chunking the data into 32-byte pieces
 * 2. Building a Merkle tree from the chunks
 * 3. For lists/bit lists: mixing in the length
 *
 * IMPORTANT: This function uses the stack for recursive Merkle tree computation
 * and does NOT allocate heap memory. For deeply nested structures or large
 * Merkle trees, ensure your stack size is sufficient (typically 1-2 MB is adequate).
 * The recursion depth is log2(number_of_chunks), so even large objects have
 * manageable stack usage.
 *
 * @param ob The SSZ object to hash
 * @param out Output buffer for the root hash (32 bytes)
 *
 * Example:
 * ```c
 * ssz_ob_t block = ssz_ob(BeaconBlock, block_bytes);
 * bytes32_t block_root;
 * ssz_hash_tree_root(block, block_root);
 * printf("Block root: 0x");
 * for (int i = 0; i < 32; i++) printf("%02x", block_root[i]);
 * ```
 */
void ssz_hash_tree_root(ssz_ob_t ob, uint8_t* out);

/**
 * Creates a Merkle proof for a single leaf in an SSZ object.
 *
 * IMPORTANT: Like ssz_hash_tree_root(), this function uses the stack for
 * recursive computation and does NOT allocate heap memory during tree traversal.
 * Only the final proof bytes are heap-allocated (which the caller must free).
 * Ensure adequate stack size for deeply nested structures.
 *
 * @param root The SSZ object serving as the Merkle tree root
 * @param root_hash The hash tree root of the object
 * @param gindex The generalized index of the leaf to prove
 * @return Allocated bytes containing the proof (caller must free)
 */
bytes_t ssz_create_proof(ssz_ob_t root, bytes32_t root_hash, gindex_t gindex);

/**
 * Creates a multi-Merkle proof for multiple leaves in an SSZ object.
 *
 * IMPORTANT: Like ssz_hash_tree_root(), this function uses the stack for
 * recursive computation and does NOT allocate heap memory during tree traversal.
 * Only the final proof bytes are heap-allocated (which the caller must free).
 * Ensure adequate stack size for deeply nested structures.
 *
 * @param root The SSZ object serving as the Merkle tree root
 * @param root_hash The hash tree root of the object
 * @param gindex_len Number of generalized indices
 * @param ... Variable arguments: gindex_len generalized indices (gindex_t)
 * @return Allocated bytes containing the proof (caller must free)
 */
bytes_t ssz_create_multi_proof(ssz_ob_t root, bytes32_t root_hash, int gindex_len, ...);

/**
 * Computes a generalized index for navigating into an SSZ structure.
 *
 * Takes a path through the SSZ structure and returns the corresponding generalized index.
 * For containers: provide field names (const char*)
 * For lists/vectors: provide element indices (int)
 *
 * @param def The root SSZ type definition
 * @param num_elements Number of path elements
 * @param ... Variable arguments: path elements (field names or indices)
 * @return The computed generalized index, or 0 on error
 *
 * Example:
 * ```c
 * // Get gindex for block.body.execution_payload.transactions[5]
 * gindex_t gindex = ssz_gindex(&BeaconBlock, 4,
 *                               "body",              // Navigate to body field
 *                               "execution_payload",  // Navigate to execution_payload
 *                               "transactions",       // Navigate to transactions list
 *                               5);                   // Navigate to element at index 5
 *
 * // Use gindex to create a proof
 * bytes_t proof = ssz_create_proof(block, block_root, gindex);
 * ```
 */
gindex_t ssz_gindex(const ssz_def_t* def, int num_elements, ...);

/**
 * Creates a multi-Merkle proof for leaves specified by an array of generalized indices.
 *
 * IMPORTANT: Like ssz_hash_tree_root(), this function uses the stack for
 * recursive computation and does NOT allocate heap memory during tree traversal.
 * Only the final proof bytes are heap-allocated (which the caller must free).
 * Ensure adequate stack size for deeply nested structures.
 *
 * @param root The SSZ object serving as the Merkle tree root
 * @param root_hash The hash tree root of the object
 * @param gindex Array of generalized indices
 * @param gindex_len Number of generalized indices in the array
 * @return Allocated bytes containing the proof (caller must free)
 */
bytes_t ssz_create_multi_proof_for_gindexes(ssz_ob_t root, bytes32_t root_hash, gindex_t* gindex, int gindex_len);

/**
 * Checks if an SSZ type has dynamic length.
 *
 * Dynamic types include: lists, bit lists, unions, and containers containing dynamic fields.
 * Dynamic types are serialized with offsets in the fixed portion.
 *
 * @param def The SSZ type definition to check
 * @return true if the type is dynamic, false if fixed-size
 */
bool ssz_is_dynamic(const ssz_def_t* def);

/**
 * Checks if an SSZ object matches a given type definition.
 *
 * @param ob Pointer to the SSZ object to check
 * @param def The type definition to match against
 * @return true if the object matches the type, false otherwise
 */
bool ssz_is_type(ssz_ob_t* ob, const ssz_def_t* def);

/**
 * Validates an SSZ object according to SSZ specification rules.
 *
 * This is the first line of defense for untrusted data. It checks:
 * - Object size doesn't exceed SSZ_MAX_BYTES (1GB) to prevent integer overflows
 * - Byte lengths match type requirements
 * - For booleans: value is 0 or 1
 * - For vectors: length matches definition
 * - For lists: offsets are valid and monotonically increasing
 * - For containers: all field offsets are valid
 * - For unions: selector is in valid range
 * - For bit lists/vectors: length is correct
 *
 * If recursive=true, recursively validates all nested objects.
 *
 * IMPORTANT: All user/network data MUST be validated with this function before use.
 * After validation, no further safety checks are performed.
 *
 * @param ob The SSZ object to validate
 * @param recursive If true, recursively validates nested objects
 * @param state Optional state object for error reporting (can be NULL)
 * @return true if valid, false if invalid (error details in state->error if provided)
 *
 * Example:
 * ```c
 * bytes_t block_data = fetch_from_network();
 * ssz_ob_t block = ssz_ob(BeaconBlock, block_data);
 *
 * c4_state_t state = {0};
 * if (!ssz_is_valid(block, true, &state)) {
 *     printf("Invalid block: %s\n", state.error);
 *     c4_state_free(&state);
 *     return;
 * }
 * // Safe to use block data now
 * ```
 */
bool ssz_is_valid(ssz_ob_t ob, bool recursive, c4_state_t* state);

extern const ssz_def_t ssz_uint8;               // Uint<8> of length 1 - single byte
extern const ssz_def_t ssz_uint32_def;          // Uint<32> of length 4
extern const ssz_def_t ssz_uint64_def;          // Uint<64> of length 8
extern const ssz_def_t ssz_uint256_def;         // Uint<256> of length 32
extern const ssz_def_t ssz_bytes32;             // Vector<uint8> of length 32
extern const ssz_def_t ssz_secp256k1_signature; // Vector<uint8> of length 65
extern const ssz_def_t ssz_bls_pubky;           // Vector<uint8> of length 48
extern const ssz_def_t ssz_bytes_list;          // List<uint8> displayed as hex in JSON
extern const ssz_def_t ssz_string_def;          // List<uint8> displayed as string in JSON
extern const ssz_def_t ssz_none;                // special value for none in uions.

/**
 * Defines a boolean field.
 *
 * @param property Name of the field
 *
 * Example:
 * ```c
 * static const ssz_def_t MyStruct[] = {
 *     SSZ_BOOLEAN("is_active"),
 *     SSZ_UINT64("timestamp")
 * };
 * ```
 */
#define SSZ_BOOLEAN(property)       \
  {                                 \
      .name     = property,         \
      .type     = SSZ_TYPE_BOOLEAN, \
      .def.uint = {.len = 1}}

/**
 * Defines a uint field of arbitrary length.
 *
 * @param property Name of the field
 * @param length Size in bytes (1, 2, 4, 8, or 32)
 *
 * Example:
 * ```c
 * SSZ_UINT("slot", 8)        // 64-bit uint
 * SSZ_UINT("epoch", 8)       // 64-bit uint
 * SSZ_UINT("gwei_amount", 8) // 64-bit uint
 * ```
 */
#define SSZ_UINT(property, length)                                        \
  {                                                                       \
    .name = property, .type = SSZ_TYPE_UINT, .def.uint = {.len = length } \
  }
/**
 * Defines a list (variable-length array) field.
 *
 * @param property Name of the field
 * @param typePtr Element type definition (without &)
 * @param max_len Maximum number of elements
 *
 * Example:
 * ```c
 * SSZ_LIST("transactions", Transaction, 1048576)  // List of up to 1M transactions
 * SSZ_LIST("logs", Log, 256)                      // List of up to 256 logs
 * ```
 */
#define SSZ_LIST(property, typePtr, max_len)                                   \
  {                                                                            \
    .name = property, .type = SSZ_TYPE_LIST, .def.vector = {.len  = max_len,   \
                                                            .type = &typePtr } \
  }

/**
 * Defines a list field with custom flags (e.g., for rendering).
 *
 * @param property Name of the field
 * @param typePtr Element type definition (without &)
 * @param max_len Maximum number of elements
 * @param ssz_flags Rendering flags (SSZ_FLAG_UINT, SSZ_FLAG_STRING, etc.)
 *
 * Example:
 * ```c
 * SSZ_FLAG_LIST("extra_data", ssz_uint8, 32, SSZ_FLAG_STRING)  // Render as string
 * ```
 */
#define SSZ_FLAG_LIST(property, typePtr, max_len, ssz_flags)                                       \
  {                                                                                                \
    .name = property, .type = SSZ_TYPE_LIST, .flags = ssz_flags, .def.vector = {.len  = max_len,   \
                                                                                .type = &typePtr } \
  }

/**
 * Defines an optional list field (used with optional field masks).
 *
 * @param property Name of the field
 * @param typePtr Element type definition (without &)
 * @param max_len Maximum number of elements
 */
#define SSZ_OPT_LIST(property, typePtr, max_len)                                                           \
  {                                                                                                        \
    .name = property, .type = SSZ_TYPE_LIST, .flags = SSZ_FLAG_OPTIONAL, .def.vector = {.len  = max_len,   \
                                                                                        .type = &typePtr } \
  }

/**
 * Defines a vector (fixed-length array) field.
 *
 * @param property Name of the field
 * @param typePtr Element type definition (without &)
 * @param length Exact number of elements (fixed)
 *
 * Example:
 * ```c
 * SSZ_VECTOR("validators", Validator, 512)  // Exactly 512 validators
 * SSZ_VECTOR("block_roots", ssz_bytes32, 8192)  // Exactly 8192 block roots
 * ```
 */
#define SSZ_VECTOR(property, typePtr, length)                                    \
  {                                                                              \
    .name = property, .type = SSZ_TYPE_VECTOR, .def.vector = {.len  = length,    \
                                                              .type = &typePtr } \
  }

/**
 * Defines a bit list (variable-length bit array) field.
 *
 * @param property Name of the field
 * @param max_length Maximum number of bits
 *
 * Example:
 * ```c
 * SSZ_BIT_LIST("aggregation_bits", 2048)  // Up to 2048 bits for aggregation
 * SSZ_BIT_LIST("justification_bits", 4)   // Up to 4 bits for justification
 * ```
 */
#define SSZ_BIT_LIST(property, max_length)                                          \
  {                                                                                 \
    .name = property, .type = SSZ_TYPE_BIT_LIST, .def.vector = {.len  = max_length, \
                                                                .type = NULL }      \
  }

/**
 * Defines a bit vector (fixed-length bit array) field.
 *
 * @param property Name of the field
 * @param length Exact number of bits (fixed)
 *
 * Example:
 * ```c
 * SSZ_BIT_VECTOR("sync_committee_bits", 512)  // Exactly 512 bits
 * ```
 */
#define SSZ_BIT_VECTOR(property, length)                                          \
  {                                                                               \
    .name = property, .type = SSZ_TYPE_BIT_VECTOR, .def.vector = {.len  = length, \
                                                                  .type = NULL }  \
  }

/**
 * Defines a container (struct) type with named fields.
 *
 * @param propname Name of the container type
 * @param children Array of ssz_def_t field definitions
 *
 * Example:
 * ```c
 * static const ssz_def_t BeaconBlockHeader[] = {
 *     SSZ_UINT64("slot"),
 *     SSZ_UINT64("proposer_index"),
 *     SSZ_BYTES32("parent_root"),
 *     SSZ_BYTES32("state_root")
 * };
 * const ssz_def_t BeaconBlockHeaderDef = SSZ_CONTAINER("BeaconBlockHeader", BeaconBlockHeader);
 * ```
 */
#define SSZ_CONTAINER(propname, children)                           \
  {                                                                 \
    .name          = propname,                                      \
    .type          = SSZ_TYPE_CONTAINER,                            \
    .def.container = {.elements = children,                         \
                      .len      = sizeof(children) / sizeof(ssz_def_t) } \
  }

/**
 * Defines a union type (one of several variants).
 *
 * @param propname Name of the union type
 * @param children Array of ssz_def_t variant definitions
 *
 * Example:
 * ```c
 * static const ssz_def_t ExecutionPayloadVariants[] = {
 *     SSZ_CONTAINER("Bellatrix", BellatrixPayload),
 *     SSZ_CONTAINER("Capella", CapellaPayload),
 *     SSZ_CONTAINER("Deneb", DenebPayload)
 * };
 * const ssz_def_t ExecutionPayloadUnion = SSZ_UNION("ExecutionPayload", ExecutionPayloadVariants);
 * ```
 */
#define SSZ_UNION(propname, children)                               \
  {                                                                 \
    .name          = propname,                                      \
    .type          = SSZ_TYPE_UNION,                                \
    .def.container = {.elements = children,                         \
                      .len      = sizeof(children) / sizeof(ssz_def_t) } \
  }

/**
 * Defines a bitmask field indicating which optional fields are present.
 * Used in containers with optional fields.
 *
 * @param property Name of the mask field
 * @param length Size in bytes (4 or 8)
 *
 * Example:
 * ```c
 * static const ssz_def_t OptionalContainer[] = {
 *     SSZ_OPT_MASK("field_mask", 4),  // 32-bit mask
 *     SSZ_UINT64("optional_field_1"),
 *     SSZ_UINT64("optional_field_2")
 * };
 * ```
 */
#define SSZ_OPT_MASK(property, length)                                                                \
  {                                                                                                   \
    .name = property, .type = SSZ_TYPE_UINT, .flags = SSZ_FLAG_OPT_MASK, .def.uint = {.len = length } \
  }

// Convenience type aliases for common SSZ types

/** Single byte type (alias for ssz_uint8) */
#define SSZ_BYTE ssz_uint8

/**
 * Defines a byte list (rendered as hex in JSON).
 *
 * @param name Field name
 * @param limit Maximum number of bytes
 *
 * Example: SSZ_BYTES("extra_data", 32)
 */
#define SSZ_BYTES(name, limit) SSZ_LIST(name, ssz_uint8, limit)

/**
 * Defines a string field (byte list rendered as UTF-8 string in JSON).
 *
 * @param name Field name
 * @param limit Maximum number of bytes
 *
 * Example: SSZ_STRING("graffiti", 32)
 */
#define SSZ_STRING(name, limit) SSZ_FLAG_LIST(name, ssz_uint8, limit, SSZ_FLAG_STRING)

/**
 * Defines a fixed-length byte vector.
 *
 * @param name Field name
 * @param len Exact number of bytes
 *
 * Example: SSZ_BYTE_VECTOR("signature", 96)
 */
#define SSZ_BYTE_VECTOR(name, len) SSZ_VECTOR(name, ssz_uint8, len)

/** Defines a 32-byte hash field. Example: SSZ_BYTES32("block_root") */
#define SSZ_BYTES32(name) SSZ_BYTE_VECTOR(name, 32)

/** Defines a 20-byte Ethereum address field. Example: SSZ_ADDRESS("recipient") */
#define SSZ_ADDRESS(name) SSZ_BYTE_VECTOR(name, 20)

/** Defines a 64-bit unsigned integer field. Example: SSZ_UINT64("slot") */
#define SSZ_UINT64(name) SSZ_UINT(name, 8)

/** Defines a 256-bit unsigned integer field. Example: SSZ_UINT256("balance") */
#define SSZ_UINT256(name) SSZ_UINT(name, 32)

/** Defines a 32-bit unsigned integer field. Example: SSZ_UINT32("epoch") */
#define SSZ_UINT32(name) SSZ_UINT(name, 4)

/** Defines a 16-bit unsigned integer field. Example: SSZ_UINT16("port") */
#define SSZ_UINT16(name) SSZ_UINT(name, 2)

/** Defines an 8-bit unsigned integer field. Example: SSZ_UINT8("version") */
#define SSZ_UINT8(name) SSZ_UINT(name, 1)

/** Defines a NONE type (used as null variant in unions). */
#define SSZ_NONE {.name = "NONE", .type = SSZ_TYPE_NONE}

/**
 * Complete example of defining SSZ types and using the builder:
 * ```c
 * // 1. Define SSZ type structures
 * static const ssz_def_t TransactionProofFields[] = {
 *     SSZ_BYTES("transaction", 1073741824),
 *     SSZ_UINT32("tx_index"),
 *     SSZ_UINT64("block_number"),
 *     SSZ_BYTES32("block_hash"),
 *     SSZ_BYTES("proof", 16777216)
 * };
 * const ssz_def_t TransactionProofDef = SSZ_CONTAINER("TransactionProof", TransactionProofFields);
 *
 * // 2. Reading SSZ data - parse and validate
 * bytes_t tx_data = fetch_transaction_data();
 * ssz_ob_t tx = ssz_ob(TransactionProofDef, tx_data);
 *
 * if (ssz_is_valid(tx, true, NULL)) {
 *     uint32_t tx_index = ssz_get_uint32(&tx, "tx_index");
 *     uint64_t block_number = ssz_get_uint64(&tx, "block_number");
 *     ssz_ob_t proof = ssz_get(&tx, "proof");
 *     printf("Transaction %u in block %lu\n", tx_index, block_number);
 * }
 *
 * // 3. Building SSZ data - create proof structures
 * ssz_builder_t proof_builder = ssz_builder_for_def(&TransactionProofDef);
 *
 * // Add simple fields
 * ssz_add_bytes(&proof_builder, "transaction", transaction_bytes);
 * ssz_add_uint32(&proof_builder, 5);  // tx_index
 * ssz_add_uint64(&proof_builder, 12345);  // block_number
 * ssz_add_bytes(&proof_builder, "block_hash", block_hash_bytes);
 * ssz_add_bytes(&proof_builder, "proof", merkle_proof_bytes);
 *
 * // Convert builder to final SSZ bytes
 * ssz_ob_t proof = ssz_builder_to_bytes(&proof_builder);
 * // Use proof.bytes...
 * safe_free(proof.bytes.data);
 *
 * // 4. Building nested structures
 * static const ssz_def_t HeaderFields[] = {
 *     SSZ_UINT64("slot"),
 *     SSZ_BYTES32("parent_root")
 * };
 * static const ssz_def_t BlockProofFields[] = {
 *     SSZ_UINT64("block_number"),
 *     SSZ_CONTAINER("header", HeaderFields),
 *     SSZ_BYTES("proof", 16777216)
 * };
 *
 * ssz_builder_t block_proof = ssz_builder_for_def(&BlockProofDef);
 * ssz_add_uint64(&block_proof, 12345);
 *
 * // Add nested container
 * ssz_builder_t header = ssz_builder_for_def(&HeaderDef);
 * ssz_add_uint64(&header, 100);  // slot
 * ssz_add_bytes(&header, "parent_root", parent_root_bytes);
 * ssz_add_builders(&block_proof, "header", header);  // Consumes header builder
 *
 * ssz_add_bytes(&block_proof, "proof", proof_bytes);
 *
 * // 5. Building lists with dynamic elements
 * static const ssz_def_t TxDataFields[] = {
 *     SSZ_BYTES("raw_tx", 1073741824),
 *     SSZ_UINT32("tx_index")
 * };
 * static const ssz_def_t BlockDataFields[] = {
 *     SSZ_UINT64("block_number"),
 *     SSZ_LIST("transactions", TxDataFields, 1048576)
 * };
 *
 * ssz_builder_t block_data = ssz_builder_for_def(&BlockDataDef);
 * ssz_add_uint64(&block_data, 12345);
 *
 * // Create list builder
 * const ssz_def_t* tx_list_def = ssz_get_def(&BlockDataDef, "transactions");
 * ssz_builder_t tx_list = ssz_builder_for_def(tx_list_def);
 *
 * // Add multiple transactions to list
 * for (int i = 0; i < tx_count; i++) {
 *     ssz_builder_t tx_item = ssz_builder_for_def(tx_list_def->def.vector.type);
 *     ssz_add_bytes(&tx_item, "raw_tx", tx_bytes[i]);
 *     ssz_add_uint32(&tx_item, i);
 *     ssz_add_dynamic_list_builders(&tx_list, tx_count, tx_item);
 * }
 *
 * ssz_add_builders(&block_data, "transactions", tx_list);
 * ssz_ob_t final_block = ssz_builder_to_bytes(&block_data);
 * ```
 */

/**
 * Builder for constructing SSZ-encoded data incrementally.
 *
 * Maintains separate buffers for fixed and dynamic portions, automatically
 * handling offset calculations for dynamic fields.
 */
typedef struct {
  const ssz_def_t* def;     /**< Type definition being built */
  buffer_t         fixed;   /**< Buffer for fixed-size portion */
  buffer_t         dynamic; /**< Buffer for dynamic-size portion */
} ssz_builder_t;

/**
 * Adds a field to a container builder by name.
 *
 * @param buffer The builder to add to
 * @param name Name of the field (must exist in container definition)
 * @param data Bytes to add for this field
 */
void ssz_add_bytes(ssz_builder_t* buffer, const char* name, bytes_t data);

/**
 * Adds a field to a container builder by name.
 *
 * @param buffer The builder to add to
 * @param name Name of the field (must exist in container definition)
 * @param ob The SSZ object to add for this field
 */
void ssz_add_ob(ssz_builder_t* buffer, const char* name, ssz_ob_t ob);

/**
 * Adds a nested builder as a field to a container builder.
 *
 * Converts the nested builder to bytes and adds it to the parent.
 * Automatically adds union selector if the type is a union.
 * Frees the resources of the nested builder.
 *
 * @param buffer The parent builder
 * @param name Name of the field
 * @param data The nested builder (will be consumed and freed)
 */
void ssz_add_builders(ssz_builder_t* buffer, const char* name, ssz_builder_t data);

/**
 * Adds bytes to a list builder for dynamic element types.
 *
 * @param buffer The list builder
 * @param num_elements Current number of elements in the list
 * @param data Bytes for the new element
 */
void ssz_add_dynamic_list_bytes(ssz_builder_t* buffer, int num_elements, bytes_t data);

/**
 * Adds a nested builder to a list builder for dynamic element types.
 *
 * @param buffer The list builder
 * @param num_elements Current number of elements in the list
 * @param data The nested builder (will be consumed and freed)
 */
void ssz_add_dynamic_list_builders(ssz_builder_t* buffer, int num_elements, ssz_builder_t data);

/**
 * Adds a uint256 value to the builder in little-endian format.
 *
 * @param buffer The builder
 * @param data Bytes representing the uint256 (any length, will be padded/truncated to 32 bytes)
 */
void ssz_add_uint256(ssz_builder_t* buffer, bytes_t data);

/** Adds a uint64 value to the builder in little-endian format */
void ssz_add_uint64(ssz_builder_t* buffer, uint64_t value);

/** Adds a uint32 value to the builder in little-endian format */
void ssz_add_uint32(ssz_builder_t* buffer, uint32_t value);

/** Adds a uint16 value to the builder in little-endian format */
void ssz_add_uint16(ssz_builder_t* buffer, uint16_t value);

/** Adds a uint8 value to the builder */
void ssz_add_uint8(ssz_builder_t* buffer, uint8_t value);

/**
 * Converts a JSON object to SSZ-encoded bytes.
 *
 * Recursively converts JSON values to SSZ format according to the type definition.
 * Handles type conversions, optional fields, and CamelCase/snake_case field names.
 *
 * @param json The JSON object to convert
 * @param def The SSZ type definition to convert to
 * @param state State object for error reporting
 * @return An ssz_ob_t with allocated bytes (caller must free bytes.data)
 *
 * Example:
 * ```c
 * const char* json_str = "{\"slot\": 12345, \"parent_root\": \"0x1234...\"}";
 * json_t json = json_parse(json_str);
 *
 * c4_state_t state = {0};
 * ssz_ob_t block = ssz_from_json(json, &BeaconBlockHeader, &state);
 * if (state.error) {
 *     printf("Conversion failed: %s\n", state.error);
 * } else {
 *     // Use block...
 *     safe_free(block.bytes.data);
 * }
 * ```
 */
ssz_ob_t ssz_from_json(json_t json, const ssz_def_t* def, c4_state_t* state);

/**
 * Frees the buffers in a builder.
 * Does not free the builder struct itself.
 *
 * @param buffer The builder to free
 */
void ssz_builder_free(ssz_builder_t* buffer);

/**
 * Creates a builder from an existing SSZ object.
 * The bytes are referenced, not copied.
 *
 * @param val The SSZ object to create a builder from
 * @return A builder wrapping the object's bytes
 */
static inline ssz_builder_t ssz_builder_from(ssz_ob_t val) {
  return (ssz_builder_t) {.def = val.def, .fixed = {.allocated = val.bytes.len, .data = val.bytes}, .dynamic = {0}};
}

/**
 * Converts a builder to final SSZ-encoded bytes and frees the builder's buffers.
 *
 * Combines the fixed and dynamic portions into a single byte array.
 * The builder should not be used after this call.
 *
 * @param buffer The builder to convert (will be consumed)
 * @return An ssz_ob_t with allocated bytes (caller must free bytes.data)
 *
 * Example:
 * ```c
 * // Build a simple container
 * ssz_builder_t builder = ssz_builder_for_def(&BlockHeader);
 * ssz_add_uint64(&builder, 12345);  // slot
 * ssz_add_bytes(&builder, "parent_root", parent_root_bytes);
 * ssz_add_bytes(&builder, "state_root", state_root_bytes);
 *
 * ssz_ob_t header = ssz_builder_to_bytes(&builder);
 * // header.bytes now contains the SSZ-encoded data
 *
 * // Use the header...
 * bytes32_t root;
 * ssz_hash_tree_root(header, root);
 *
 * safe_free(header.bytes.data);
 * ```
 */
ssz_ob_t ssz_builder_to_bytes(ssz_builder_t* buffer);
#ifdef __cplusplus
}
#endif

#endif