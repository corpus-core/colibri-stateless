#ifndef json_h__
#define json_h__

#include "bytes.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum json_type_t {
  JSON_TYPE_INVALID   = 0,
  JSON_TYPE_STRING    = 1,
  JSON_TYPE_NUMBER    = 2,
  JSON_TYPE_OBJECT    = 3,
  JSON_TYPE_ARRAY     = 4,
  JSON_TYPE_BOOLEAN   = 5,
  JSON_TYPE_NULL      = 6,
  JSON_TYPE_NOT_FOUND = -1
} json_type_t;

typedef struct json_t {
  char*       start;
  size_t      len;
  json_type_t type;
} json_t;

typedef enum json_next_t {
  JSON_NEXT_FIRST,
  JSON_NEXT_PROPERTY,
  JSON_NEXT_VALUE,
} json_next_t;

json_t json_next_value(json_t val, bytes_t* property_name, json_next_t type);

json_t   json_parse(char* data);
json_t   json_get(json_t parent, char* property);
json_t   json_at(json_t parent, size_t index);
size_t   json_len(json_t parent);
char*    json_as_string(json_t parent, buffer_t* buffer);
bytes_t  json_as_bytes(json_t parent, buffer_t* buffer);
uint64_t json_as_uint64(json_t val);
bool     json_as_bool(json_t val);
bool     json_is_null(json_t val);

#define json_for_each_property(parent, val, property_name)                    \
  for (json_t val = json_next_value(parent, &property_name, JSON_NEXT_FIRST); \
       val.type != JSON_TYPE_NOT_FOUND && val.type != JSON_TYPE_INVALID;      \
       val = json_next_value(val, &property_name, JSON_NEXT_PROPERTY))

#define json_for_each_value(parent, val)                                 \
  for (json_t val = json_next_value(parent, NULL, JSON_NEXT_FIRST);      \
       val.type != JSON_TYPE_NOT_FOUND && val.type != JSON_TYPE_INVALID; \
       val = json_next_value(val, NULL, JSON_NEXT_VALUE))

#ifdef __cplusplus
}
#endif

#endif
