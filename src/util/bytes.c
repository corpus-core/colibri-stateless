#include "./bytes.h"
#include "./compat.h" /* Include our compatibility header for PRI* macros */
#include "./json.h"
#include "ssz.h"
#include <ctype.h>
#include <errno.h> // For errno
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Safe Memory Allocation Wrappers ---

void* safe_malloc(size_t size) {
  void* ptr = malloc(size);
  if (size > 0 && ptr == NULL) {
    fprintf(stderr, "Error: Memory allocation failed (malloc) for size %zu bytes: %s. Exiting.\\n", size, strerror(errno));
    exit(EXIT_FAILURE);
  }
  return ptr;
}

void* safe_calloc(size_t num, size_t size) {
  void* ptr = calloc(num, size);
  if (num > 0 && size > 0 && ptr == NULL) {
    fprintf(stderr, "Error: Memory allocation failed (calloc) for %zu items of size %zu bytes: %s. Exiting.\\n", num, size, strerror(errno));
    exit(EXIT_FAILURE);
  }
  return ptr;
}

void* safe_realloc(void* ptr, size_t new_size) {
  void* new_ptr = realloc(ptr, new_size);
  // Note:safe_realloc(NULL, size) is equivalent tosafe_malloc(size)
  // safe_realloc(ptr, 0) is equivalent tosafe_free(ptr) and may return NULL
  if (new_size > 0 && new_ptr == NULL) {
    fprintf(stderr, "Error: Memory allocation failed (realloc) for new size %zu bytes: %s. Exiting.\\n", new_size, strerror(errno));
    // Important: The original block ptr is NOT freed by realloc if it fails
    exit(EXIT_FAILURE);
  }
  return new_ptr;
}

void safe_free(void* ptr) {
  free(ptr); // safe_free(NULL) is safe and does nothing
}

// --- End Safe Memory Allocation Wrappers ---

uint32_t uint32_from_le(uint8_t* data) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  // Check alignment before dereferencing
  if (((uintptr_t) data % sizeof(uint32_t)) == 0) {
    // Safe to dereference
    return *(uint32_t*) data;
  }
  else {
    // Fallback to manual byte manipulation
    return (uint32_t) data[0] |
           ((uint32_t) data[1] << 8) |
           ((uint32_t) data[2] << 16) |
           ((uint32_t) data[3] << 24);
  }
#else
  // Manual byte manipulation for big-endian or unknown endianness
  return (uint32_t) data[0] |
         ((uint32_t) data[1] << 8) |
         ((uint32_t) data[2] << 16) |
         ((uint32_t) data[3] << 24);
#endif
}

uint16_t uint16_from_le(uint8_t* data) {
  return (uint16_t) data[0] |
         ((uint16_t) data[1] << 8);
}

uint64_t uint64_from_le(uint8_t* data) {
  return (uint64_t) data[0] |
         ((uint64_t) data[1] << 8) |
         ((uint64_t) data[2] << 16) |
         ((uint64_t) data[3] << 24) |
         ((uint64_t) data[4] << 32) |
         ((uint64_t) data[5] << 40) |
         ((uint64_t) data[6] << 48) |
         ((uint64_t) data[7] << 56);
}

uint64_t uint64_from_be(uint8_t* data) {
  return (uint64_t) data[7] |
         ((uint64_t) data[6] << 8) |
         ((uint64_t) data[5] << 16) |
         ((uint64_t) data[4] << 24) |
         ((uint64_t) data[3] << 32) |
         ((uint64_t) data[2] << 40) |
         ((uint64_t) data[1] << 48) |
         ((uint64_t) data[0] << 56);
}
void uint64_to_be(uint8_t* data, uint64_t value) {
  data[0] = (value >> 56) & 0xFF;
  data[1] = (value >> 48) & 0xFF;
  data[2] = (value >> 40) & 0xFF;
  data[3] = (value >> 32) & 0xFF;
  data[4] = (value >> 24) & 0xFF;
  data[5] = (value >> 16) & 0xFF;
  data[6] = (value >> 8) & 0xFF;
  data[7] = value & 0xFF;
}

