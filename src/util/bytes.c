#include "./bytes.h"
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
uint32_t uint32_from_le(uint8_t* data) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    // Check alignment before dereferencing
    if (((uintptr_t)data % sizeof(uint32_t)) == 0) {
        // Safe to dereference
        return *(uint32_t*)data;
    } else {
        // Fallback to manual byte manipulation
        return (uint32_t)data[0] |
               ((uint32_t)data[1] << 8) |
               ((uint32_t)data[2] << 16) |
               ((uint32_t)data[3] << 24);
    }
#else
    // Manual byte manipulation for big-endian or unknown endianness
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
#endif
}

uint16_t uint16_from_le(uint8_t* data) {
    return (uint16_t)data[0] |
           ((uint16_t)data[1] << 8);
}

uint64_t uint64_from_le(uint8_t* data) {
    return (uint64_t)data[0] |
           ((uint64_t)data[1] << 8) |
           ((uint64_t)data[2] << 16) |
           ((uint64_t)data[3] << 24) |
           ((uint64_t)data[4] << 32) |
           ((uint64_t)data[5] << 40) |
           ((uint64_t)data[6] << 48) |
           ((uint64_t)data[7] << 56);
}


void buffer_grow(bytes_buffer_t* buffer, size_t min_len) {
    if (buffer->data.data==NULL) {
        buffer->data.data = malloc(min_len);
        buffer->allocated = min_len;
    }
    else if (buffer->allocated<min_len) {
        size_t new_len = buffer->allocated;
        while (new_len<min_len) new_len = new_len*3/2;
        buffer->data.data=realloc(buffer->data.data, new_len);
        buffer->allocated = new_len;
    }
}

void buffer_append(bytes_buffer_t* buffer, bytes_t data) {
    if (!data.len) return;
    buffer_grow(buffer, buffer->data.len+data.len);
    if (data.data)
        memcpy(buffer->data.data+buffer->data.len,data.data,data.len);
    else
        memset(buffer->data.data+buffer->data.len,0,data.len);
    buffer->data.len+=data.len;
}

void buffer_free(bytes_buffer_t* buffer) {
    if (buffer->data.data && buffer->allocated)
        free(buffer->data.data);
}

void print_hex(FILE *f, bytes_t data, char* prefix, char* suffix) {
    if (prefix) fprintf(f,"%s",prefix);
    for (uint32_t i=0;i<data.len;i++)
        fprintf(f,"%02x",data.data[i]);
    if (suffix) fprintf(f,"%s",suffix);
}
