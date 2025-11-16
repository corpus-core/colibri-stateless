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

#include "../prover/prover.h"
#ifdef USE_CURL
#include "../../libs/curl/http.h"
#endif
#include "../util/bytes.h"
#include "../util/crypto.h"
#include "../util/json.h"
#include "../util/ssz.h"
#include "../util/state.h"
#include "../util/version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// : Bindings

// :: CLI
//
// **colibri.stateless** includes a native command-line interface.  
// It can generate proofs and verify them, enabling use in shell scripts, cron jobs, tests, and development workflows.
//
// ## Configuration
//
// Arguments can be passed directly to the prover or verifier.  
// Backend API settings can also be provided through a config file.  
// colibri tools search for configuration in the following order:
//
// 1. use the path set in the `C4_CONFIG` environment variable  
// 2. search the current directory for `c4_config.json`  
// 3. fall back to built-in defaults
//
// This file is a JSON file in the form:
//
// ````json
// {
//   "eth_rpc": ["https://nameless-sly-reel.quiknode.pro/<APIKEY>/", "https://eth-mainnet.g.alchemy.com/v2/<APIKEY>", "https://rpc.ankr.com/eth/<APIKEY>"],
//   "beacon_api": ["https://lodestar-mainnet.chainsafe.io"]
// }
// ````

// ::: colibri-prover
// The colibri-prover command is used to create proofs for a given method and parameters. It works without any backend.
//
// ````sh
//     # Create a proof for the eth_getBlockByNumber method
//     colibri-prover -o block_proof.ssz eth_getBlockByNumber latest false
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

/**
 * Main entry point for the colibri-prover CLI tool.
 * Parses command-line arguments, creates a prover context, and executes the proof generation.
 *
 * @param argc Number of command-line arguments
 * @param argv Array of command-line argument strings
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on error
 */
int main(int argc, char* argv[]) {
  // Check for --version or -v flag
  if (argc >= 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)) {
    c4_print_version(stdout, "colibri-prover");
    exit(EXIT_SUCCESS);
  }

  // Display help if no arguments provided or help flag is used
  if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
    fprintf(stderr, "Usage: %s [options] <method> <params> > proof.ssz\n"
                    "\n"
                    "  -c <chain_id>    : selected chain (default MAINNET = 1)\n"
                    "  -t <testname>    : generates test files in test/data/<testname>\n"
                    "  -x <cachedir>    : caches all requests in the cache directory\n"
                    "  -o <outputfile>  : ssz file with the proof ( default to stdout )\n"
                    "  -d <chain_store> : use chain_data from the chain_store found within the path\n"
                    "  -i               : include code in the proof\n"
                    "  --version, -v    : display version information\n"
                    "\n",
            argv[0]);
    exit(EXIT_FAILURE);
  }

  // Initialize variables for argument parsing
  char*      method     = NULL;  // RPC method name (e.g., "eth_getBlockByNumber")
  buffer_t   buffer     = {0};   // Buffer for building JSON parameter array
  char*      outputfile = NULL;   // Output file path (NULL = stdout)
  uint32_t   flags      = 0;      // Prover flags (e.g., C4_PROVER_FLAG_INCLUDE_CODE)
  chain_id_t chain_id   = C4_CHAIN_MAINNET; // Default to Ethereum mainnet
  buffer_add_chars(&buffer, "["); // Start building JSON array for parameters
  bytes_t client_state = {0};     // Client state data (loaded from chain_store if -d is used)

  // Parse command-line arguments
  // Options start with '-' and can be combined (e.g., "-co" = "-c -o")
  // Non-option arguments are: first = method name, rest = method parameters
  for (int i = 1; i < argc; i++) {
    if (*argv[i] == '-') {
      // Process option flags (can be combined like "-co" or separate like "-c -o")
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
            // Use chain_store: configure file-based chain data source
            // and load client state from a state file if it exists
            curl_set_config(json_parse(bprintf(NULL, "{\"chain_store\":[\"file://%s\"]}", argv[++i])));
            flags |= C4_PROVER_FLAG_CHAIN_STORE;
            // Try to load client state from file (e.g., "./states_1" for chain_id 1)
            char* path   = bprintf(NULL, "./states_%l", (uint64_t) chain_id);
            client_state = bytes_read(path);
            break;
          }
#endif
          case 'i':
            // Include contract code in the proof (for call proofs)
            flags |= C4_PROVER_FLAG_INCLUDE_CODE;
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
    else if (method == NULL) {
      // First non-option argument is the RPC method name
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

  // Create prover context with parsed arguments
  prover_ctx_t* ctx = c4_prover_create(method, (char*) buffer.data.data, chain_id, flags);
  ctx->client_state = client_state; // Set client state if loaded from chain_store

  // Main execution loop: execute prover until completion or error
  // The prover may need to fetch data from remote APIs, so it can return C4_PENDING
  // multiple times before completing.
  while (true) {
    switch (c4_prover_execute(ctx)) {
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
        // Prover needs to fetch data from remote APIs before continuing
#ifdef USE_CURL
        curl_fetch_all(&ctx->state); // Fetch all pending HTTP requests
#else
        fprintf(stderr, "CURL not enabled\n");
        exit(EXIT_FAILURE);
#endif
        break; // Continue loop to retry execution
    }
  }

  c4_prover_free(ctx);
}
