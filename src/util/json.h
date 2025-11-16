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

#ifndef json_h__
#define json_h__

#include "bytes.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * JSON value types.
 */
typedef enum json_type_t {
  JSON_TYPE_INVALID   = 0, ///< Invalid/malformed JSON
  JSON_TYPE_STRING    = 1, ///< JSON string value
  JSON_TYPE_NUMBER    = 2, ///< JSON number value
  JSON_TYPE_OBJECT    = 3, ///< JSON object value
  JSON_TYPE_ARRAY     = 4, ///< JSON array value
  JSON_TYPE_BOOLEAN   = 5, ///< JSON boolean value (true/false)
  JSON_TYPE_NULL      = 6, ///< JSON null value
  JSON_TYPE_NOT_FOUND = -1 ///< Property/element not found
} json_type_t;

/**
 * JSON value structure. Zero-copy parser - points to original string.
 * The start pointer points into the original JSON string and len indicates
 * the length of this JSON value including delimiters (quotes, brackets, etc).
 */
typedef struct json_t {
  const char* start; ///< Pointer to start of JSON value in original string
  size_t      len;   ///< Length of JSON value including delimiters
  json_type_t type;  ///< Type of JSON value
} json_t;

/**
 * Iterator state for json_next_value function (internal use).
 */
typedef enum json_next_t {
  JSON_NEXT_FIRST,    ///< Get first element/property
  JSON_NEXT_PROPERTY, ///< Get next property in object
  JSON_NEXT_VALUE,    ///< Get next value in array
} json_next_t;

/**
 * Parse a JSON string into a json_t structure.
 * @param data JSON string to parse (must not be NULL)
 * @return parsed JSON object or JSON_TYPE_INVALID on error
 */
json_t json_parse(const char* data) NONNULL;

/**
 * Get a property value from a JSON object by name.
 * @param parent JSON object to search in
 * @param property property name to find (must not be NULL)
 * @return property value or JSON_TYPE_NOT_FOUND if not found
 */
json_t json_get(json_t parent, const char* property) NONNULL_FOR((2));

/**
 * Get an element from a JSON array by index.
 * @param parent JSON array to access
 * @param index zero-based index
 * @return array element or JSON_TYPE_NOT_FOUND if index out of bounds
 */
json_t json_at(json_t parent, size_t index);

/**
 * Get the length of a JSON array.
 * @param parent JSON array
 * @return number of elements in array, 0 if not an array
 */
size_t json_len(json_t parent);

/**
 * Iterator function for JSON objects and arrays (internal use).
 * @param value current JSON value
 * @param property_name output parameter for property names (can be NULL)
 * @param type iteration type
 * @return next JSON value or JSON_TYPE_NOT_FOUND when done
 */
json_t json_next_value(json_t value, bytes_t* property_name, json_next_t type);

/**
 * Convert JSON value to string. For JSON strings, removes quotes.
 * @param value JSON value to convert
 * @param buffer buffer for string storage (can be NULL for allocating new string, which must be freed by the caller)
 * @return C string representation
 */
char* json_as_string(json_t value, buffer_t* buffer);

/**
 * Create a new allocated string from JSON value.
 * @param parent JSON value to convert
 * @return newly allocated string (caller must free), never returns NULL
 */
char* json_new_string(json_t parent) RETURNS_NONNULL;

/**
 * Convert JSON value to byte array. Handles hex strings and numbers.
 * @param value JSON value to convert
 * @param buffer buffer for byte storage (must not be NULL)
 * @return bytes_t with converted data or NULL_BYTES on error
 */
bytes_t json_as_bytes(json_t value, buffer_t* buffer) NONNULL_FOR((2));

/**
 * Convert JSON value to byte array. Handles hex strings and numbers.
 * @param value JSON value to convert
 * @param target bytes_t to store the result
 * @return the length of the bytes written into the target
 */
uint32_t json_to_bytes(json_t value, bytes_t target);

/**
 * Convert JSON value to uint64. Handles hex strings and decimal numbers.
 * @param value JSON value to convert
 * @return converted number, 0 on error
 */
uint64_t json_as_uint64(json_t value);

/**
 * Check if JSON value is boolean true.
 * @param value JSON value to check
 * @return true if JSON boolean with value true
 */
bool json_as_bool(json_t value);

/**
 * Check if JSON value is null.
 * @param value JSON value to check
 * @return true if JSON null value
 */
bool json_as_null(json_t value);

/**
 * Compare JSON string with C string.
 * @param value JSON string value
 * @param str C string to compare (must not be NULL)
 * @return true if strings are equal
 */
bool json_equal_string(json_t value, const char* str) NONNULL_FOR((2));

/**
 * Append JSON value to buffer as string.
 * @param buffer buffer to append to (must not be NULL)
 * @param data JSON value to append
 */
void buffer_add_json(buffer_t* buffer, json_t data) NONNULL_FOR((1));

/**
 * Convert JSON value to byte array. Handles hex strings and numbers.
 * @param value JSON value to convert
 * @param target bytes_t to store the result
 * @return the length of the bytes written into the target
 */
#define json_to_var(val, var) json_to_bytes(val, bytes(var, sizeof(var)))

/**
 * Duplicate a JSON value. this will allocate new memory for json.start. Make sure to free this.
 * @param json JSON value to duplicate
 * @return duplicated JSON value
 */
