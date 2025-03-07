#include "ssz.h"
#include "../chains/eth/proofer/ssz_types.h"
#include "../chains/eth/verifier/types_verify.h"
#include "bytes.h"
#include "chains.h"
#include "crypto.h"
#include "verify.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const ssz_def_t* get_def(char* typename) {
  if (strcmp(typename, "signedblock") == 0) return &SIGNED_BEACON_BLOCK_CONTAINER;
  if (strcmp(typename, "blockbody") == 0) return &BEACON_BLOCK_BODY_CONTAINER;
  fprintf(stderr, "Unknown type : %s \n", typename);
  exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
  if (argc == 1) {
    fprintf(stderr, "Usage: %s -t <typename> -o <outfile> -nh <file.ssz> <field1> <field2> ...\n"
                    "\n"
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
  bytes_t  req_data     = bytes_read(argv[1]);
  ssz_ob_t res          = {.def = c4_get_req_type_from_req(req_data), .bytes = req_data};
  char*    out_filename = NULL;
  bool     show_hash    = false;
  bool     show_name    = false;
  bool     show_serial  = false;
  for (int i = 2; i < argc; i++) {
    if (argv[i][0] == '-') {
      for (int j = 1; argv[i][j] != '\0'; j++) {
        if (argv[i][j] == 'h')
          show_hash = true;
        if (argv[i][j] == 'n')
          show_name = true;
        if (argv[i][j] == 's')
          show_serial = true;
        if (argv[i][j] == 'o') {
          out_filename = argv[i + 1];
          i++;
          break;
        }
        if (argv[i][j] == 't') {
          res.def = get_def(argv[i + 1]);
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

  ssz_dump_to_file(stdout, res, show_name, true);
  if (show_hash) {
    bytes32_t hashroot;
    ssz_hash_tree_root(res, hashroot);
    print_hex(stdout, bytes(hashroot, 32), "\ntree_hash_root: 0x", "\n");
  }
}