void uint64_to_le(uint8_t* data, uint64_t value) {
  data[7] = (value >> 56) & 0xFF;
  data[6] = (value >> 48) & 0xFF;
  data[5] = (value >> 40) & 0xFF;
  data[4] = (value >> 32) & 0xFF;
  data[3] = (value >> 24) & 0xFF;
  data[2] = (value >> 16) & 0xFF;
  data[1] = (value >> 8) & 0xFF;
  data[0] = value & 0xFF;
}

void uint32_to_le(uint8_t* data, uint32_t value) {
  data[0] = value & 0xFF;
  data[1] = (value >> 8) & 0xFF;
  data[2] = (value >> 16) & 0xFF;
  data[3] = (value >> 24) & 0xFF;
}

void buffer_grow(buffer_t* buffer, size_t min_len) {
  if (buffer->data.data == NULL) {
    if (buffer->allocated > 0 && (size_t) buffer->allocated > min_len) min_len = (size_t) buffer->allocated;
    buffer->data.data = safe_malloc(min_len);
    buffer->allocated = (int32_t) min_len;
  }
  else if (buffer->allocated >= 0 && (size_t) buffer->allocated < min_len) {
    size_t new_len = (size_t) buffer->allocated;
    while (new_len < min_len) new_len = (new_len + 1) * 3 / 2;
    buffer->data.data = safe_realloc(buffer->data.data, new_len);
    buffer->allocated = (int32_t) new_len;
  }
}

uint32_t buffer_append(buffer_t* buffer, bytes_t data) {
  if (buffer->allocated < 0 && buffer->data.len + data.len > (uint32_t) (0 - buffer->allocated))
    data.len = ((uint32_t) (0 - buffer->allocated)) - buffer->data.len;
  if (!data.len) return 0;
  buffer_grow(buffer, buffer->data.len + data.len);
  if (data.data)
    memcpy(buffer->data.data + buffer->data.len, data.data, data.len);
  else
    memset(buffer->data.data + buffer->data.len, 0, data.len);
  buffer->data.len += data.len;
  return data.len;
}

void buffer_splice(buffer_t* buffer, size_t offset, uint32_t len, bytes_t data) {
  buffer_grow(buffer, buffer->data.len + data.len - len);
  uint32_t old_end_offset = offset + len;
  uint32_t new_end_offset = offset + data.len;
  // TODO add preallocated check
  if (new_end_offset != old_end_offset && buffer->data.len - old_end_offset > 0)
    memmove(buffer->data.data + new_end_offset, buffer->data.data + old_end_offset, buffer->data.len - old_end_offset);

  if (data.len) {
    if (data.data)
      memcpy(buffer->data.data + offset, data.data, data.len);
    else
      memset(buffer->data.data + offset, 0, data.len);
  }

  buffer->data.len = buffer->data.len + data.len - len;
}

void buffer_free(buffer_t* buffer) {
  if (buffer->data.data && buffer->allocated > 0)
    safe_free(buffer->data.data);
  buffer->data.data = NULL;
  buffer->allocated = 0;
  buffer->data.len  = 0;
}

void print_hex(FILE* f, bytes_t data, char* prefix, char* suffix) {
  if (prefix) fprintf(f, "%s", prefix);
  for (uint32_t i = 0; i < data.len; i++)
    fprintf(f, "%02x", data.data[i]);
  if (suffix) fprintf(f, "%s", suffix);
}

bool bytes_all_equal(bytes_t a, uint8_t value) {
  for (uint32_t i = 0; i < a.len; i++)
    if (a.data[i] != value) return false;
  return true;
}

static inline int hexchar_to_int(char c) {
  if (isdigit(c)) return c - '0';
  if (isxdigit(c)) return tolower(c) - 'a' + 10;
  return -1; // Ungültiges Zeichen
}