json_t json_dup(json_t json);
// Convenience macros for type conversion
#define json_as_uint32(value)             ((uint32_t) json_as_uint64(value))
#define json_as_uint16(value)             ((uint16_t) json_as_uint64(value))
#define json_as_uint8(value)              ((uint8_t) json_as_uint64(value))
#define json_get_uint64(object, name)     json_as_uint64(json_get(object, name))
#define json_get_uint32(object, name)     json_as_uint32(json_get(object, name))
#define json_get_uint16(object, name)     json_as_uint16(json_get(object, name))
#define json_get_uint8(object, name)      json_as_uint8(json_get(object, name))
#define json_get_bytes(object, name, buf) json_as_bytes(json_get(object, name), buf)

/**
 * Validate JSON against a schema definition.
 *
 * This function validates a JSON value against a schema definition string and returns
 * NULL on success or an error message on validation failure.
 *
 * @param value JSON value to validate
 * @param def schema definition string (see schema syntax below)
 * @param error_prefix prefix for error messages (e.g. "request.params")
 * @return NULL on success, dynamically allocated error message string on failure
 *
 * SCHEMA SYNTAX:
 *
 * 1. PRIMITIVE TYPES:
 *    - "bytes32"   : hex string with exactly 32 bytes (66 chars with 0x prefix)
 *    - "address"   : hex string with exactly 20 bytes (42 chars with 0x prefix)
 *    - "bytes"     : hex string with arbitrary length (must have 0x prefix)
 *    - "hexuint"   : hex-encoded unsigned integer (no leading zeros, max 32 bytes)
 *    - "hex32"     : hex string with max 32 bytes length
 *    - "uint"      : JSON number (integer)
 *    - "suint"     : JSON string (integer) like "1234567890"
 *    - "bool"      : JSON boolean (true/false)
 *    - "block"     : block identifier (hex uint or "latest", "safe", "finalized")
 *
 * 2. ARRAYS:
 *    Syntax: [element_type]
 *    Example: "[bytes32]" - array of 32-byte hex strings
 *    Example: "[{name:uint,value:bytes}]" - array of objects
 *    Note: All elements must match the specified type
 *
 * 3. OBJECTS:
 *    Syntax: {field1:type1,field2:type2,...}
 *    Example: "{hash:bytes32,number:hexuint}"
 *
 *    Optional fields: Use '?' after field name
 *    Example: "{required:uint,optional?:bytes}"
 *    Note: Optional fields can be missing or null
 *
 * 4. WILDCARD OBJECTS (dynamic keys):
 *    Syntax: {*:value_type}
 *    Example: "{*:bytes32}" - object with arbitrary keys, all values must be bytes32
 *    Use case: For objects with unknown/dynamic property names
 *
 * 5. NESTED STRUCTURES:
 *    Types can be nested arbitrarily:
 *    Example: "{logs:[{address:address,topics:[bytes32],data:bytes}]}"
 *
 * EXAMPLES:
 *
 *   // Ethereum transaction
 *   "{hash:bytes32,from:address,to?:address,value:hexuint,data:bytes}"
 *
 *   // Array of log entries
 *   "[{address:address,topics:[bytes32],data:bytes}]"
 *
 *   // Storage proof with wildcard object for dynamic storage
 *   "{balance:hexuint,storage:{*:bytes32}}"
 *
 * USAGE:
 *
 *   json_t tx = json_parse(data);
 *   const char* err = json_validate(tx, "{hash:bytes32,value:hexuint}", "tx");
 *   if (err) {
 *     printf("Validation error: %s\n", err);
 *     free((void*)err);
 *   }
 */
const char* json_validate(json_t value, const char* def, const char* error_prefix);

/**
 * Validate JSON with a small global cache to skip repeated validations.
 *
 * Uses a lightweight 64-bit FNV-1a hash over (def || 0x00 || raw JSON bytes)
 * and caches a few successful validations to avoid re-validating identical,
 * large payloads (e.g., eth_getBlockReceipts). Collisions are acceptable for
 * this performance use-case.
 *
 * @param value JSON value to validate
 * @param def schema definition string
 * @param error_prefix prefix for error messages
 * @return NULL on success, dynamically allocated error message on failure
 */
const char* json_validate_cached(json_t value, const char* def, const char* error_prefix);

/**
 * Iterate over properties in a JSON object.
 * @param parent JSON object to iterate
 * @param value variable name for current property value
 * @param property_name variable name for current property name (bytes_t)
 */
#define json_for_each_property(parent, value, property_name)                    \
  for (json_t value = json_next_value(parent, &property_name, JSON_NEXT_FIRST); \
       value.type != JSON_TYPE_NOT_FOUND && value.type != JSON_TYPE_INVALID;    \
       value = json_next_value(value, &property_name, JSON_NEXT_PROPERTY))

/**
 * Iterate over values in a JSON array.
 * @param parent JSON array to iterate
 * @param value variable name for current array element
 */
#define json_for_each_value(parent, value)                                   \
  for (json_t value = json_next_value(parent, NULL, JSON_NEXT_FIRST);        \
       value.type != JSON_TYPE_NOT_FOUND && value.type != JSON_TYPE_INVALID; \
       value = json_next_value(value, NULL, JSON_NEXT_VALUE))

#ifdef __cplusplus
}
#endif

#endif
