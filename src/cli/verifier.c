/*
 * Copyright (c) 2025 corpus.core
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include "beacon_types.h"
#include "bytes.h"
#include "config.h"
#include "crypto.h"
#include "logger.h"
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

static bytes_t read_from_prover(char* method, char* args, bytes_t state, chain_id_t chain_id) {
  // fprintf(stderr, "reading from prover: %s(%s) from %s\n", method, args, url);
  if (strcmp(method, "colibri_simulateTransaction") == 0) method = "eth_call";
  buffer_t   payload = {0};
  c4_state_t ctx     = {0};

  bprintf(&payload, "{\"method\":\"%s\",\"params\":%s,\"c4\":\"0x%b\"}", method, args, state);
  data_request_t req = {.chain_id = chain_id, .type = C4_DATA_TYPE_PROVER, .payload = payload.data, .encoding = C4_DATA_ENCODING_SSZ, .method = C4_DATA_METHOD_POST};
  ctx.requests       = &req;
  curl_fetch_all(&ctx);
  if (req.error) {
    fprintf(stderr, "Prover returned error: %s\n", req.error);
    exit(EXIT_FAILURE);
  }
  else if (req.response.data == NULL) {
    fprintf(stderr, "prover returned empty response\n");
    exit(EXIT_FAILURE);
  }
  else if (req.response.data[0] == '{') {
    json_t json  = json_parse((char*) req.response.data);
    json_t error = json_get(json, "error");
    if (error.type == JSON_TYPE_STRING)
      fprintf(stderr, "prover returned error: %s\n", json_new_string(error));
    else
      fprintf(stderr, "prover returned unknown error: %s\n", json_new_string(error));
    exit(EXIT_FAILURE);
  }
  return req.response;
}
#endif

// : Bindings

// :: CLI

// ::: colibri-verifier
// The colibri-verifier command is used to verify a proof for a given method and parameters.
// You can pass either a proof file as input or a url to a prover-service. If none are specified the default prover-service will be used.
//
// ````sh
//     # Verify a proof for the eth_getBlockByNumber method
//     colibri-verifier -i block_proof.ssz eth_getBlockByNumber latest false
// ````
//
// ## Options
//
// | Option         | Argument        | Description                | Default |
// |----------------|-----------------|----------------------------|---------|
// | `-c`           | `<chain_id>`    | Chain name or ID           |         |
// | `-l`           | `<log_level>`   | Log level (0=silent, 1=error, 2=info, 3=debug, 4=debug_full)                 |         |
// | `-b`           | `<block_hash>`  | Trusted checkpoint         |         |
// | `-s`           | `<cache_dir>`  | cache-directory   |         |
// | `-t`           | `<test_dir>`    | Test directory (if -DTEST=1)|         |
// | `-i`           | `<proof_file>`  | Proof file to verify       |
// | `-o`           | `<proof_file>`  | Proof file to write        |         |
// | `-p`           | `<prover_url>` | URL of the prover           |         |
// | `-r`           | `<rpc_url>` | URL of the rpc-prover          |         |
// | `-x`           | `<checkpointz_url>` | URL of a checkpointz or beacon-api|         |
// | `-h`           |                 | Display this help message  |         |
// | `<method>`     |                 | Method to verify           |         |
// | `<args>`       |                 | Arguments for the method   |         |
int main(int argc, char* argv[]) {

  // Check for --version
  if (argc >= 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)) {
    c4_print_version(stdout, "colibri-verifier");
    exit(EXIT_SUCCESS);
  }

  if (argc == 1 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
    fprintf(stderr, "Usage: %s <OPTIONS> <method> <args> \n", argv[0]);
    fprintf(stderr, "OPTIONS: \n");
    fprintf(stderr, "  -c <chain_id> \n");
    fprintf(stderr, "  -l <log_level> log level (0=silent, 1=error, 2=info, 3=debug, 4=debug_full)\n");
#ifdef FILE_STORAGE
    fprintf(stderr, "  -s <states_dir> directory to store states\n");
#endif
    fprintf(stderr, "  -b <block_hash> trusted checkpoint\n");
#ifdef TEST
    fprintf(stderr, "  -t <test_dir>  test directory\n");
#endif
    fprintf(stderr, "  -i <proof_file> proof file to read\n");
    fprintf(stderr, "  -s <cache_dir> cache directory\n");
    fprintf(stderr, "  -o <proof_file> proof file to write\n");
    fprintf(stderr, "  -p url of the prover\n");
    fprintf(stderr, "  -r rpc url\n");
    fprintf(stderr, "  -x checkpointz url\n");
    fprintf(stderr, "  --version, -v display version information\n");
    fprintf(stderr, "  -h help\n");
    exit(EXIT_FAILURE);
  }
  char*      method             = NULL;
  chain_id_t chain_id           = C4_CHAIN_MAINNET;
  buffer_t   args               = {0};
  char*      input              = NULL;
  char*      test_dir           = NULL;
  char*      chain_name         = NULL;
  char*      output             = NULL;
  bytes32_t  trusted_checkpoint = {0};
  bool       has_checkpoint     = false;
  char*      rpc_url            = NULL;
  char*      beacon_url         = NULL;
  char*      checkpointz_url    = NULL;
  char*      prover_url         = NULL;
  c4_set_log_level(LOG_ERROR);
  buffer_add_chars(&args, "[");

  for (int i = 1; i < argc; i++) {
    if (*argv[i] == '-') {
      for (char* c = argv[i] + 1; *c; c++) {
        switch (*c) {
          case 'l':
            c4_set_log_level(atoi(argv[++i]));
            break;
#ifdef FILE_STORAGE
          case 's':
            state_data_dir = argv[++i];
            break;
#endif
          case 'c':
            chain_name = argv[++i];
            break;
          case 'i':
          case 'p':
            input = argv[++i];
            if (input && (strncmp(input, "http://", 7) == 0 || strncmp(input, "https://", 8) == 0)) {
              prover_url = input;
              input      = NULL;
            }
            break;
#ifdef USE_CURL
          case 'x':
            checkpointz_url = argv[++i];
            break;
          case 'r':
            rpc_url = argv[++i];
            break;
#endif
          case 'b':
            if (hex_to_bytes(argv[++i], -1, bytes(trusted_checkpoint, 32)) == 32)
              has_checkpoint = true;
            else {
              fprintf(stderr, "invalid blockhash: %s\n", argv[--i]);
              exit(EXIT_FAILURE);
            }
            break;
#ifdef TEST
#ifdef USE_CURL
          case 'o':
            output = argv[++i];
            break;
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

  get_default_config(chain_name, &chain_id, NULL);
  if (prover_url) set_config("prover", prover_url);
  if (rpc_url) set_config("eth_rpc", rpc_url);
  if (beacon_url) set_config("beacon_api", beacon_url);
  if (checkpointz_url) set_config("checkpointz", checkpointz_url);

  if (has_checkpoint)
    c4_eth_set_trusted_checkpoint(chain_id, trusted_checkpoint);
  else if (c4_get_chain_state(chain_id).status == C4_STATE_SYNC_EMPTY) {
    bytes32_t  checkpoint = {0};
    uint64_t   epoch      = 0;
    c4_state_t state      = {0};
#ifdef USE_CURL
    if (!c4_req_checkpointz_status(&state, chain_id, &epoch, checkpoint) && !state.error) {
      curl_fetch_all(&state);
      if (c4_req_checkpointz_status(&state, chain_id, &epoch, checkpoint))
        c4_eth_set_trusted_checkpoint(chain_id, checkpoint);
    }
    c4_state_free(&state);
#endif
    if (!epoch) {
      fprintf(stderr, "failed to get checkpoint from checkpointz : %s\n", state.error);
      exit(EXIT_FAILURE);
    }
  }
  if (!method) {
    fprintf(stderr, "method is required\n");
    exit(EXIT_FAILURE);
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
      if (!input) {
#ifdef USE_CURL
        char name[100];
        sprintf(name, "states_%d", (uint32_t) chain_id);
        buffer_t         state = {0};
        storage_plugin_t storage;
        c4_get_storage_config(&storage);
        storage.get(name, &state);
        request = read_from_prover(method, (char*) args.data.data, state.data, chain_id);
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