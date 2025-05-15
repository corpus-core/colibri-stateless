#ifndef bytes_h__
#define bytes_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// Only define the attribute if the clang analyzer is running
#ifdef __clang_analyzer__
#define COUNTED_BY(len_field) __attribute__((counted_by_or_null(len_field)))
#else
#define COUNTED_BY(len_field)
#endif
// : APIs

// :: Internal APIs

// ::: bytes.h
// The bytes API is used to handle bytes data. and buffers.
//

#ifndef BYTES_T_DEFINED

/**
 * a basic type struct, which holds a pointer to a bytes buffer and the length of the buffer.
 * It should be used as a fat pointer, which is a pointer and passed by value.
 */
typedef struct {
  uint32_t      len;             // the length of the data
  uint8_t* data COUNTED_BY(len); // the data pointer
} bytes_t;

#define BYTES_T_DEFINED
#endif

#define NULL_BYTES (bytes_t){.data = NULL, .len = 0}

/**
 * a buffer representing bytes with a capacity, allowing the bytes to grow.
 * This is mainly used for creating strings or buffering data of unknown size.
 *
 * while the allocated field is handled internally. there are different values creating specific behaviors:
 *
 * - `allocated == 0`: the buffer will allocate memory on demand.
 * - `allocated > 0`: first time memory is needed, the buffer will allocate the memory and set the allocated field to the initial size.
 * - `allocated < 0`: the buffer will use the fixed memory and not grow beyond the initial size, allowing it to be used with stack variables.
 *  */
typedef struct {
  bytes_t data;      // the resulting bytes
  int32_t allocated; // 0: not allocated, >0: allocated and bytes.data == NULL, this would be the inital size, <0: fixed memory - don 't grow and don't go over the limit!
} buffer_t;

/**
 * converts a bytes_t to a uint64_t in little endian format.
 * @param data the bytes_t to convert
 * @return the uint64_t in little endian format
 */
uint64_t bytes_as_le(bytes_t data);

/**
 * converts a bytes_t to a uint64_t in big endian format.
 * @param data the bytes_t to convert
 * @return the uint64_t in big endian format
 */
uint64_t bytes_as_be(bytes_t data);

/**
 * converts a uint16_t to a bytes_t in little endian format. expecting 2 bytes.
 * @param data the pointer to the first byte
 * @return the value in little endian format
 */
uint16_t uint16_from_le(uint8_t* data);

/**
 * converts a uint32_t to a bytes_t in little endian format. expecting 4 bytes.
 * @param data the pointer to the first byte
 * @return the value in little endian format
 */
uint32_t uint32_from_le(uint8_t* data);

/**
 * converts a uint64_t to a bytes_t in little endian format. expecting 8 bytes.
 * @param data the pointer to the first byte
 * @return the value in little endian format
 */
uint64_t uint64_from_le(uint8_t* data);

/**
 * converts a uint64_t to a bytes_t in big endian format.
 * @param data the pointer to the first byte
 * @return the value in big endian format
 */
uint64_t uint64_from_be(uint8_t* data);

/**
 * writes 8 bytes as big endian from the given value.
 * @param data the pointer to the first byte
 * @param value the value to write
 */
void uint64_to_be(uint8_t* data, uint64_t value);

/**
 * writes 8 bytes as little endian from the given value.
 * @param data the pointer to the first byte
 * @param value the value to write
 */
void uint64_to_le(uint8_t* data, uint64_t value);

/**
 * writes 4 bytes as little endian from the given value.
 * @param data the pointer to the first byte
 * @param value the value to write
 */
void uint32_to_le(uint8_t* data, uint32_t value);

/**
 * appends the given bytes to the buffer.
 * @param buffer the buffer to append to
 * @param data the bytes to append
 * @return the new length of the buffer
 */
uint32_t buffer_append(buffer_t* buffer, bytes_t data);

/**
 * inserts or deletes a segment from or into the buffer.
 * @param buffer the buffer to insert into
 * @param offset the offset to insert at
 * @param len the length of the bytes to replace
 * @param data the bytes to insert
 */
void buffer_splice(buffer_t* buffer, size_t offset, uint32_t len, bytes_t data);

/**
 * append chars to a buffer while escaping special characters.
 * an additional NULL-Terminator will be added to the end of the buffer.
 * @param buffer the buffer to append to
 * @param data the data to append
 */
void buffer_add_chars_escaped(buffer_t* buffer, const char* data);

/**
 * append chars to a buffer.
 * An additional NULL-Terminator will be added to the end of the buffer.
 * @param buffer the buffer to append to
 * @param data the data to append
 */
void buffer_add_chars(buffer_t* buffer, const char* data);

/**
 * append a value as big endian to a buffer.
 * @param buffer the buffer to append to
 * @param value the value to append
 * @param len the length of the value to append
 */
void buffer_add_be(buffer_t* buffer, uint64_t value, uint32_t len);

/**
 * append bytes as hex chars to a buffer.
 * @param buffer the buffer to append to
 * @param data the data to append
 * @param prefix the prefix to add or NULL
 * @param suffix the suffix to add or NULL
 */
void buffer_add_hex_chars(buffer_t* buffer, bytes_t data, char* prefix, char* suffix);

