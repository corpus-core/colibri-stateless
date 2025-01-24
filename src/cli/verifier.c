#include "../util/bytes.h"
#include "../util/crypto.h"
#include "../util/ssz.h"
#include "../verifier/types_beacon.h"
#include "../verifier/types_verify.h"
#include "../verifier/verify.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bytes_t read_from_file(const char* filename) {
  unsigned char buffer[1024];
  size_t        bytesRead;
  buffer_t      data = {0};

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

void error(const char* msg) {
  fprintf(stderr, "%s\n", msg);
  exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
  if (argc == 1) {
    fprintf(stderr, "Usage: %s request.ssz \n", argv[0]);
    exit(EXIT_FAILURE);
  }

  verify_ctx_t ctx = {0};
  c4_verify_from_bytes(&ctx, read_from_file(argv[1]));

  ssz_ob_t res = ssz_ob(C4_REQUEST_CONTAINER, read_from_file(argv[1]));

  if (ctx.success) {
    printf("proof is valid\n");
    return EXIT_SUCCESS;
  }
  else {
    printf("proof is invalid: %s\n", ctx.error);

    if (ctx.first_missing_period) printf("first missing period: %" PRIu64 "\n", ctx.first_missing_period);
    if (ctx.last_missing_period) printf("last missing period: %" PRIu64 "\n", ctx.last_missing_period);
    return EXIT_FAILURE;
  }
}