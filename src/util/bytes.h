
#ifndef bytes_h__
#define bytes_h__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
  uint8_t* data;
  uint32_t len;
} bytes_t;

#define NULL_BYTES (bytes_t){.data = NULL, .len = 0}

typedef struct {
  bytes_t data;
  int32_t allocated; // 0: not allocated, >0: allocated or f bytes.data == NULL, this would be the inital size, <0: fixed memory - don 't grow and don't go over the limit!
} buffer_t;

uint64_t bytes_as_le(bytes_t data);
uint64_t bytes_as_be(bytes_t data);
uint16_t uint16_from_le(uint8_t* data);
uint32_t uint32_from_le(uint8_t* data);
uint64_t uint64_from_le(uint8_t* data);
uint64_t uint64_from_be(uint8_t* data);
void     uint64_to_be(uint8_t* data, uint64_t value);
void     uint64_to_le(uint8_t* data, uint64_t value);
void     uint32_to_le(uint8_t* data, uint32_t value);
uint32_t buffer_append(buffer_t* buffer, bytes_t data);
void     buffer_splice(buffer_t* buffer, size_t offset, uint32_t len, bytes_t data);
void     buffer_add_chars(buffer_t* buffer, const char* data);
void     buffer_add_be(buffer_t* buffer, uint64_t value, uint32_t len);
void     buffer_add_hex_chars(buffer_t* buffer, bytes_t data, char* prefix, char* suffix);
void     buffer_free(buffer_t* buffer);
void     buffer_grow(buffer_t* buffer, size_t min_len);
char*    bprintf(buffer_t* buf, const char* fmt, ...);

void print_hex(FILE* f, bytes_t data, char* prefix, char* suffix);

bool    bytes_all_equal(bytes_t a, uint8_t value);
bool    bytes_eq(bytes_t a, bytes_t b);
bytes_t bytes_dup(bytes_t data);
void    bytes_write(bytes_t data, FILE* f, bool close);
bytes_t bytes_read(char* filename);
int     hex_to_bytes(const char* hexstring, int len, bytes_t buffer);

bytes_t bytes_remove_leading_zeros(bytes_t data);
void    buffer_add_bytes(buffer_t* buf, uint32_t len, ...);
// creates a buffer on the stack which write into given variable on the stack and ensure to stay within the bounds of the variable
#define stack_buffer(stack_var) \
  (buffer_t) { .data = bytes((uint8_t*) stack_var, 0), .allocated = -((int32_t) sizeof(stack_var)) }

#define buffer_as_string(buffer) (char*) buffer.data.data
#define bytes_as_string(bytes)   (char*) bytes.data

#define buffer_for_size(init_size) \
  (buffer_t) { .data = NULL_BYTES, .allocated = init_size }

#define bytes(ptr, length) \
  (bytes_t) { .data = (uint8_t*) ptr, .len = length }
#define bytes_slice(parent, offet, length) \
  (bytes_t) { .data = parent.data + offet, .len = length }
#define bytes_all_zero(a) bytes_all_equal(a, 0)

#ifdef __cplusplus
}
#endif

#endif