#include "../proofer/proofer.h"
#include "../util/bytes.h"
#include "../util/crypto.h"
#include "../util/json.h"
#include "../util/ssz.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* read_from_stdin() {
  unsigned char  buffer[1024];
  size_t         bytesRead;
  bytes_buffer_t data = {0};

  while ((bytesRead = fread(buffer, 1, 1024, stdin)) > 0)
    buffer_append(&data, bytes(buffer, bytesRead));

  buffer_append(&data, bytes(NULL, 1));

  return (char*) data.data.data;
}

static char* read_from_file(const char* filename) {
  if (strcmp(filename, "-") == 0)
    return read_from_stdin();

  unsigned char  buffer[1024];
  size_t         bytesRead;
  bytes_buffer_t data = {0};

  FILE* file = fopen(filename, "rb");
  if (file == NULL) {
    fprintf(stderr, "Error opening file: %s\n", filename);
    exit(EXIT_FAILURE);
  }

  while ((bytesRead = fread(buffer, 1, 1024, file)) > 0)
    buffer_append(&data, bytes(buffer, bytesRead));

  fclose(file);

  buffer_append(&data, bytes(NULL, 1));
  return (char*) data.data.data;
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: proof <method> <params>\n");
    exit(EXIT_FAILURE);
  }

  char*          method;
  bytes_buffer_t buffer = {0};
  buffer_add_chars(&buffer, "[");

  for (int i = 1; i < argc; i++) {
    if (method == NULL) {
      method = argv[i];
    }
    else {
      if (buffer.data.len > 1) buffer_add_chars(&buffer, ",");
      buffer_add_chars(&buffer, argv[i]);
    }
  }
  buffer_add_chars(&buffer, "]");

  proofer_ctx_t* ctx = c4_proofer_create(method, (char*) buffer.data.data);

  while (true) {
    c4_proofer_execute(ctx);
    if (ctx->error || ctx->proof.data) break;

    data_request_t* data_request = c4_proofer_get_pending_data_request(ctx);
    if (data_request) {
      // handle data request
    }
    else {
      printf("no pending data request but also no error - this is strange. Exiting!\n");
      break;
    }
  }

  if (ctx->error) {
    fprintf(stderr, "Error: %s\n", ctx->error);
    exit(EXIT_FAILURE);
  }
  else if (ctx->proof.data) {
    fwrite(ctx->proof.data, 1, ctx->proof.len, stdout);
    fflush(stdout);
  }

  c4_proofer_free(ctx);
}
