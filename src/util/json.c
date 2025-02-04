#include "json.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#define json(jtype, data, length) \
  (json_t) { .type = jtype, .start = data, .len = length }

static char* next_non_whitespace_token(char* data) {
  while (*data && isspace(*data)) data++;
  return *data ? data : NULL;
}

static char* find_end(char* pos, char start, char end) {
  int  level     = 1;
  bool in_string = start == '"';
  for (; *pos; pos++) {
    if (in_string && *pos == '\\') {
      if (!(*(++pos))) return NULL;
      continue;
    }
    if (*pos == start && !in_string)
      level++;
    else if (*pos == end && !in_string) {
      level--;
      if (!level) return pos;
    }
    else if (*pos == '"') {
      in_string = !in_string;
      if (!in_string && *pos == end) return pos;
    }
  }
  return NULL;
}

static json_t parse_number(char* start) {
  json_t json = json(JSON_TYPE_NUMBER, start, 0);
  for (; *start; start++) {
    if (isdigit(*start) || *start == '.' || *start == '-' || *start == 'e' || *start == 'E')
      json.len++;
    else
      break;
  }
  return json;
}

json_t json_parse(const char* data) {
  json_t invalid = json(JSON_TYPE_INVALID, data, 0);
  char*  start   = next_non_whitespace_token(data);
  char*  end     = NULL;

  if (!start) return invalid;
  switch (*start) {
    case '{':
      end = find_end(start + 1, '{', '}');
      return end ? json(JSON_TYPE_OBJECT, start, end - start + 1) : invalid;
    case '[':
      end = find_end(start + 1, '[', ']');
      return end ? json(JSON_TYPE_ARRAY, start, end - start + 1) : invalid;
    case '"':
      end = find_end(start + 1, '"', '"');
      return end ? json(JSON_TYPE_STRING, start, end - start + 1) : invalid;
    case 't':
      return strncmp(start, "true", 4) ? invalid : json(JSON_TYPE_BOOLEAN, start, 4);
    case 'f':
      return strncmp(start, "false", 5) ? invalid : json(JSON_TYPE_BOOLEAN, start, 5);
    case 'n':
      return strncmp(start, "null", 4) ? invalid : json(JSON_TYPE_NULL, start, 4);
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      return parse_number(start);
    default:
      return invalid;
  }
  return invalid;
}

json_t json_next_value(json_t val, bytes_t* property_name, json_next_t type) {
  char* start = next_non_whitespace_token(val.start + (type == JSON_NEXT_FIRST ? 1 : val.len));
  if (!start) return json(JSON_TYPE_INVALID, start, 0);
  if (type != JSON_NEXT_FIRST) {
    if (*start == ',') {
      start++;
      start = next_non_whitespace_token(start);
      if (!start) return json(JSON_TYPE_INVALID, val.start, 0);
    }
  }
  else
    type = val.type == JSON_TYPE_OBJECT ? JSON_NEXT_PROPERTY : JSON_NEXT_VALUE;
  if (*start == '}' || *start == ']') return json(JSON_TYPE_NOT_FOUND, start, 0);

  if (type == JSON_NEXT_PROPERTY) {
    if (*start != '"') return json(JSON_TYPE_INVALID, start, 0);
    char* end_name = find_end(start + 1, '"', '"');
    if (!end_name) return json(JSON_TYPE_INVALID, start, 0);
    if (property_name) {
      property_name->data = (uint8_t*) start + 1;
      property_name->len  = end_name - start - 1;
    }
    start = next_non_whitespace_token(end_name + 1);
    if (!start || *start != ':') return json(JSON_TYPE_INVALID, start, 0);
    start = next_non_whitespace_token(start + 1);
  }

  return json_parse(start);
}

json_t json_get(json_t parent, const char* property) {
  if (parent.type != JSON_TYPE_OBJECT) return json(JSON_TYPE_INVALID, parent.start, 0);
  bytes_t property_name = NULL_BYTES;
  size_t  len           = strlen(property);
  json_for_each_property(parent, val, property_name) {
    if (property_name.len == len && property_name.data && memcmp(property_name.data, property, len) == 0) return val;
  }
  return json(JSON_TYPE_NOT_FOUND, parent.start, 0);
}

json_t json_at(json_t parent, size_t index) {
  if (parent.type != JSON_TYPE_ARRAY) return json(JSON_TYPE_INVALID, parent.start, 0);
  size_t i = 0;
  json_for_each_value(parent, val) {
    if (index == i++) return val;
  }
  return json(JSON_TYPE_NOT_FOUND, parent.start, 0);
}

size_t json_len(json_t parent) {
  if (parent.type != JSON_TYPE_ARRAY) return 0;
  size_t i = 0;
  json_for_each_value(parent, val) i++;
  return i;
}

char* json_as_string(json_t val, buffer_t* buffer) {
  buffer->data.len = 0;
  buffer_grow(buffer, val.len);
  if (val.type == JSON_TYPE_STRING)
    buffer_append(buffer, bytes((uint8_t*) val.start + 1, val.len - 2));
  else
    buffer_append(buffer, bytes((uint8_t*) val.start, val.len));

  if (buffer->data.len >= val.len - 1)
    buffer->data.data[val.len - 2] = '\0';
  return (char*) buffer->data.data;
}

uint64_t json_as_uint64(json_t val) {
  uint8_t  tmp[20] = {0};
  buffer_t buffer  = stack_buffer(tmp);
  if (val.len > 5 && val.start[1] == '0' && val.start[1] == 'x') {
    int len = hex_to_bytes(val.start + 1, val.len - 2, bytes(tmp, 20));
    if (len == -1) return 0;
    memmove(tmp + 8 - len, tmp, len);
    memset(tmp, 0, 8 - len);
    return uint64_from_be(tmp);
  }
  return (uint64_t) strtoull(json_as_string(val, &buffer), NULL, 10);
}

bytes_t json_as_bytes(json_t val, buffer_t* buffer) {
  if (val.type == JSON_TYPE_NUMBER) {
    buffer->data.len = 8;
    buffer_grow(buffer, 8);
    uint64_to_be(buffer->data.data, json_as_uint64(val));
    return buffer->data;
  }

  buffer_grow(buffer, val.len / 2);
  buffer->data.len = val.len;
  int len          = hex_to_bytes(val.start + 1, val.len - 2, buffer->data);
  if (len == -1) return NULL_BYTES;
  buffer->data.len = (uint32_t) len;
  return buffer->data;
}

bool json_as_bool(json_t val) {
  return val.type == JSON_TYPE_BOOLEAN && val.start[0] == 't';
}

bool json_as_null(json_t val) {
  return val.type == JSON_TYPE_NULL && val.start[0] == 'n';
}

void buffer_add_json(buffer_t* buffer, json_t data) {
  buffer_grow(buffer, buffer->data.len + data.len + 1);
  buffer_append(buffer, bytes((uint8_t*) data.start, data.len));
  buffer->data.data[buffer->data.len] = 0;
}

char* json_new_string(json_t parent) {
  char* new_str = (char*) malloc(parent.len + 1);
  if (!new_str) return NULL;
  memcpy(new_str, parent.start, parent.len);
  new_str[parent.len] = '\0';
  return new_str;
}