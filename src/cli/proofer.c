#include "../proofer/proofer.h"
#ifdef USE_CURL
#include "../../libs/curl/http.h"
#endif
#include "../util/bytes.h"
#include "../util/crypto.h"
#include "../util/json.h"
#include "../util/request.h"
#include "../util/ssz.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef TEST
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#include <unistd.h>
#define MKDIR(path) mkdir(path, 0755)
#endif

static char* REQ_TEST_DIR = NULL;

static void set_req_test_dir(const char* dir) {
  char* path = malloc(strlen(dir) + strlen(TESTDATA_DIR) + 5);
  sprintf(path, "%s/%s", TESTDATA_DIR, dir);
  REQ_TEST_DIR = path;

  if (MKDIR(path) != 0) {
    perror("Error creating directory");
  }
}

static void test_write_file(const char* filename, bytes_t data) {
  if (!REQ_TEST_DIR) return;
  char* path = malloc(strlen(REQ_TEST_DIR) + strlen(filename) + 5);
  sprintf(path, "%s/%s", REQ_TEST_DIR, filename);
  bytes_write(data, fopen(path, "w"), true);
  free(path);
}

#endif

static char* read_from_stdin() {
  unsigned char buffer[1024];
  size_t        bytesRead;
  buffer_t      data = {0};

  while ((bytesRead = fread(buffer, 1, 1024, stdin)) > 0)
    buffer_append(&data, bytes(buffer, bytesRead));

  buffer_append(&data, bytes(NULL, 1));

  return (char*) data.data.data;
}

static char* read_from_file(const char* filename) {
  if (strcmp(filename, "-") == 0)
    return read_from_stdin();

  unsigned char buffer[1024];
  size_t        bytesRead;
  buffer_t      data = {0};

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

  char*    method = NULL;
  buffer_t buffer = {0};
  buffer_add_chars(&buffer, "[");

  for (int i = 1; i < argc; i++) {
    if (method == NULL) {
      method = argv[i];
    }
#ifdef TEST
    else if (strcmp(argv[i], "-t") == 0) {
      set_req_test_dir(argv[++i]);
    }
#endif
    else {
      if (buffer.data.len > 1) buffer_add_chars(&buffer, ",");
      buffer_add_chars(&buffer, "\"");
      buffer_add_chars(&buffer, argv[i]);
      buffer_add_chars(&buffer, "\"");
    }
  }
  buffer_add_chars(&buffer, "]");

  proofer_ctx_t*  ctx = c4_proofer_create(method, (char*) buffer.data.data);
  data_request_t* req;
  while (true) {
    switch (c4_proofer_execute(ctx)) {
      case C4_PROOFER_WAITING:
        while ((req = c4_proofer_get_pending_data_request(ctx))) {
#ifdef USE_CURL
          curl_fetch(req);
#ifdef TEST
          if (req->response.data && REQ_TEST_DIR) {
            char test_filename[1024];
            sprintf(test_filename, "%llx.%s", *((unsigned long long*) req->id), req->type == C4_DATA_TYPE_BEACON_API ? "ssz" : "json");
            test_write_file(test_filename, req->response);
          }
#endif
#else
          fprintf(stderr, "CURL not enabled\n");
          exit(EXIT_FAILURE);
#endif
        }
        break;

      case C4_PROOFER_ERROR:
        fprintf(stderr, "Error: %s\n", ctx->error);
        exit(EXIT_FAILURE);

      case C4_PROOFER_SUCCESS:
        fwrite(ctx->proof.data, 1, ctx->proof.len, stdout);
        fflush(stdout);
        exit(EXIT_SUCCESS);

      case C4_PROOFER_PENDING:
        break;
    }
  }

  c4_proofer_free(ctx);
}