/**
 * frees a buffer.
 * @param buffer the buffer to free
 */
void buffer_free(buffer_t* buffer);

/**
 * grows the buffer if needed, so it will be able to hold the min_len of bytes.
 * If the allocated is <0 and this is a fixed buffer, it will do nothing.
 * @param buffer the buffer to grow
 * @param min_len the minimum length of the buffer
 */
void buffer_grow(buffer_t* buffer, size_t min_len);

/**
 * calls malloc and check if the returned pointer is not NULL.
 * if the memory could not be allocated, the program will exit with an error message.
 * @param size the size of the memory to allocate
 * @return the pointer to the allocated memory
 */
void* safe_malloc(size_t size);

/**
 * calls calloc and check if the returned pointer is not NULL.
 * if the memory could not be allocated, the program will exit with an error message.
 * @param num the number of elements to allocate
 * @param size the size of the memory to allocate
 */
void* safe_calloc(size_t num, size_t size);

/**
 * calls realloc and check if the returned pointer is not NULL.
 * if the memory could not be allocated, the program will exit with an error message.
 * @param ptr the pointer to the memory to reallocate
 * @param new_size the new size of the memory
 */
void* safe_realloc(void* ptr, size_t new_size);

/**
 * calls free and check if the pointer is not NULL.
 * @param ptr the pointer to the memory to free
 */
void safe_free(void* ptr);

/**
 * writes to the buffer. the format is similar to printf. but those are the supported formats:
 *
 * - `%s`: char*
 * - `%S`: chars, but escaped
 * - `%x`: bytes_t as hex
 * - `%u`: bytes_t as hex without leading zeros
 * - `%c`: char as char
 * - `%j`: json_t adds as json string
 * - `%J`: json_t adds as json string , but on case of a string, the quotes are removed
 * - `%l`: uint64_t as number
 * - `%lx`: uint64_t as hex
 * - `%d`: uint32_t as number
 * - `%dx`: uint32_t as hex
 * - `%z`: ssz_ob_t as json using numbers for uint
 * - `%Z`: ssz_ob_t as json using hex without leading zeros for uint
 *
 * @param buf the buffer to write to
 * @param fmt the format string
 * @return the pointer to the start of the buffer as char*
 */
char* bprintf(buffer_t* buf, const char* fmt, ...);

void print_hex(FILE* f, bytes_t data, char* prefix, char* suffix);

/**
 * checks if all bytes in the bytes_t are equal to the given value.
 * @param a the bytes_t to check
 * @param value the value to check against
 * @return true if all bytes are equal to the value, false otherwise
 */
bool bytes_all_equal(bytes_t a, uint8_t value);

/**
 * checks if two bytes_t are equal.
 * @param a the first bytes_t to check
 * @param b the second bytes_t to check
 * @return true if the bytes_t are equal, false otherwise
 */
bool bytes_eq(bytes_t a, bytes_t b);

/**
 * duplicates a bytes_t.
 * @param data the bytes_t to duplicate
 * @return the duplicated bytes_t
 */
bytes_t bytes_dup(bytes_t data);

/**
 * writes a bytes_t to a file.
 * @param data the bytes_t to write
 * @param f the file to write to
 * @param close true if the file should be closed after writing, false otherwise
 */
void bytes_write(bytes_t data, FILE* f, bool close);

/**
 * reads a bytes_t from a file.
 * @param filename the name of the file to read
 * @return the bytes_t read from the file
 */
bytes_t bytes_read(char* filename);

/**
 * converts a hex string to a bytes_t.
 * @param hexstring the hex string to convert
 * @param len the length of the hex string
 * @param buffer the buffer to store the result
 * @return the length of the bytes written into the buffer
 */
int hex_to_bytes(const char* hexstring, int len, bytes_t buffer);

/**
 * removes leading zeros from a bytes_t.
 * @param data the bytes_t to remove leading zeros from
 * @return the bytes_t with leading zeros removed
 */
bytes_t bytes_remove_leading_zeros(bytes_t data);

/**
 * adds a variable number of bytes to a buffer.
 * @param buf the buffer to add the bytes to
 * @param len the length of the bytes to add
 * @param ... the bytes to add
 */
void buffer_add_bytes(buffer_t* buf, uint32_t len, ...);
// creates a buffer on the stack which write into given variable on the stack and ensure to stay within the bounds of the variable
#define stack_buffer(stack_var) \
  (buffer_t) { .data = bytes((uint8_t*) stack_var, 0), .allocated = -((int32_t) sizeof(stack_var)) }

#define buffer_as_string(buffer) (char*) buffer.data.data
#define bytes_as_string(bytes)   (char*) bytes.data

#define buffer_for_size(init_size) \
  (buffer_t) { .data = NULL_BYTES, .allocated = init_size }

#define buffer_reset(buf) (buf)->data.len = 0

#define bytes(ptr, length) \
  (bytes_t) { .data = (uint8_t*) ptr, .len = length }
#define bytes_slice(parent, offet, length) \
  (bytes_t) { .data = parent.data + offet, .len = length }
#define bytes_all_zero(a) bytes_all_equal(a, 0)

#ifdef __cplusplus
}
#endif

#endif