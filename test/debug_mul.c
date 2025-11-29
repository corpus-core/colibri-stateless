#include <stdio.h>
#include <string.h>
#include "bn254.h"
#include "../libs/intx/intx_c_api.h"

// Re-declare fp_mul and friends if they are static in bn254.c, 
// or just use bn254 internal headers if available.
// Since they are static, I will copy the necessary parts or include bn254.c directly (ugly but works for test).
// Better: expose fp_mul in bn254.h or just use intx directly here to test intx wrapper.

void hex_to_bytes(const char* hex, uint8_t* bytes) {
    for (int i = 0; i < 32; i++) {
        sscanf(hex + 2*i, "%2hhx", &bytes[i]);
    }
}

void print_bytes(const char* label, const uint8_t* bytes) {
    printf("%s: ", label);
    for(int i=0; i<32; i++) printf("%02x", bytes[i]);
    printf("\n");
}

int main() {
    bn254_init(); // Initialize modulus
    
    // y0
    const char* y0_hex = "205d04965ffdb09577a1631dad84eb1694e1d70de643a93a61feb453969f33db";
    // j0
    const char* j0_hex = "0c2e0c5613c5f93a2a0a7ab4a4eecd1edad86631197377388d776758ecee88aa";
    // modulus
    const char* p_hex = "30644e72e131a029b85045b68181585d97816a916871ca8d3c208c16d87cfd47";

    bn254_fp_t a, b, r, mod;
    hex_to_bytes(y0_hex, a.bytes);
    hex_to_bytes(j0_hex, b.bytes);
    hex_to_bytes(p_hex, mod.bytes); // Manually load modulus to check intx directly

    // Check via intx wrapper
    intx_mul_mod(&r, &a, &b, &mod);
    
    print_bytes("y0", a.bytes);
    print_bytes("j0", b.bytes);
    print_bytes("mod", mod.bytes);
    print_bytes("Result (y0 * j0 % mod)", r.bytes);
    
    // Expected result from Python
    // 8ca1ca293be3b1a89c0926b1928157769608a8afa6a27f225fc155ff84f6c0c
    const char* expected_hex = "08ca1ca293be3b1a89c0926b1928157769608a8afa6a27f225fc155ff84f6c0c";
    printf("Expected: %s\n", expected_hex);

    return 0;
}

