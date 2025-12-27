#include "zk_verifier.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bytes.h"

void print_usage(const char* prog_name) {
    printf("Usage: %s <proof_file> <public_values_file>\n", prog_name);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    const char* proof_file = argv[1];
    const char* pub_file = argv[2];
    
    printf("Verifying proof:\n");
    printf("  Proof File: %s\n", proof_file);
    printf("  Public Values: %s\n", pub_file);
    
    bytes_t proof = bytes_read((char*)proof_file);
    if (!proof.data) {
        fprintf(stderr, "Failed to read proof file\n");
        return 1;
    }
    
    bytes_t pub = bytes_read((char*)pub_file);
    if (!pub.data) {
        fprintf(stderr, "Failed to read public values file\n");
        free(proof.data);
        return 1;
    }

    // Verify
    bool valid = verify_zk_proof(proof, pub);
    
    free(proof.data);
    free(pub.data);

    if (valid) {
        printf("Verification SUCCESS! ✅\n");
        return 0;
    } else {
        printf("Verification FAILED ❌\n");
        return 1;
    }
}
