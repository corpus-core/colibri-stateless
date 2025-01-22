#include "../util/ssz.h"
#include "../util/bytes.h"
#include "../util/crypto.h"
#include "../verifier/types_verify.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Function to write bytes to a file
void write_bytes_to_file(const char* filename, const unsigned char* data, size_t length) {
  FILE* file = fopen(filename, "wb");
  if (file == NULL) {
    fprintf(stderr, "Error opening file for writing: %s\n", filename);
    exit(EXIT_FAILURE);
  }

  size_t written = fwrite(data, sizeof(unsigned char), length, file);
  if (written != length) {
    fprintf(stderr, "Error writing data to file: %s\n", filename);
    fclose(file);
    exit(EXIT_FAILURE);
  }

  fclose(file);
}

static bytes_t read_from_file(const char* filename) {
  unsigned char  buffer[1024];
  size_t         bytesRead;
  bytes_buffer_t data = {0};

  FILE* file = strcmp(filename, "-") ? fopen(filename, "rb") : stdin;
  if (file == NULL) {
    fprintf(stderr, "Error opening file: %s\n", filename);
    exit(EXIT_FAILURE);
  }

  while ((bytesRead = fread(buffer, 1, 1024, file)) > 0)
    buffer_append(&data, bytes(buffer, bytesRead));

  if (file != stdin)
    fclose(file);
  return data.data;
}

int main(int argc, char* argv[]) {
  if (argc == 1) {
    fprintf(stderr, "Usage: %s <file.ssz> <field1> <field2> ...\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  ssz_ob_t res          = ssz_ob(C4_REQUEST_CONTAINER, read_from_file(argv[1]));
  char*    out_filename = NULL;

  for (int i = 2; i < argc; i++) {
    if (argv[i][0] == '-') {
      if (strcmp(argv[i], "-o") == 0) {
        out_filename = argv[i + 1];
        i++;
      }
    }
    else if (res.def && res.def->type != SSZ_TYPE_CONTAINER) {
      char* endptr;
      long  value = strtol(argv[i], &endptr, 10);
      if (endptr && *endptr == '\0')
        res = ssz_at(res, value);
      else {
        fprintf(stderr, "Invalid value for index : %s!\n", argv[i]);
        exit(EXIT_FAILURE);
      }
    }
    else
      res = ssz_get(&res, argv[i]);
  }

  if (out_filename) {
    write_bytes_to_file(out_filename, res.bytes.data, res.bytes.len);
  }

  if (ssz_is_error(res)) {
    fprintf(stderr, "No value found!\n");
    exit(EXIT_FAILURE);
  }

  ssz_dump(stdout, res, true, 0);
  bytes32_t hashroot;
  ssz_hash_tree_root(res, hashroot);
  print_hex(stdout, bytes(hashroot, 32), "\ntree_hash_root: 0x", "\n");
}