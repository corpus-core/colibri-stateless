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

#include "../../bindings/colibri_common.h"
#include "beacon_types.h"
#include "bytes.h"
#include "config.h"
#include "crypto.h"
#include "logger.h"
#include "plugin.h"
#include "ssz.h"
#include "state.h"
#include "sync_committee.h"
#include "version.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef USE_CURL
#include "../../libs/curl/http.h"
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
// | `-m`           | `<mode>`        | Prover mode: `local`, `remote`, `hybrid` |         |
// | `-P`           |                 | Enable PAP (Pragmatic Adaptive Privacy) mode       |         |
// | `-h`           |                 | Display this help message  |         |
// | `<method>`     |                 | Method to verify           |         |
// | `<args>`       |                 | Arguments for the method   |         |
int main(int argc, char* argv[]) {

  // Check for --version
  if (argc >= 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)) {
    c4_print_version(stdout, "colibri-verifier");
    exit(EXIT_SUCCESS);
  }

#ifdef FILE_STORAGE
  /* Prefer file storage so sync committee state is persisted across runs. */
  storage_plugin_t file_plugin = {0};
  c4_get_file_storage_plugin(&file_plugin);
  c4_set_storage_config(&file_plugin);
#endif

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
#ifdef ETH_ZKPROOF
    fprintf(stderr, "  -z use zk_proof\n");
#endif
    fprintf(stderr, "  -i <proof_file> proof file to read\n");
    fprintf(stderr, "  -s <cache_dir> cache directory\n");
    fprintf(stderr, "  -o <proof_file> proof file to write\n");
    fprintf(stderr, "  -p url of the prover\n");
    fprintf(stderr, "  -m <mode> prover mode: local, remote, hybrid\n");
    fprintf(stderr, "  -L local proof (shorthand for -m local)\n");
    fprintf(stderr, "  -r rpc url\n");
    fprintf(stderr, "  -x checkpointz url\n");
    fprintf(stderr, "  -n <SIGNERS> if set, the verifier uses checkpoints signed by the given signers (multiple addresses are concatinated bytes with 20 bytes each)\n");
    fprintf(stderr, "  -P enable PAP (Pragmatic Adaptive Privacy) mode\n");
    fprintf(stderr, "  -O no verifier, just return the proof\n");
    fprintf(stderr, "  --version, -v display version information\n");
    fprintf(stderr, "  -h help\n");
    exit(EXIT_FAILURE);
  }
#ifdef USE_CURL
  char     tmp[1000] = {0};
  buffer_t buf       = stack_buffer(tmp);
#endif
  char*          method             = NULL;
  chain_id_t     chain_id           = C4_CHAIN_MAINNET;
  buffer_t       args               = {0};
  char*          input              = NULL;
  char*          test_dir           = NULL;
  char*          chain_name         = NULL;
  char*          output             = NULL;
  char*          signers            = NULL;
  bytes32_t      trusted_checkpoint = {0};
  bool           has_checkpoint     = false;
  bool           use_zk_proof       = false;
  verify_flags_t verify_flags       = 0;
  char*          rpc_url            = NULL;
  char*          beacon_url         = NULL;
  char*          checkpointz_url    = NULL;
  char*          prover_url         = NULL;
  char*          trace_id           = NULL;
  c4_prover_mode_t prover_mode      = C4_PROVER_MODE_REMOTE;
  bool             prover_mode_set  = false;
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
            input = argv[++i];
            if (input && (strncmp(input, "http://", 7) == 0 || strncmp(input, "https://", 8) == 0)) {
              prover_url = input;
              input      = NULL;
            }
            break;
          case 'L':
            prover_mode     = C4_PROVER_MODE_LOCAL;
            prover_mode_set = true;
            break;
          case 'm': {
            char* mode = argv[++i];
            if (strcmp(mode, "local") == 0) {
              prover_mode = C4_PROVER_MODE_LOCAL;
            }
            else if (strcmp(mode, "remote") == 0) {
              prover_mode = C4_PROVER_MODE_REMOTE;
            }
            else if (strcmp(mode, "hybrid") == 0) {
              prover_mode = C4_PROVER_MODE_HYBRID;
            }
            else {
              fprintf(stderr, "invalid prover mode: %s (expected: local, remote, hybrid)\n", mode);
              exit(EXIT_FAILURE);
            }
            prover_mode_set = true;
            break;
          }
        case 'p':
            prover_url = argv[++i];
            break;
