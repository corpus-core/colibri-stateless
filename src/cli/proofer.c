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
  buffer_t buf = {0};
  REQ_TEST_DIR = bprintf(&buf, "%s/%s", TESTDATA_DIR, dir);
  if (MKDIR(REQ_TEST_DIR) != 0) perror("Error creating directory");
}

static void test_write_file(const char* filename, bytes_t data) {
  if (!REQ_TEST_DIR) return;
  buffer_t buf = {0};
  bytes_write(data, fopen(bprintf(&buf, "%s/%s", REQ_TEST_DIR, filename), "w"), true);
  buffer_free(&buf);
}

#endif

int main(int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: proof <method> <params>\n");
    exit(EXIT_FAILURE);
  }

  char*    method = NULL;
  buffer_t buffer = {0};
  buffer_add_chars(&buffer, "[");

  for (int i = 1; i < argc; i++) {
#ifdef TEST
    if (strcmp(argv[i], "-t") == 0) {
      set_req_test_dir(argv[++i]);
      continue;
    }
#endif

    if (method == NULL) {
      method = argv[i];
    }
    else {
      if (buffer.data.len > 1) buffer_add_chars(&buffer, ",");
      if (argv[i][0] == '{' || argv[i][0] == '[' || strcmp(argv[i], "true") == 0 || strcmp(argv[i], "false") == 0)
        buffer_add_chars(&buffer, argv[i]);
      else
        bprintf(&buffer, "\"%s\"", argv[i]);
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
