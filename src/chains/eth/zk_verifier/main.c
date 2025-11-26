#include "zk_verifier.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void print_usage(const char* prog_name) {
    printf("Usage: %s <proof_file> <public_values_file>\n", prog_name);
}

uint8_t* read_file(const char* filename, size_t* out_len) {
    FILE* f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open file: %s\n", filename);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    uint8_t* buf = malloc(fsize);
    if (fread(buf, 1, fsize, f) != fsize) {
        fprintf(stderr, "Failed to read file: %s\n", filename);
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_len = fsize;
    return buf;
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
    
    size_t proof_len;
    uint8_t* proof_bytes = read_file(proof_file, &proof_len);
    if (!proof_bytes) return 1;
    
    size_t pub_len;
    uint8_t* pub_bytes = read_file(pub_file, &pub_len);
    if (!pub_bytes) {
        free(proof_bytes);
        return 1;
    }

    // Verify
    bool valid = verify_zk_proof(proof_bytes, proof_len, pub_bytes, pub_len);
    
    free(proof_bytes);
    free(pub_bytes);

    if (valid) {
        printf("Verification SUCCESS! ✅\n");
        return 0;
    } else {
        printf("Verification FAILED ❌\n");
        return 1;
    }
}
