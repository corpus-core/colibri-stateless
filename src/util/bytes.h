
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
uint64_t uint64_from_le(uint8_t* data) ;

void buffer_append(bytes_buffer_t* buffer, bytes_t data);
void buffer_free(bytes_buffer_t* buffer);

#define bytes(ptr,length) (bytes_t){.data=ptr, .len=length}
void print_hex(FILE *f, bytes_t data, char* prefix, char* suffix);

#ifdef __cplusplus
}
#endif

#endif