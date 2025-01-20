#include "../util/bytes.h"
#include "../util/crypto.h"
#include "../util/ssz.h"
#include <stdio.h>
#include <stdlib.h>

const ssz_def_t BEACON_BLOCK_HEADER[] = {
    SSZ_UINT64("slot"),
    SSZ_UINT64("proposerIndex"),
    SSZ_BYTES32("parentRoot"),
    SSZ_BYTES32("stateRoot"),
    SSZ_BYTES32("bodyRoot")};

const ssz_def_t BEACON_BLOCK_HEADER_CONTAINER = SSZ_CONTAINER("BeaconBlockHeader", BEACON_BLOCK_HEADER);

const ssz_def_t BLOCK_HASH_PROOF[] = {
    SSZ_LIST("offsets", SSZ_BYTE, 256),
    SSZ_LIST("leaves", ssz_bytes32, 256),
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),
    SSZ_BYTES32("block_hash"),
    SSZ_BYTES32("sign_message"),
    SSZ_VECTOR("sync_committee", ssz_bls_pubky, 512),
    SSZ_BIT_VECTOR("sync_committee_bits", 512),
    SSZ_BYTES32("sync_committee_signature")};

const ssz_def_t BLOCK_HASH_PROOF_CONTAINER = SSZ_CONTAINER("BlockHashProof", BLOCK_HASH_PROOF);

static bytes_t read_from_stdin() {
  unsigned char  buffer[1024];
  size_t         bytesRead;
  bytes_buffer_t data = {0};

  while ((bytesRead = fread(buffer, 1, 1024, stdin)) > 0)
    buffer_append(&data, bytes(buffer, bytesRead));

  return data.data;
}

static bytes_t read_from_file(const char* filename) {
  unsigned char  buffer[1024];
  size_t         bytesRead;
  bytes_buffer_t data = {0};

  FILE* file = fopen(filename, "rb");
  if (file == NULL) {
    fprintf(stderr, "Error opening file: %s\n", filename);
    exit(EXIT_FAILURE);
  }

  while ((bytesRead = fread(buffer, 1, 1024, file)) > 0)
    buffer_append(&data, bytes(buffer, bytesRead));

  fclose(file);
  return data.data;
}

int main(int argc, char* argv[]) {
  bytes_t  data = read_from_file("../proof_data.ssz"); // read_from_stdin();
  ssz_ob_t res  = ssz_ob(BLOCK_HASH_PROOF_CONTAINER, data);

  // calculate hash
  bytes32_t hashroot;
  ssz_hash_tree_root(ssz_get(&res, "header"), hashroot);

  print_hex(stdout, bytes(hashroot, 32), "\nBlochHeader is  : 0x", "\n");
  print_hex(stdout, ssz_get(&res, "block_hash").bytes, "\nBlochHeader must: 0x", "\n");

  // verify signature
  ssz_ob_t sync_committee_bits      = ssz_get(&res, "sync_committee_bits");
  ssz_ob_t sync_committee           = ssz_get(&res, "sync_committee");
  ssz_ob_t sync_committee_signature = ssz_get(&res, "sync_committee_signature");
  ssz_ob_t sign_message             = ssz_get(&res, "sign_message");

  if (!blst_verify(
          sign_message.bytes.data,
          sync_committee_signature.bytes.data,
          sync_committee.bytes.data, 512,
          sync_committee_bits.bytes))
    printf("Sync committee is NOT valid\n");

  //    printf("Sync committee is valid: %d\n", valid);

  /*

      if (blst_verify(sign_message.bytes.data, sync_committee_signature.bytes.data, sync_committee.bytes.data, 512, sync_committee_bits.bytes))
          printf("Sync committee is valid\n");
      else
          printf("Sync committee is invalid\n");






      for (int i = 1; i < argc; i++)
          res = ssz_get(&res, argv[i]);

      ssz_dump(stdout, res, false, 0);
      bytes32_t hashroot;
      ssz_hash_tree_root(res, hashroot);
      print_hex(stdout, bytes(hashroot,32), "\nHR: 0x", "\n");
  */
}

/*
int main_old(int argc, char *argv[]) {

    bytes_t data = read_from_file("testdata.ssz");// read_from_stdin();
    ssz_ob_t in_data = ssz_ob(CB_CONTAINER,data);
    ssz_ob_t res = in_data;
        // Iterate over each argument
    for (int i = 1; i < argc; i++)
        res = ssz_get(&res, argv[i]);

    if (res.bytes.data==NULL)
       fprintf(stderr, "Nothing found!\n");
    else
       ssz_dump(stdout, res, false, 0);

       printf("\n_________\n");
       ssz_builder_t buffer = {0};
       buffer.def = &CB_CONTAINER;

       ssz_add_uint8(&buffer, ssz_get(&res,"fix").bytes.data[0]);
       ssz_add_bytes(&buffer, "dynamic", ssz_get(&res,"dynamic").bytes);

       ssz_ob_t sub = ssz_get(&res,"sub");
       ssz_builder_t sub_buffer = {0};
       sub_buffer.def = &CA_CONTAINER;
       ssz_add_uint8(&sub_buffer, ssz_get(&sub,"fix").bytes.data[0]);
       ssz_add_bytes(&sub_buffer, "dynamic", ssz_get(&sub,"dynamic").bytes);
       ssz_ob_t sub_new = ssz_builder_to_bytes(&sub_buffer);
       ssz_add_bytes(&buffer,"sub",sub_new.bytes);
       free(sub_new.bytes.data);

       bytes32_t hashroot;

       ssz_hash_tree_root(res, hashroot);
       print_hex(stdout, bytes(hashroot,32), "HR: 0x", "\n");

       ssz_ob_t out_bytes = ssz_builder_to_bytes(&buffer);
       ssz_dump(stdout, out_bytes, false, 0);


       print_hex(stdout, out_bytes.bytes, "Hex: 0x", "\n");
       bytes32_t hash;
       sha256(out_bytes.bytes, hash);
       print_hex(stdout, bytes(hash, 32), "Hash: 0x", "\n");

       free(out_bytes.bytes. data);
       if (argc=100)
          blst_verify(hash, res.bytes.data, res.bytes.data, 1, res.bytes);






    return 0;
}
*/