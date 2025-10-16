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

// : Bindings

// :: CLI
// Colibri comes with a native comandline interface. The can be used to create proofs and verify them, which allows easy use within shellscripts, cronjobs or while testing or developing.
//
// ## Configuration
//
// while you can pass arguments to the the proofer or verifier, when it comes to configuring the backend apis, you can use create a config-file. colibri tools will try to find it in the following order:
//
// 1. look for a file with the path specified in the `C4_CONFIG` environment variable
// 2. look in the current directory for a file named `c4_config.json`
// 3. use defaults.
//
// this file is a json-file in the form:
//
// ````json
// {
//   "eth_rpc": ["https://nameless-sly-reel.quiknode.pro/<APIKEY>/", "https://eth-mainnet.g.alchemy.com/v2/<APIKEY>", "https://rpc.ankr.com/eth/<APIKEY>"],
//   "beacon_api": ["https://lodestar-mainnet.chainsafe.io"]
// }
// ````

// ::: proof
// The proof command is used to create proofs for a given method and parameters. It works without any backend.
//
// ````sh
//     # Create a proof for the eth_getBlockByNumber method
//     proof -o block_proof.ssz eth_getBlockByNumber latest false
// ````
//
// ## Options
//
// | Option         | Argument        | Description                                                                 | Default      |
// |----------------|-----------------|-----------------------------------------------------------------------------|--------------|
// | `-c`           | `<chain_id>`    | Selected chain                                                              | `MAINNET` (1)|
// | `-t`           | `<testname>`    | Generates test files in `test/data/<testname>`                                |              |
// | `-x`           | `<cachedir>`    | Caches all requests in the cache directory                                  |              |
// | `-o`           | `<outputfile>`  | SSZ file with the proof                                                     | `stdout`     |
// | `-d`           | `<chain_store>` | Use `chain_data` from the `chain_store` found within the path               |              |
// | `-i`           |                 | Include code in the proof                                                   |              |
// | `<method>`     |                 | The method to execute                                                       |              |
// | `<params>`     |                 | Parameters for the method                                                   |              |

int main(int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s [options] <method> <params> > proof.ssz\n"
                    "\n"
                    "  -c <chain_id>    : selected chain (default MAINNET = 1)\n"
                    "  -t <testname>    : generates test files in test/data/<testname>\n"
                    "  -x <cachedir>    : caches all reguests in the cache directory\n"
                    "  -o <outputfile>  : ssz file with the proof ( default to stdout )\n"
                    "  -d <chain_store> : use chain_data from the chain_store found within the path\n"
                    "  -i               : include code in the proof\n"
                    "\n",
            argv[0]);
    exit(EXIT_FAILURE);
  }

  char*      method     = NULL;
  buffer_t   buffer     = {0};
  char*      outputfile = NULL;
  uint32_t   flags      = 0;
  chain_id_t chain_id   = C4_CHAIN_MAINNET;
  buffer_add_chars(&buffer, "[");
  bytes_t client_state = {0};

  for (int i = 1; i < argc; i++) {
    if (*argv[i] == '-') {
      for (char* c = argv[i] + 1; *c; c++) {
        switch (*c) {
          case 'c':
            chain_id = atoi(argv[++i]);
            break;
          case 'o':
            outputfile = argv[++i];
            break;
#ifdef USE_CURL
          case 'd': {
            curl_set_config(json_parse(bprintf(NULL, "{\"chain_store\":[\"file://%s\"]}", argv[++i])));
            flags |= C4_PROOFER_FLAG_CHAIN_STORE;
            char* path   = bprintf(NULL, "./states_%l", (uint64_t) chain_id);
            client_state = bytes_read(path);
            break;
          }
#endif
          case 'i':
            flags |= C4_PROOFER_FLAG_INCLUDE_CODE;
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

  proofer_ctx_t* ctx = c4_proofer_create(method, (char*) buffer.data.data, chain_id, flags);
  ctx->client_state  = client_state;
  while (true) {
    switch (c4_proofer_execute(ctx)) {
      case C4_SUCCESS:
        if (outputfile)
          bytes_write(ctx->proof, fopen(outputfile, "wb"), true);
        else
          fwrite(ctx->proof.data, 1, ctx->proof.len, stdout);
        fflush(stdout);
        exit(EXIT_SUCCESS);

      case C4_ERROR:
        fprintf(stderr, "Failed: %s\n", ctx->state.error);
        exit(EXIT_FAILURE);

      case C4_PENDING:
#ifdef USE_CURL
        curl_fetch_all(&ctx->state);
#else
        fprintf(stderr, "CURL not enabled\n");
        exit(EXIT_FAILURE);
#endif
        break;
    }
  }

  c4_proofer_free(ctx);
}
