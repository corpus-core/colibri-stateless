#include <stdio.h>
#include <string.h>
//#include "../../src/chains/eth/bn254/bn254.h"
#include "../../src/chains/eth/bn254/bn254.c"
#include "../../libs/intx/intx_c_api.h"

void print_fp2(const char* name, const bn254_fp2_t* a) {
    printf("%s: ", name);
    for(int i=0; i<32; i++) printf("%02x", a->c0.bytes[i]);
    printf(", ");
    for(int i=0; i<32; i++) printf("%02x", a->c1.bytes[i]);
    printf("\n");
}

int main() {
    bn254_init();
    bn254_fp2_t lambda, lc, xP;
    bn254_fp_t px;
    
    // Lambda: 19c9ff6de1446785de1ee54017a07af7f650b609b9063eebdc929c4be52b0d00, 0ee47bb1a1f18943866cb0c9f675b23b075064e14354efb9596961fb5d97ce96
    const char* lam_c0_hex = "19c9ff6de1446785de1ee54017a07af7f650b609b9063eebdc929c4be52b0d00";
    const char* lam_c1_hex = "0ee47bb1a1f18943866cb0c9f675b23b075064e14354efb9596961fb5d97ce96";
    
    // Px: 1a78f6839bb5d88d1674dc0bb7231aef8ad32aded0418bcd3c0e8186365a448f
    const char* px_hex = "1a78f6839bb5d88d1674dc0bb7231aef8ad32aded0418bcd3c0e8186365a448f";
    
    // Parse hex to bytes (BE)
    // Assuming hex string does not have 0x prefix
    auto hex2bin = [](const char* hex, uint8_t* bin) {
        for(int i=0; i<32; i++) {
            sscanf(hex + 2*i, "%2hhx", &bin[i]);
        }
    };
    
    hex2bin(lam_c0_hex, lambda.c0.bytes);
    hex2bin(lam_c1_hex, lambda.c1.bytes);
    hex2bin(px_hex, px.bytes);
    
    memset(&xP, 0, sizeof(xP));
    xP.c0 = px;
    
    print_fp2("Lambda", &lambda);
    print_fp2("xP", &xP);
    
    // l_c = -lambda * xP
    fp2_mul(&lc, &lambda, &xP);
    fp2_neg(&lc, &lc);
    
    print_fp2("l_c", &lc);
    
    return 0;
}

