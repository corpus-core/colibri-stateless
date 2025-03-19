#include "beacon_types.h"
#include "bytes.h"
#include "crypto.h"
#include "ssz.h"
#include "state.h"
#include "sync_committee.h"
#include "verify.h"
#include "version.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef USE_CURL
#include "../../libs/curl/http.h"
#endif

int main(int argc, char* argv[]) {
  if (argc == 1) {
    fprintf(stderr, "Usage: %s request.ssz \n", argv[0]);
    exit(EXIT_FAILURE);
  }

  char*      method         = NULL;
  chain_id_t chain_id       = C4_CHAIN_MAINNET;
  buffer_t   args           = {0};
  char*      input          = NULL;
  char*      test_dir       = NULL;
  buffer_t   trusted_blocks = {0};
  buffer_add_chars(&args, "[");
  buffer_add_chars(&trusted_blocks, "[");

  for (int i = 1; i < argc; i++) {
    if (*argv[i] == '-') {
      for (char* c = argv[i] + 1; *c; c++) {
        switch (*c) {
          case 'c':
            chain_id = atoi(argv[++i]);
            break;
          case 'b':
            if (trusted_blocks.data.len > 1) buffer_add_chars(&trusted_blocks, ",");
            bprintf(&trusted_blocks, "\"%s\"", argv[++i]);
            break;
#ifdef TEST
#ifdef USE_CURL
          case 't':
            test_dir = curl_set_test_dir(argv[++i]);
            break;
#endif
#endif
          default:
            fprintf(stderr, "Unknown option: %c\n", *c);
            exit(EXIT_FAILURE);
        }
      }
      if (input == NULL && strlen(argv[i]) == 1)
        input = argv[i];
    }
    else if (input == NULL)
      input = argv[i];
    else if (method == NULL)
      method = argv[i];
    else {
      if (args.data.len > 1) buffer_add_chars(&args, ",");
      if (*argv[i] == '{' || *argv[i] == '[' || strcmp(argv[i], "true") == 0 || strcmp(argv[i], "false") == 0)
        bprintf(&args, "%s", argv[i]);
      else
        bprintf(&args, "\"%s\"", argv[i]);
    }
  }
  buffer_add_chars(&args, "]");
  buffer_add_chars(&trusted_blocks, "]");
  if (input == NULL) {
    fprintf(stderr, "No input file provided\n");
    exit(EXIT_FAILURE);
  }
  bytes_t request = bytes_read(input);

  verify_ctx_t ctx = {0};
  for (
      c4_status_t status = c4_verify_from_bytes(&ctx, request, method, method ? json_parse((char*) args.data.data) : (json_t) {0}, chain_id);
      status == C4_PENDING;
      status = c4_verify(&ctx))
#ifdef USE_CURL
    curl_fetch_all(&ctx.state);
#else
  {
    fprintf(stderr, "require data, but no curl installed");
    exit(EXIT_FAILURE);
  }
#endif
  if (ctx.success) {
    if (test_dir) {
      char* filename = bprintf(NULL, "%s/test.json", test_dir);
      char* content  = bprintf(NULL, "{\n  \"method\":\"%s\",\n  \"params\":%J,\n  \"chain_id\": %l,\n  \"expected_result\": %Z\n}",
                               method, args, chain_id, ctx.data);
      bytes_write(bytes(content, strlen(content)), fopen(filename, "w"), true);
      free(filename);
      free(content);
    }
    ssz_dump_to_file(stdout, ctx.data, false, true);
    fflush(stdout);
    return EXIT_SUCCESS;
  }
  else if (ctx.state.error) {
    fprintf(stderr, "proof is invalid: %s\n", ctx.state.error);
    return EXIT_FAILURE;
  }
  else
    return EXIT_FAILURE;
}