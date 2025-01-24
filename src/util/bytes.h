
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
} bytes_buffer_t;

uint16_t uint16_from_le(uint8_t* data);
uint32_t uint32_from_le(uint8_t* data);
uint64_t uint64_from_le(uint8_t* data);
uint64_t uint64_from_be(uint8_t* data);
void     uint64_to_be(uint8_t* data, uint64_t value);

void buffer_append(bytes_buffer_t* buffer, bytes_t data);
void buffer_add_chars(bytes_buffer_t* buffer, char* data);
void buffer_add_hex_chars(bytes_buffer_t* buffer, bytes_t data, char* prefix, char* suffix);
void buffer_free(bytes_buffer_t* buffer);
void buffer_grow(bytes_buffer_t* buffer, size_t min_len);
void print_hex(FILE* f, bytes_t data, char* prefix, char* suffix);
bool bytes_all_equal(bytes_t a, uint8_t value);
int  hex_to_bytes(const char* hexstring, int len, bytes_t buffer);

// creates a buffer on the stack which write into given variable on the stack and ensure to stay within the bounds of the variable
#define bytes_stack_buffer(stack_var) \
  (bytes_buffer_t) { .data = bytes(stack_var, 0), .allocated = -((int32_t) sizeof(stack_var)) }

#define bytes_buffer_for_size(init_size) \
  (bytes_buffer_t) { .data = NULL_BYTES, .allocated = init_size }

#define bytes(ptr, length) \
  (bytes_t) { .data = ptr, .len = length }
#define bytes_slice(parent, offet, length) \
  (bytes_t) { .data = parent.data + offet, .len = length }
#define bytes_all_zero(a) bytes_all_equal(a, 0)

#ifdef __cplusplus
}
#endif

#endif