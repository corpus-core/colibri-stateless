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
    bn254_fp12_t f;
    
    // Miller Loop result for e(A, B) - Verified
    // c0 (Fp6): c00, c01, c02, c03, c04, c05
    const char* c_vals[] = {
        "1003d7314a18cd67a63964518bd0a6ad6e0bdc88e09017ce4e3cbf27fd331f10",
        "2e8a66d629df9eb661d2de5bcbb03691d67ff3786972a9313bdb22c3d1f3b027",
        "2737e8829c99ce19d33a0509a4e34d98f12a14eb9ca1781de66b9a12c329870a",
        "1664027d7d898c80e29442921b7adfd5da1e1c3bd8678697e04d1435a08a4e25",
        "13b59b6ad01e72db205aea512aa306ff729f32df745b586f25048993e5e51a10",
        "27da01313377817da0fefa1fc89058fecb690d3bb55fd42c0bc9109d56369f3f",
        "1b3e436a1f2ab2230f160a5621ed1140dcb2efc142084c8f27c769a79cc485d8",
        "1f1d3c0b7743f9c7682cebcfeec5657c525b2138a18970bbc60c22c341d320d5",
        "2646bdc41db2b1a464f9b6b782c3fe17d8b94b4a7b3a7cd8017173cd58f46c82",
        "18ca11ffa555817cc25db62db360297338b88b27ef14398bf9374e873adddb79",
        "079ec0840009bae15ad3f43b9de7d8c858b7856116c6651828bae8f6bb293b25",
        "03b8936f26496c338455411144eef97af0e48ffb71dc20235a3c851c1e3e3fb5"
    };
    
    hex2bin(c_vals[0], f.c0.c0.c0.bytes);
    hex2bin(c_vals[1], f.c0.c0.c1.bytes);
    hex2bin(c_vals[2], f.c0.c1.c0.bytes);
    hex2bin(c_vals[3], f.c0.c1.c1.bytes);
    hex2bin(c_vals[4], f.c0.c2.c0.bytes);
    hex2bin(c_vals[5], f.c0.c2.c1.bytes);
    hex2bin(c_vals[6], f.c1.c0.c0.bytes);
    hex2bin(c_vals[7], f.c1.c0.c1.bytes);
    hex2bin(c_vals[8], f.c1.c1.c0.bytes);
    hex2bin(c_vals[9], f.c1.c1.c1.bytes);
    hex2bin(c_vals[10], f.c1.c2.c0.bytes);
    hex2bin(c_vals[11], f.c1.c2.c1.bytes);
    
    bn254_fp12_t r;
    bn254_final_exponentiation(&r, &f);
    
    fp12_print("DEBUG FE RESULT", &r);
    
    return 0;
}


