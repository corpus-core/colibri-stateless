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

#include "ssz.h"
#include "beacon_types.h"
#include "bytes.h"
#include "chains.h"
#include "crypto.h"
#include "verify.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const ssz_def_t* get_definition(char* typename, chain_id_t chain_id) {
  if (strcmp(typename, "signedblock") == 0) return eth_ssz_type_for_fork(ETH_SSZ_SIGNED_BEACON_BLOCK_CONTAINER, C4_FORK_ELECTRA, chain_id);
  if (strcmp(typename, "blockbody") == 0) return eth_ssz_type_for_fork(ETH_SSZ_BEACON_BLOCK_BODY_CONTAINER, C4_FORK_ELECTRA, chain_id);
  if (strcmp(typename, "lcu") == 0) return eth_get_light_client_update_list(C4_FORK_ELECTRA)->def.vector.type;
  fprintf(stderr, "Unknown type : %s \n", typename);
  exit(EXIT_FAILURE);
}

// : Bindings

// :: CLI

// ::: ssz
// a simple tool converting a ssz-formated proof into json.
//
// ````sh
//     # Convert a ssz-formated proof into json
//     ssz -o block.json block_proof.ssz
// ````
//
// ## Options
//
// | Option         | Argument        | Description                      | Default |
// |----------------|-----------------|----------------------------------|---------|
// | `-t`           | `<typename>`    | Type name                        |         |
// | `-o`           | `<outfile>`     | Output file                      |         |
// | `-h`           |                 | Show hash_tree_root of the SSZ   |         |
// | `-n`           |                 | Show type name in JSON output    |         |
// | `-s`           |                 | Show serialization (hex string)  |         |
// | `<file.ssz>`   |                 | Input SSZ file                   |         |
// | `<field1> ...` |                 | Fields to include in JSON output |         |

int main(int argc, char* argv[]) {
  if (argc == 1) {
    fprintf(stderr, "Usage: %s -t <typename> -o <outfile> -nh <file.ssz> <field1> <field2> ...\n"
                    "\n"
                    "  -c            : chain_id ( ust be prior to the type name)\n"
                    "  -t <typename> : type name\n"
                    "  -o <outfile>  : output file\n"
                    "  -h            : show hash_root\n"
                    "  -n            : show typename\n"
                    "  -s            : show serialization\n"
                    "\n",
            argv[0]);
    exit(EXIT_FAILURE);
  }

  //  ssz_ob_t res = ssz_ob(SIGNED_BEACON_BLOCK_CONTAINER, read_from_file(argv[1]));
  bytes_t    req_data     = bytes_read(argv[1]);
  ssz_ob_t   res          = {.def = c4_get_req_type_from_req(req_data), .bytes = req_data};
  char*      out_filename = NULL;
  bool       show_hash    = false;
  bool       show_name    = false;
  bool       show_serial  = false;
  chain_id_t chain_id     = C4_CHAIN_MAINNET;
  for (int i = 2; i < argc; i++) {
    if (argv[i][0] == '-') {
      for (int j = 1; argv[i][j] != '\0'; j++) {
        if (argv[i][j] == 'h')
          show_hash = true;
        if (argv[i][j] == 'n')
          show_name = true;
        if (argv[i][j] == 's')
          show_serial = true;
        if (argv[i][j] == 'c')
          chain_id = strtol(argv[++i], NULL, 10);
        if (argv[i][j] == 'o') {
          out_filename = argv[i + 1];
          i++;
          break;
        }
        if (argv[i][j] == 't') {
          res.def = get_definition(argv[i + 1], chain_id);
          if (strcmp(argv[i + 1], "lcu") == 0 && req_data.len > 12 && uint64_from_le(req_data.data) > 20000) {
            req_data.data += 12;
            req_data.len -= 12;
            res.bytes = req_data;
          }
          i++;
          break;
        }
      }
    }
    else if (res.def && res.def->type != SSZ_TYPE_CONTAINER) {
      char* endptr;
      long  value = strtol(argv[i], &endptr, 10);
      if (endptr && *endptr == '\0')
        res = ssz_at(res, value);
      else {
        fprintf(stderr, "Invalid value for index : %s!\n", argv[i]);
        exit(EXIT_FAILURE);
      }
    }
    else
      res = ssz_get(&res, argv[i]);
  }

  if (out_filename)
    bytes_write(res.bytes, fopen(out_filename, "wb"), true);

  if (show_serial) {
    for (int i = 0; i < res.bytes.len; i += 32) {
      print_hex(stdout, bytes(res.bytes.data + i, res.bytes.len - i < 32 ? res.bytes.len - i : 32), "# ", "\n");
    }
  }

  if (ssz_is_error(res)) {
    fprintf(stderr, "No value found!\n");
    exit(EXIT_FAILURE);
  }

  ssz_dump_to_file(stdout, res, show_name, false);
  if (show_hash) {
    bytes32_t hashroot;
    ssz_hash_tree_root(res, hashroot);
    print_hex(stdout, bytes(hashroot, 32), "\ntree_hash_root: 0x", "\n");
  }
}