int hex_to_bytes(const char* hexstring, int len, bytes_t buffer) {
  size_t hex_len = len == -1 ? strlen(hexstring) : (size_t) len;
  if (!hexstring || !buffer.data) return -1;
  int dst_offset = hex_len % 2;
  int src_offset = (hexstring[0] == '0' && hexstring[1] == 'x') ? 2 : 0;
  if (dst_offset) buffer.data[0] = hexchar_to_int(hexstring[src_offset++]);

  if ((hex_len - src_offset) % 2 || (buffer.len - dst_offset) < (hex_len - src_offset) / 2)
    return -1;

  for (size_t i = src_offset; i < hex_len; i += 2) {
    int high = hexchar_to_int(hexstring[i]);
    int low  = hexchar_to_int(hexstring[i + 1]);
    if (high == -1 || low == -1) return -1;
    buffer.data[dst_offset++] = (high << 4) | low;
  }

  return dst_offset;
}

void buffer_add_chars(buffer_t* buffer, const char* data) {
  if (!data) return;
  size_t len = strlen(data);
  buffer_append(buffer, bytes((uint8_t*) data, len + 1));
  buffer->data.len -= 1;
}

void buffer_add_hex_chars(buffer_t* buffer, bytes_t data, char* prefix, char* suffix) {
  uint32_t len = data.len * 2 + (prefix ? strlen(prefix) : 0) + (suffix ? strlen(suffix) : 0);
  buffer_grow(buffer, buffer->data.len + len + 1);
  buffer_add_chars(buffer, prefix);
  char tmp[4];

  for (size_t i = 0; i < data.len; i++) {
    sprintf(tmp, "%02x", data.data[i]);
    buffer_add_chars(buffer, tmp);
  }
  buffer_add_chars(buffer, suffix);
}

bytes_t bytes_dup(bytes_t data) {
  bytes_t result = {.data = safe_malloc(data.len), .len = data.len};
  if (!result.data && data.len > 0) {
    // This case should technically not be reached if safe_malloc exits on failure,
    // but kept for conceptual completeness or if safe_malloc behaviour changes.
    return NULL_BYTES; // Indicate failure if malloc fails
  }
  memcpy(result.data, data.data, data.len);
  return result;
}

void bytes_write(bytes_t data, FILE* f, bool close) {
  if (!f) return;
  fwrite(data.data, 1, data.len, f);
  if (close && f != stdout && f != stderr) fclose(f);
}

bytes_t bytes_read(char* filename) {
  unsigned char buffer[1024];
  size_t        bytesRead;
  buffer_t      data = {0};

  FILE* file = strcmp(filename, "-") ? fopen(filename, "rb") : stdin;
  if (file == NULL) return NULL_BYTES;

  while ((bytesRead = fread(buffer, 1, 1024, file)) > 0)
    buffer_append(&data, bytes(buffer, bytesRead));

  buffer_append(&data, bytes(NULL, 1));
  data.data.len--;

  if (file != stdin)
    fclose(file);
  return data.data;
}

void buffer_add_bytes(buffer_t* buf, uint32_t len, ...) {
  buffer_grow(buf, buf->data.len + len);
  va_list args;
  va_start(args, len);
  for (uint32_t i = 0; i < len; i++) {
    buf->data.data[buf->data.len] = (uint8_t) va_arg(args, int);
    buf->data.len++;
  }
  va_end(args);
}

void buffer_add_be(buffer_t* buffer, uint64_t value, uint32_t len) {
  buffer_grow(buffer, buffer->data.len + len);
  uint32_t s = buffer->data.len;
  for (uint32_t i = 0; i < len; i++)
    buffer->data.data[s + len - i - 1] = (value >> (i << 3)) & 0xFF;
  buffer->data.len += len;
}