#ifdef USE_CURL
          case 'n':
            signers = argv[++i];
            break;
          case 'x':
            checkpointz_url = argv[++i];
            break;
          case 'r':
            rpc_url = argv[++i];
            break;
          case 'T':
            curl_set_config(json_parse(bprintf(&buf, "{\"trace_config\":{\"level\":\"%s\"}}", argv[++i])));
            break;
#endif
#ifdef ETH_ZKPROOF
          case 'z':
            use_zk_proof = true;
            break;
#endif
          case 'P':
            verify_flags |= VERIFY_FLAG_PAP;
            break;
          case 'O':
            verify_flags |= VERIFY_FLAG_PROOF_ONLY;
            break;
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
  json_t default_config = json_parse(get_default_config(chain_name, &chain_id, NULL));

  if (prover_url)
    set_config("prover", prover_url);
  else {
    json_t provers = json_get(default_config, "prover");
    if (json_len(provers) > 0)
      prover_url = (char*)json_at(provers, 0).start;
  }
  if (prover_mode_set && prover_mode == C4_PROVER_MODE_LOCAL) prover_url = NULL;
  if (rpc_url) set_config("eth_rpc", rpc_url);
  if (beacon_url) set_config("beacon_api", beacon_url);
  if (checkpointz_url) set_config("checkpointz", checkpointz_url);

  if (has_checkpoint)
    c4_eth_set_trusted_checkpoint(chain_id, trusted_checkpoint);
  else if (c4_get_chain_state(chain_id).status == C4_STATE_SYNC_EMPTY && !use_zk_proof) {
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

  prover_flags_t prover_flags = C4_PROVER_FLAG_USE_ACCESSLIST;
  if (use_zk_proof) prover_flags |= C4_PROVER_FLAG_ZK_PROOF;
  if (!prover_mode_set) {
    if (input != NULL)
      prover_mode = C4_PROVER_MODE_LOCAL;
    else if (prover_url != NULL)
      prover_mode = C4_PROVER_MODE_REMOTE;
    else
      prover_mode = C4_PROVER_MODE_LOCAL;
  }

  c4_rpc_ctx_t* ctx = c4_rpc_ctx_create(method, (char*) args.data.data, chain_id,
                                        prover_flags, verify_flags, prover_mode);

  if (input) {
    ctx->proof       = bytes_read(input);
    ctx->proof_owned = true;
  }

  if (signers) {
    c4_rpc_ctx_set_witness_keys(ctx, signers);
    if (!ctx->witness_keys.data || ctx->witness_keys.len % 20 != 0) {
      fprintf(stderr, "invalid signers: %s\n", signers);
      c4_rpc_ctx_free(ctx);
      exit(EXIT_FAILURE);
    }
  }

  c4_status_t status;
  while ((status = c4_rpc_execute(ctx)) == C4_PENDING) {
#ifdef USE_CURL
    c4_state_t* state = c4_rpc_get_state(ctx);
    if (state) curl_fetch_all(state);
#else
    fprintf(stderr, "require data, but no curl installed");
    c4_rpc_ctx_free(ctx);
    exit(EXIT_FAILURE);
#endif
  }

  if (output && ctx->proof.data)
    bytes_write(ctx->proof, fopen(output, "w"), true);

  if (status == C4_SUCCESS) {
    if (test_dir) {
      char* filename = bprintf(NULL, "%s/test.json", test_dir);
      char* content  = bprintf(NULL, "{\n  \"method\":\"%s\",\n  \"params\":%J,\n  \"chain_id\": %l,\n  \"pap\": %s,\n  \"prover_mode\": \"%s\",\n  \"expected_result\": %Z\n}",
                               ctx->verifier.method, ctx->verifier.args, chain_id,
                               verify_flags & VERIFY_FLAG_PAP ? "true" : "false",
                               prover_mode == C4_PROVER_MODE_LOCAL ? "local" : (prover_mode == C4_PROVER_MODE_HYBRID ? "hybrid" : "remote"),
                               ctx->verifier.data);
      bytes_write(bytes(content, strlen(content)), fopen(filename, "w"), true);
      safe_free(filename);
      safe_free(content);
    }
    if (verify_flags & VERIFY_FLAG_PROOF_ONLY)
      fwrite(ctx->proof.data, 1, ctx->proof.len, stdout);
    else
      ssz_dump_to_file_no_quotes(stdout, ctx->verifier.data);
    fflush(stdout);
    c4_rpc_ctx_free(ctx);
    return EXIT_SUCCESS;
  }

  c4_state_t* state = c4_rpc_get_state(ctx);
  char* error = state ? state->error : ctx->error;
  fprintf(stderr, "Error: %s\n", error ? error : "unknown error");

  c4_rpc_ctx_free(ctx);
  return EXIT_FAILURE;
}
