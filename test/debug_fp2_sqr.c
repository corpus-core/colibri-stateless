#include <stdio.h>
#include <string.h>
#include "../src/chains/eth/bn254/bn254.c"
#include "../../libs/intx/intx_c_api.h"

void hex2bin(const char* hex, uint8_t* bin) {
    for(int i=0; i<32; i++) {
        sscanf(hex + 2*i, "%2hhx", &bin[i]);
    }
}

void print_fp2(const char* name, const bn254_fp2_t* a) {
    printf("%s:\n", name);
    printf("c0: "); for(int i=0; i<32; i++) printf("%02x", a->c0.bytes[i]); printf("\n");
    printf("c1: "); for(int i=0; i<32; i++) printf("%02x", a->c1.bytes[i]); printf("\n");
}

int main() {
    bn254_init();
    bn254_fp2_t a, r;
    const char* c0_hex = "15e6972b12358521e0d54682cd273798ce4b90c59a5b8c8b0697a05d7cc96aee";
    const char* c6_hex = "19a5c20ca28ec512e782e0e5898f2d0dac0195bb13df59bb15cf080d81e87287";
    
    hex2bin(c0_hex, a.c0.bytes);
    hex2bin(c6_hex, a.c1.bytes);
    
    fp2_sqr(&r, &a);
    
    print_fp2("Result", &r);
    
    return 0;
}

