#ifndef ssz_h__
#define ssz_h__

#ifdef __cplusplus
extern "C" {
#endif


#include <stdint.h>
#include "bytes.h"
#include <stdio.h>
#include <stdbool.h>

// Forward declarations
typedef struct ssz_def ssz_def_t;
typedef struct ssz_list ssz_list_t;
typedef struct ssz_container ssz_container_t;

// Define ssz_type_t enum
typedef enum {
    SSZ_TYPE_UINT      = 0,
    SSZ_TYPE_BOOLEAN   = 1,
    SSZ_TYPE_CONTAINER = 2,
    SSZ_TYPE_VECTOR    = 3,
    SSZ_TYPE_LIST      = 4,
    SSZ_TYPE_BIT_VECTOR = 5,
    SSZ_TYPE_BIT_LIST = 6,
    SSZ_TYPE_UNION  = 7,
} ssz_type_t;

// Define ssz_uint_t
typedef struct {
    uint32_t len;
} ssz_uint_t;

// Define ssz_list_t
struct ssz_list {
    uint32_t len;
    ssz_def_t* type; // Use a pointer to ssz_def_t
};

// Define ssz_container_t
struct ssz_container {
    ssz_def_t* elements; // Use a pointer to ssz_def_t
    uint32_t len;
};

// Define ssz_def_t
struct ssz_def {
    char* name;
    ssz_type_t type;
    union {
        ssz_uint_t uint;
        ssz_container_t container;
        ssz_list_t vector;
    } def;
};



typedef struct {
    bytes_t bytes;
    ssz_def_t* def;
} ssz_ob_t;
#define ssz_ob(typename, data) (ssz_ob_t){.bytes=data, .def=&typename}


static inline uint64_t ssz_uint64(ssz_ob_t ob) {
    return ob.bytes.len==8 ? uint64_from_le(ob.bytes.data) : 0;
}

static inline uint32_t ssz_uint32(ssz_ob_t ob) {
    return ob.bytes.len==4 ? uint32_from_le(ob.bytes.data) : 0;
}

uint32_t ssz_len(ssz_ob_t ob);

static inline bytes_t ssz_bytes(ssz_ob_t ob) {
    return ob.bytes;
}

ssz_ob_t ssz_at(ssz_ob_t ob, uint32_t index) ;


ssz_ob_t ssz_get(ssz_ob_t* ob, char* name);

void ssz_dump(FILE *f, ssz_ob_t ob, bool include_name, int intend);

extern const ssz_def_t ssz_uint8;

#define SSZ_UINT(property, length) { .name = property, .type = SSZ_TYPE_UINT, .def.uint = {.len = length} }
#define SSZ_LIST(property, typePtr) { .name = property, .type = SSZ_TYPE_LIST, .def.vector = {.len = 0, .type=(ssz_def_t*)&typePtr} }
#define SSZ_VECTOR(property, typePtr,length) { .name = property, .type = SSZ_TYPE_VECTOR, .def.vector = {.len = length, .type=(ssz_def_t*)&typePtr} }
#define SSZ_CONTAINER(propname, children) \
    { \
        .name = propname,  \
        .type = SSZ_TYPE_CONTAINER, \
        .def.container = { .elements = children, .len = sizeof(children) / sizeof(ssz_def_t) } \
    }
#define SSZ_BYTE ssz_uint8
#define SSZ_BYTES(name) SSZ_LIST(name,ssz_uint8)
#define SSZ_BYTE_VECTOR(name,len) SSZ_VECTOR(name,ssz_uint8,len)


typedef struct {
    ssz_def_t* def;
    bytes_buffer_t fixed;
    bytes_buffer_t dynamic;
} ssz_buffer_t;

void ssz_add_bytes(ssz_buffer_t* buffer, char* name, bytes_t data);
void ssz_add_uint64(ssz_buffer_t* buffer, uint64_t value);
void ssz_add_uint32(ssz_buffer_t* buffer, uint32_t value);
void ssz_add_uint16(ssz_buffer_t* buffer, uint16_t value);
void ssz_add_uint8(ssz_buffer_t* buffer, uint8_t value);
void ssz_buffer_free(ssz_buffer_t* buffer);
// converts the ssz_buffer to bytes and the frees up the buffer
// make sure to free the returned after using 
ssz_ob_t ssz_buffer_to_bytes(ssz_buffer_t* buffer);
#ifdef __cplusplus
}
#endif

#endif