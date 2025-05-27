#include "beacon_types.h"
#include "bytes.h"
#include "crypto.h"
#include "plugin.h"
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
static bytes_t read_from_proofer(char* url, char* method, char* args, bytes_t state, chain_id_t chain_id) {
  //  printf("reading from proofer: %s(%s) from %s\n", method, args, url);
  buffer_t payload         = {0};
  buffer_t response_buffer = {0};
  bprintf(&payload, "{\"method\":\"%s\",\"params\":%s,\"c4\":\"0x%b\"}", method, args, state);
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

// : Bindings

// :: CLI

// ::: verify
// The verify command is used to verify a proof for a given method and parameters.
// You can pass either a proof file as input or a url to a proofer-service. If non are specified the default proofer-service will be used.
//
// ````sh
//     # Verify a proof for the eth_getBlockByNumber method
//     verify -i block_proof.ssz eth_getBlockByNumber latest false
// ````
//
// ## Options
//
// | Option         | Argument        | Description                | Default |
// |----------------|-----------------|----------------------------|---------|
// | `-c`           | `<chain_id>`    | Chain ID                   |         |
// | `-b`           | `<block_hash>`  | Trusted blockhash          |         |
// | `-t`           | `<test_dir>`    | Test directory             |         |
// | `-i`           | `<proof_file>`  | Proof file to verify       |         |
// | `-o`           | `<proof_file>`  | Proof file to write        |         |
// | `-p`           | `<proofer_url>` | URL of the proofer         |         |
// | `-h`           |                 | Display this help message  |         |
// | `<method>`     |                 | Method to verify           |         |
// | `<args>`       |                 | Arguments for the method   |         |
int main(int argc, char* argv[]) {
  if (argc == 1 || strcmp(argv[1], "-h") == 0) {
    fprintf(stderr, "Usage: %s <OPTIONS> <method> <args> \n", argv[0]);
    fprintf(stderr, "OPTIONS: \n");
    fprintf(stderr, "  -c <chain_id> \n");
    fprintf(stderr, "  -b <block_hash> trusted blockhash\n");
    fprintf(stderr, "  -t <test_dir>  test directory\n");
    fprintf(stderr, "  -i <proof_file> proof file to read\n");
    fprintf(stderr, "  -o <proof_file> proof file to write\n");
    fprintf(stderr, "  -p url of the proofer\n");
    fprintf(stderr, "  -h help\n");
    exit(EXIT_FAILURE);
  }

  char*      method         = NULL;
  chain_id_t chain_id       = C4_CHAIN_MAINNET;
  buffer_t   args           = {0};
  char*      input          = NULL;
  char*      test_dir       = NULL;
  char*      output         = NULL;
  buffer_t   trusted_blocks = {0};
  buffer_add_chars(&args, "[");

  for (int i = 1; i < argc; i++) {
    if (*argv[i] == '-') {
      for (char* c = argv[i] + 1; *c; c++) {
        switch (*c) {
          case 'c':
            chain_id = atoi(argv[++i]);
            break;
          case 'o':
            output = argv[++i];
            break;
          case 'i':
          case 'p':
            input = argv[++i];
            break;
          case 'b':
            buffer_grow(&trusted_blocks, trusted_blocks.data.len + 32);
            if (hex_to_bytes(argv[++i], -1, bytes(trusted_blocks.data.data + trusted_blocks.data.len, 32)) == 32)
              trusted_blocks.data.len += 32;
            else {
              fprintf(stderr, "invalid blockhash: %s\n", argv[--i]);
              exit(EXIT_FAILURE);
            }
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
  if (input == NULL) {
    input = getenv("C4_PROOFER");
    if (input == NULL)
      input = "https://mainnet.colibri-proof.tech";
  }
  if (trusted_blocks.data.len > 0)
    c4_eth_set_trusted_blockhashes(chain_id, trusted_blocks.data);
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
        char name[100];
        sprintf(name, "states_%d", (uint32_t) chain_id);
        buffer_t         state = {0};
        storage_plugin_t storage;
        c4_get_storage_config(&storage);
        storage.get(name, &state);
        request = read_from_proofer(input, method, (char*) args.data.data, state.data, chain_id);
        curl_set_config(json_parse(bprintf(NULL, "{\"beacon_api\":[\"%s\"],\"eth_rpc\":[]}", input)));
        buffer_free(&state);
        if (output) bytes_write(request, fopen(output, "w"), true);
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