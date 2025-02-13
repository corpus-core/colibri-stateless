#include "../proofer/proofer.h"
#ifdef USE_CURL
#include "../../libs/curl/http.h"
#endif
#include "../util/bytes.h"
#include "../util/crypto.h"
#include "../util/json.h"
#include "../util/ssz.h"
#include "../util/state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: proof <method> <params>\n");
    exit(EXIT_FAILURE);
  }

  char*      method   = NULL;
  buffer_t   buffer   = {0};
  chain_id_t chain_id = C4_CHAIN_MAINNET;
  buffer_add_chars(&buffer, "[");

  for (int i = 1; i < argc; i++) {
    if (*argv[i] == '-') {
      for (char* c = argv[i] + 1; *c; c++) {
        switch (*c) {
          case 'c':
            chain_id = atoi(argv[++i]);
            break;
#ifdef TEST
#ifdef USE_CURL
          case 't':
            curl_set_test_dir(argv[++i]);
            break;
          case 'x':
            curl_set_cache_dir(argv[++i]);
            break;
#endif
#endif
          default:
            fprintf(stderr, "Unknown option: %c\n", *c);
            exit(EXIT_FAILURE);
        }
      }
    }
    else if (method == NULL)
      method = argv[i];
    else {
      if (buffer.data.len > 1) buffer_add_chars(&buffer, ",");
      if (argv[i][0] == '{' || argv[i][0] == '[' || strcmp(argv[i], "true") == 0 || strcmp(argv[i], "false") == 0)
        buffer_add_chars(&buffer, argv[i]);
      else
        bprintf(&buffer, "\"%s\"", argv[i]);
    }
  }
  buffer_add_chars(&buffer, "]");

  proofer_ctx_t*  ctx = c4_proofer_create(method, (char*) buffer.data.data, chain_id);
  data_request_t* req;
  while (true) {
    switch (c4_proofer_execute(ctx)) {
      case C4_SUCCESS:
        fwrite(ctx->proof.data, 1, ctx->proof.len, stdout);
        fflush(stdout);
        exit(EXIT_SUCCESS);

      case C4_ERROR:
        fprintf(stderr, "Error: %s\n", ctx->state.error);
        exit(EXIT_FAILURE);

      case C4_PENDING:
        while ((req = c4_state_get_pending_request(&ctx->state))) {
#ifdef USE_CURL
          curl_fetch(req);
#else
          fprintf(stderr, "CURL not enabled\n");
          exit(EXIT_FAILURE);
#endif
        }
        break;
    }
  }

  c4_proofer_free(ctx);
}
