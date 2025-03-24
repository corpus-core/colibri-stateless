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
#include <curl/curl.h>
#endif

#ifdef USE_CURL
static size_t write_data(void* ptr, size_t size, size_t nmemb, void* userdata) {
  buffer_t* response_buffer = (buffer_t*) userdata;
  buffer_append(response_buffer, bytes(ptr, size * nmemb));
  return size * nmemb;
}
static bytes_t read_from_proofer(char* url, char* method, char* args, chain_id_t chain_id) {
  buffer_t payload         = {0};
  buffer_t response_buffer = {0};
  bprintf(&payload, "{\"method\":\"%s\",\"params\":%s}", method, args);
  CURL* curl = curl_easy_init();
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.data.data);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payload.data.len);
  struct curl_slist* headers = NULL;
  headers                    = curl_slist_append(headers, "Content-Type: application/json");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_buffer);
  curl_easy_perform(curl);
  curl_easy_cleanup(curl);

  if (response_buffer.data.len == 0) {
    fprintf(stderr, "proofer returned empty response\n");
    exit(EXIT_FAILURE);
  }
  if (response_buffer.data.data[0] == '{') {
    json_t json  = json_parse((char*) response_buffer.data.data);
    json_t error = json_get(json, "error");
    if (error.type == JSON_TYPE_STRING) {
      fprintf(stderr, "proofer returned error: %s\n", json_new_string(error));
    }
    exit(EXIT_FAILURE);
  }
  return response_buffer.data;
}
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
  bytes_t request = {0};
  if (strncmp(input, "http://", 7) == 0 || strncmp(input, "https://", 8) == 0) {
#ifdef USE_CURL
    request = read_from_proofer(input, method, (char*) args.data.data, chain_id);
#else
    fprintf(stderr, "require data, but no curl installed");
    exit(EXIT_FAILURE);
#endif
  }
  else {
    request = bytes_read(input);
  }

  bytes_read(input);

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