char* bprintf(buffer_t* buf, const char* fmt, ...) {
  buffer_t tmp_buf = {0};
  if (buf == NULL) buf = &tmp_buf;
  va_list args;
  va_start(args, fmt);
  const char* last_pos = fmt;
  const char* p;
  for (p = fmt; *p; p++) {
    if (*p == '%') {
      if (p != last_pos) buffer_append(buf, bytes((uint8_t*) last_pos, p - last_pos));
      switch (*(p + 1)) {
        case 's':
          buffer_add_chars(buf, va_arg(args, const char*));
          break;
        case 'x':
        case 'b':
          buffer_add_hex_chars(buf, va_arg(args, bytes_t), NULL, NULL);
          break;
        case 'u': {
          uint32_t len = buf->data.len;
          buffer_add_hex_chars(buf, bytes_remove_leading_zeros(va_arg(args, bytes_t)), NULL, NULL);
          if (buf->data.data[len] == '0') {
            buffer_splice(buf, len, 1, bytes(NULL, 0));
            buf->data.data[buf->data.len] = 0;
          }
          break;
        }
        case 'J': {
          json_t val = va_arg(args, json_t);
          buffer_append(buf, bytes((uint8_t*) val.start, val.len));
          break;
        }
        case 'j': {
          json_t val = va_arg(args, json_t);
          if (val.type == JSON_TYPE_STRING)
            buffer_append(buf, bytes((uint8_t*) val.start + 1, val.len - 2));
          else
            buffer_append(buf, bytes((uint8_t*) val.start, val.len));
          break;
        }
        case 'l': {
          uint64_t value   = va_arg(args, uint64_t);
          char     tmp[20] = {0};
          if (*(p + 2) == 'x') {
            p++;
            if (!*(p + 1)) break;
            sprintf(tmp, "%" PRIx64, value);
          }
          else
            sprintf(tmp, "%" PRIu64, value);
          buffer_add_chars(buf, tmp);
          break;
        }
        case 'd': {
          uint32_t value   = va_arg(args, uint32_t);
          char     tmp[20] = {0};
          if (*(p + 2) == 'x') {
            p++;
            if (!*(p + 1)) break;
            sprintf(tmp, "%" PRIx32, value);
          }
          else
            sprintf(tmp, "%" PRIu32, value);
          buffer_add_chars(buf, tmp);
          break;
        }
        case 'c': {
          char c = va_arg(args, int);
          buffer_append(buf, bytes((uint8_t*) &c, 1));
          break;
        }
        case 'z': {
          char* s = ssz_dump_to_str(va_arg(args, ssz_ob_t), false, false);
          buffer_add_chars(buf, s);
          safe_free(s);
          break;
        }
        case 'Z': {
          char* s = ssz_dump_to_str(va_arg(args, ssz_ob_t), false, true);
          buffer_add_chars(buf, s);
          safe_free(s);
          break;
        }
      }
      p++;
      last_pos = p + 1;
      if (!*(p + 1)) break;
    }
  }
  va_end(args);
  if (last_pos != p)
    buffer_add_chars(buf, last_pos);
  else if (buffer_append(buf, bytes(NULL, 1)))
    buf->data.len--;
  return (char*) buf->data.data;
}

bytes_t bytes_remove_leading_zeros(bytes_t data) {
  while (data.len > 1 && data.data[0] == 0) {
    data.len--;
    data.data++;
  }
  return data;
}

bool bytes_eq(bytes_t a, bytes_t b) {
  if (a.len != b.len) return false;
  for (uint32_t i = 0; i < a.len; i++)
    if (a.data[i] != b.data[i]) return false;
  return true;
}
uint64_t bytes_as_le(bytes_t data) {
  uint64_t result = 0;
  for (uint32_t i = 0; i < data.len; i++)
    result = (result << 8) | data.data[data.len - i - 1];
  return result;
}
uint64_t bytes_as_be(bytes_t data) {
  uint64_t result = 0;
  for (uint32_t i = 0; i < data.len; i++)
    result = (result << 8) | data.data[i];
  return result;
}
