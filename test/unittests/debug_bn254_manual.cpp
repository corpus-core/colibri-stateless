#include "bn254.h"
#include "zk_verifier_constants.h"
#include "intx_c_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Include MCL C++ headers
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <mcl/bn.hpp>
#pragma GCC diagnostic pop

using namespace mcl::bn;

static void print_hex(const char* label, const uint8_t* data, size_t len) {
    printf("%s: ", label);
    for (size_t i = 0; i < len; i++) {
        printf("%02x", data[i]);
    }
    printf("\n");
}

static void hex2bin(const char* hex, uint8_t* out) {
    for(int i=0; i<32; i++) sscanf(hex + 2*i, "%2hhx", &out[i]);
}

// We include bn254.c
// This is a hack to allow direct access to static functions for debugging.
#include "../../src/chains/eth/bn254/bn254.c"

void compare_pairing() {
    try {
        bn254_init();
        initPairing(mcl::BN_SNARK1);
        
        bn254_g1_t P;
        bn254_g2_t Q;
        
        // P = G1 Generator
        printf("Setting up P...\n");
    uint8_t p_bytes[64]; memset(p_bytes, 0, 64);
    p_bytes[31] = 1; p_bytes[63] = 2;
    bn254_g1_from_bytes_be(&P, p_bytes);
    
    // Q = G2 Generator
    uint8_t q_bytes[128];
    const char* x_im_hex = "198e9393920d483a7260bfb731fb5d25f1aa493335a9e71297e485b7aef312c2";
    const char* x_re_hex = "1800deef121f1e76426a00665e5c4479674322d4f75edadd46debd5cd992f6ed";
    const char* y_im_hex = "090689d0585ff075ec9e99ad690c3395bc4b313370b38ef355acdadcd122975b";
    const char* y_re_hex = "12c85ea5db8c6deb4aab71808dcb408fe3d1e7690c43d37b4ce6cc0166fa7daa";
    
    hex2bin(x_im_hex, q_bytes); hex2bin(x_re_hex, q_bytes + 32);
    hex2bin(y_im_hex, q_bytes + 64); hex2bin(y_re_hex, q_bytes + 96);
    bn254_g2_from_bytes_eth(&Q, q_bytes);
    
    // MCL Setup
    G1 mP;
    G2 mQ;
    Fp12 mRes;
    
    // Load P into mP
    printf("Setting up MCL P...\n");
    // mP.setStr("1 2"); // This throws exception
    mP.x.setStr("1");
    mP.y.setStr("2");
    mP.z.setStr("1");
    
    // Load Q into mQ
    printf("Setting up MCL Q...\n");
    
    Fp x_re, x_im, y_re, y_im;
    x_re.setStr(x_re_hex, 16);
    x_im.setStr(x_im_hex, 16);
    y_re.setStr(y_re_hex, 16);
    y_im.setStr(y_im_hex, 16);
    
    mQ.x.a = x_re; mQ.x.b = x_im;
    mQ.y.a = y_re; mQ.y.b = y_im;
    mQ.z = 1;
    
    printf("Computing MCL Pairing...\n");
    // Compute MCL Pairing
    mcl::bn::pairing(mRes, mP, mQ);
    
    printf("MCL Pairing Result:\n");
    uint8_t mcl_bytes[576]; // enough for Fp12 serialization
    size_t n = mRes.serialize(mcl_bytes, 576);
    print_hex("MCL", mcl_bytes, n);
    
    // Compute ETH Pairing
    bn254_fp12_t ethRes;
    bn254_miller_loop(&ethRes, &P, &Q);
    bn254_final_exponentiation(&ethRes, &ethRes);
    
    printf("ETH Pairing Result:\n");
    // Need to serialize ethRes to match MCL serialization?
    // eth_bn254 is Fp12 (c0, c1). c0=Fp6, c1=Fp6.
    // c0 = c00 + c01 v + c02 v^2
    // c1 = c10 + c11 v + c12 v^2
    // MCL serialization order?
    // Usually c00 c01 c02 c10 c11 c12 or similar.
    // Or maybe d0 d1 ... d11.
    
    // We can just print components for manual check.
    uint8_t eth_bytes[384];
    // Dump strictly: c0.c0.c0, c0.c0.c1, ...
    uint8_t* ptr = eth_bytes;
    // c0 (Fp6)
    // c0.c0 (Fp2) -> c0.c0.c0, c0.c0.c1
    memcpy(ptr, ethRes.c0.c0.c0.bytes, 32); ptr += 32;
    memcpy(ptr, ethRes.c0.c0.c1.bytes, 32); ptr += 32;
    memcpy(ptr, ethRes.c0.c1.c0.bytes, 32); ptr += 32;
    memcpy(ptr, ethRes.c0.c1.c1.bytes, 32); ptr += 32;
    memcpy(ptr, ethRes.c0.c2.c0.bytes, 32); ptr += 32;
    memcpy(ptr, ethRes.c0.c2.c1.bytes, 32); ptr += 32;
    // c1 (Fp6)
    memcpy(ptr, ethRes.c1.c0.c0.bytes, 32); ptr += 32;
    memcpy(ptr, ethRes.c1.c0.c1.bytes, 32); ptr += 32;
    memcpy(ptr, ethRes.c1.c1.c0.bytes, 32); ptr += 32;
    memcpy(ptr, ethRes.c1.c1.c1.bytes, 32); ptr += 32;
    memcpy(ptr, ethRes.c1.c2.c0.bytes, 32); ptr += 32;
    memcpy(ptr, ethRes.c1.c2.c1.bytes, 32); ptr += 32;
    
    print_hex("ETH", eth_bytes, 384);
    
    // Compare
    // Note: MCL serialization might differ in field order.
    // But if we see the hex, we can judge.
    
    // Let's also print mRes components using getStr to be sure.
    // ...
    } catch (std::exception& e) {
        printf("Exception: %s\n", e.what());
    }
}

int main() {
    compare_pairing();
    return 0;
}
