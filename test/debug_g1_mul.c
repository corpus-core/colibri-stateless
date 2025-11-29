#include <stdio.h>
#include <string.h>
#include "bn254.h"
#include "../../src/chains/eth/bn254/bn254.c"

void print_g1(const char* label, const bn254_g1_t* p) {
    printf("%s:\n", label);
    printf("x: "); for(int i=0; i<32; i++) printf("%02x", p->x.bytes[i]); printf("\n");
    printf("y: "); for(int i=0; i<32; i++) printf("%02x", p->y.bytes[i]); printf("\n");
    printf("z: "); for(int i=0; i<32; i++) printf("%02x", p->z.bytes[i]); printf("\n");
}

int main() {
    bn254_init();
    
    // Point P (Generator or random)
    // Use A from trace
    bn254_g1_t P;
    // 1a78f6...
    uint8_t x_bytes[32] = {
        0x1a, 0x78, 0xf6, 0x83, 0x9b, 0xb5, 0xd8, 0x8d, 0x16, 0x74, 0xdc, 0x0b, 0xb7, 0x23, 0x1a, 0xef,
        0x8a, 0xd3, 0x2a, 0xde, 0xd0, 0x41, 0x8b, 0xcd, 0x3c, 0x0e, 0x81, 0x86, 0x36, 0x5a, 0x44, 0x8f
    };
    // 1d1160...
    uint8_t y_bytes[32] = {
        0x1d, 0x11, 0x60, 0xf7, 0x90, 0x3c, 0x23, 0x8c, 0x2a, 0xc8, 0xa2, 0x49, 0x38, 0x44, 0x83, 0x37,
        0x62, 0x0e, 0x9e, 0x20, 0xc1, 0x51, 0x1e, 0xdb, 0x61, 0xe1, 0x47, 0x66, 0xa8, 0xaa, 0x35, 0x69
    };
    
    memcpy(P.x.bytes, x_bytes, 32);
    memcpy(P.y.bytes, y_bytes, 32);
    memset(P.z.bytes, 0, 32); P.z.bytes[31] = 1; // Z=1
    
    // Scalar s = 12345
    bn254_fp_t s;
    intx_init(&s);
    s.bytes[31] = 0x39; // 57
    s.bytes[30] = 0x30; // 48 -> 0x3039 = 12345
    
    bn254_g1_t R;
    bn254_g1_mul(&R, &P, &s);
    
    print_g1("P", &P);
    print_g1("R = P * 12345", &R);
    
    // Normalize R
    bn254_fp_t z_inv, z2, z3;
    fp_inv(&z_inv, &R.z);
    fp_mul(&z2, &z_inv, &z_inv);
    fp_mul(&z3, &z2, &z_inv);
    fp_mul(&R.x, &R.x, &z2);
    fp_mul(&R.y, &R.y, &z3);
    memset(R.z.bytes, 0, 32); R.z.bytes[31] = 1;
    
    print_g1("R Normalized", &R);
    
    return 0;
}

