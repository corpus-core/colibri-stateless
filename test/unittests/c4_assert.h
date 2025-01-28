#include "../../src/proofer/ssz_types.h"
#include "../../src/util/bytes.h"
#include "../../src/util/crypto.h"
#include "../../src/util/ssz.h"
#include "../../src/verifier/types_verify.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_HEX_STRING_EQUAL(expected_hex, actual_array, size, message)              \
  do {                                                                                  \
    uint8_t expected_bytes[size];                                                       \
    hex_to_bytes(expected_hex, -1, bytes(expected_bytes, size));                        \
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected_bytes, actual_array, size, message); \
  } while (0)

static bytes_t read_testdata(const char* filename) {
  unsigned char buffer[1024];
  size_t        bytesRead;
  buffer_t      data = {0};
  buffer_t      path = {0};
  buffer_add_chars(&path, TESTDATA_DIR "/");
  buffer_add_chars(&path, filename);

  FILE* file = fopen((char*) path.data.data, "rb");
  buffer_free(&path);
  if (file == NULL) {
    fprintf(stderr, "Error opening file: %s\n", filename);
    return NULL_BYTES;
  }

  while ((bytesRead = fread(buffer, 1, 1024, file)) > 0)
    buffer_append(&data, bytes(buffer, bytesRead));

  return data.data;
}
