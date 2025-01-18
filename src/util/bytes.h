
#ifndef bytes_h__
#define bytes_h__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint8_t* data;
    uint32_t len;
} bytes_t;

typedef struct {
    bytes_t data;
    uint32_t allocated;
} bytes_buffer_t;

uint16_t uint16_from_le(uint8_t* data);
uint32_t uint32_from_le(uint8_t* data);
uint64_t uint64_from_le(uint8_t* data);

void buffer_append(bytes_buffer_t* buffer, bytes_t data);
void buffer_free(bytes_buffer_t* buffer);
void buffer_grow(bytes_buffer_t* buffer, size_t min_len);

#define bytes(ptr,length) (bytes_t){.data=ptr, .len=length}
#define bytes_slice(parent, offet, length) (bytes_t){.data=parent.data+offet, .len=length}
void print_hex(FILE *f, bytes_t data, char* prefix, char* suffix);

typedef uint8_t address_t[20]; 
typedef uint8_t bytes32_t[32]; 

#ifdef __cplusplus
}
#endif

#endif