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
  if (argc == 1 || strcmp(argv[1], "-h") == 0) {
    fprintf(stderr, "Usage: %s <OPTIONS> <method> <args> \n", argv[0]);
    fprintf(stderr, "OPTIONS: \n");
    fprintf(stderr, "  -c <chain_id> \n");
    fprintf(stderr, "  -b <block_hash> trusted blockhash\n");
    fprintf(stderr, "  -t <test_dir>  test directory\n");
    fprintf(stderr, "  -i <proof_file> proof file\n");
    fprintf(stderr, "  -p url of the proofer\n");
    fprintf(stderr, "  -h help\n");
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
          case 'i':
          case 'p':
            input = argv[++i];
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
    }
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
    input = getenv("C4_PROOFER");
    if (input == NULL)
      input = "https://c4.incubed.net";
  }
  bytes_t       request     = {0};
  method_type_t method_type = c4_get_method_type(chain_id, method);
  switch (method_type) {
    case METHOD_UNDEFINED:
      fprintf(stderr, "method not known: %s\n", method);
      exit(EXIT_FAILURE);
    case METHOD_NOT_SUPPORTED:
      fprintf(stderr, "method not supported: %s\n", method);
      exit(EXIT_FAILURE);
    case METHOD_PROOFABLE:
      if (strncmp(input, "http://", 7) == 0 || strncmp(input, "https://", 8) == 0) {
#ifdef USE_CURL
        request = read_from_proofer(input, method, (char*) args.data.data, chain_id);
#else
        fprintf(stderr, "require data, but no curl installed");
        exit(EXIT_FAILURE);
#endif
      }
      else
        request = bytes_read(input);

      break;
    case METHOD_LOCAL:
      request = NULL_BYTES;
      break;
    case METHOD_UNPROOFABLE:
      fprintf(stderr, "method not proofable: %s\n", method);
      exit(EXIT_FAILURE);
      break;
  }

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
                               ctx.method, ctx.args, chain_id, ctx.data);
      bytes_write(bytes(content, strlen(content)), fopen(filename, "w"), true);
      safe_free(filename);
      safe_free(content);
    }
    ssz_dump_to_file_no_quotes(stdout, ctx.data);
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