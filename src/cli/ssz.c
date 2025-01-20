#include "../util/ssz.h"
#include "../util/bytes.h"
#include "../util/crypto.h"
#include "../verifier/beacon_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  ssz_ob_t res = ssz_ob(C4_PROOFS_CONTAINER, read_from_file(argv[1]));

  if (res.def->type == SSZ_TYPE_UNION) res = ssz_union(res);

  for (int i = 2; i < argc; i++)
    res = ssz_get(&res, argv[i]);

  if (ssz_is_error(res)) {
    fprintf(stderr, "No value found!\n");
    exit(EXIT_FAILURE);
  }

  ssz_dump(stdout, res, true, 0);
  bytes32_t hashroot;
  ssz_hash_tree_root(res, hashroot);
  print_hex(stdout, bytes(hashroot, 32), "\ntree_hash_root: 0x", "\n");
}