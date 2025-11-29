#include <stdio.h>
#include <string.h>
#include "../src/chains/eth/bn254/bn254.c"
#include "../libs/intx/intx_c_api.h"

void hex2bin(const char* hex, uint8_t* bin) {
    for(int i=0; i<32; i++) {
        sscanf(hex + 2*i, "%2hhx", &bin[i]);
    }
}

int main() {
    bn254_init();
    bn254_fp12_t l_dbl, l_add, res;
    memset(&l_dbl, 0, sizeof(l_dbl));
    memset(&l_add, 0, sizeof(l_add));
    
    // L_DBL
    // l_a (c1.c1)
    const char* la_dbl_c0 = "23e436d04ab88db6ee05346199d632f2a969e7ea4754f8c072c2938c8b863098";
    const char* la_dbl_c1 = "1afa89682363645e1569493e6245e6f40906e5f810ec1d456ef0c0638ccea6dc";
    hex2bin(la_dbl_c0, l_dbl.c1.c1.c0.bytes);
    hex2bin(la_dbl_c1, l_dbl.c1.c1.c1.bytes);
    
    // l_c (c1.c0)
    const char* lc_dbl_c0 = "04ea72552dab2205a779a55c26ebdb421593f4fb597774f3cd03b6085e9218e9";
    const char* lc_dbl_c1 = "249e9d5a2ece2cda34c794f4a7e573f94882126052729b2be5f34eebdfb3923e";
    hex2bin(lc_dbl_c0, l_dbl.c1.c0.c0.bytes);
    hex2bin(lc_dbl_c1, l_dbl.c1.c0.c1.bytes);
    
    // l_b (c0.c0)
    const char* lb_dbl_c0 = "1352ed7b50f57c9d8d87a36d493cd5263572cc70a720abb1da3f44b02fd2c7de";
    hex2bin(lb_dbl_c0, l_dbl.c0.c0.c0.bytes);
    
    // L_ADD
    // l_a (c1.c1)
    const char* la_add_c0 = "0715642eb535db9e820f390d2da685720730773e481d205ff3cd93cd5f433c0b";
    const char* la_add_c1 = "2e818f5ceceb5be92df49353afe8c0c90d1f21912cacd51587e44bd28e2123c7";
    hex2bin(la_add_c0, l_add.c1.c1.c0.bytes);
    hex2bin(la_add_c1, l_add.c1.c1.c1.bytes);
    
    // l_c (c1.c0)
    const char* lc_add_c0 = "2936fe82335532a27c183102bcbbbe971aa81b2901af1a3713357316a7914ebf";
    const char* lc_add_c1 = "1be82408b32d93f1a623fcb410499b7dc9ea425150b4d6de0d7d7528c37dc5b6";
    hex2bin(lc_add_c0, l_add.c1.c0.c0.bytes);
    hex2bin(lc_add_c1, l_add.c1.c0.c1.bytes);
    
    // l_b (c0.c0)
    const char* lb_add_c0 = "1352ed7b50f57c9d8d87a36d493cd5263572cc70a720abb1da3f44b02fd2c7de";
    hex2bin(lb_add_c0, l_add.c0.c0.c0.bytes);
    
    fp12_mul_internal(&res, &l_dbl, &l_add);
    
    fp12_print("DEBUG TEST RES", &res);
    
    return 0;
}
