#include <mcl/bn.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <stdio.h>

extern "C" {
#include "../../src/chains/eth/bn254/bn254.h"
}

using namespace mcl::bn;

// Helper to convert hex string to bytes
void hex_to_bytes(const char* hex, uint8_t* out) {
    size_t len = strlen(hex);
    for (size_t i = 0; i < len; i += 2) {
        sscanf(hex + i, "%2hhx", &out[i / 2]);
    }
}

int main() {
    // Init MCL
    initPairing(mcl::BN_SNARK1);

    // Init eth_bn254
    bn254_init();

    // Define P, Q
    // P = (1, 2)
    // Q from hex (same as test_precompile_ecpairing_valid)
    
    const char* P_hex =
      "0000000000000000000000000000000000000000000000000000000000000001"
      "0000000000000000000000000000000000000000000000000000000000000002";

    const char* Q_hex =
      "198e9393920d483a7260bfb731fb5d25f1aa493335a9e71297e485b7aef312c2"
      "1800deef121f1e76426a00665e5c4479674322d4f75edadd46debd5cd992f6ed"
      "090689d0585ff075ec9e99ad690c3395bc4b313370b38ef355acdadcd122975b"
      "12c85ea5db8c6deb4aab71808dcb408fe3d1e7690c43d37b4ce6cc0166fa7daa";

    uint8_t p_bytes[64];
    hex_to_bytes(P_hex, p_bytes);
    
    uint8_t q_bytes[128];
    hex_to_bytes(Q_hex, q_bytes);

    // Setup eth_bn254 points
    bn254_g1_t P_eth;
    bn254_g1_from_bytes_be(&P_eth, p_bytes);
    
    bn254_g2_t Q_eth;
    bn254_g2_from_bytes_eth(&Q_eth, q_bytes);

    // Setup MCL points
    // MCL G1 expects x, y.
    // P is (1, 2)
    G1 P_mcl;
    Fp px, py;
    px.setStr("1");
    py.setStr("2");
    P_mcl.set(px, py, false); // false means not checked yet, verify later?
    std::cerr << "P_mcl: " << P_mcl << std::endl;
    if (!P_mcl.isValid()) {
        std::cerr << "P_mcl invalid!" << std::endl;
    }

    // MCL G2 expects X_re, X_im, Y_re, Y_im (MCL convention)
    // But Input Q_hex is: X_im, X_re, Y_im, Y_re (ETH convention)
    // Wait, bn254_g2_from_bytes_eth reads:
    // bytes[0..31] -> X_im
    // bytes[32..63] -> X_re
    // ...
    
    // MCL Fp2 set(re, im) or set(a, b) where elem = a + bi
    // So Q.x.set(X_re, X_im)
    
    char buf[65];
    
    Fp2 qx, qy;
    
    // X_re
    memcpy(buf, Q_hex + 64, 64); buf[64] = 0;
    Fp x_re; x_re.setStr(buf, 16);
    
    // X_im
    memcpy(buf, Q_hex, 64); buf[64] = 0;
    Fp x_im; x_im.setStr(buf, 16);
    
    qx.set(x_re, x_im);
    
    // Y_re
    memcpy(buf, Q_hex + 192, 64); buf[64] = 0;
    Fp y_re; y_re.setStr(buf, 16);
    
    // Y_im
    memcpy(buf, Q_hex + 128, 64); buf[64] = 0;
    Fp y_im; y_im.setStr(buf, 16);
    
    qy.set(y_re, y_im);
    
    G2 Q_mcl;
    Q_mcl.set(qx, qy, false);
    std::cerr << "Q_mcl: " << Q_mcl << std::endl;
    if (!Q_mcl.isValid()) {
        std::cerr << "Q_mcl invalid!" << std::endl;
        // Try swapping Re/Im?
        // Maybe Q_hex format is different?
        // eth: x = x_re + x_im * i.
        // mcl: Fp2(a, b) = a + bi.
        // So set(x_re, x_im) should be correct.
        // Unless Q is twist on different basis?
        // BN254 Twist: D-type.
        // equation: y^2 = x^3 + 3/(9+u). (or similar).
        // MCL handles this.
    }

    std::cout << "----------------------------------------" << std::endl;
    std::cout << "Running MCL Pairing..." << std::endl;
    Fp12 e_mcl;
    pairing(e_mcl, P_mcl, Q_mcl);
    std::cout << "MCL Result: " << e_mcl << std::endl;

    std::cout << "----------------------------------------" << std::endl;
    std::cout << "Running eth_bn254 Pairing..." << std::endl;
    bn254_fp12_t e_eth, miller;
    bn254_miller_loop(&miller, &P_eth, &Q_eth);
    
    // Convert miller (eth_bn254) to MCL Fp12
    Fp12 miller_mcl;
    uint8_t buf_fp12[384];
    memcpy(buf_fp12, &miller, 384);
    
    // Fp12 structure in C: c0 (Fp6), c1 (Fp6)
    // Fp6: c0 (Fp2), c1 (Fp2), c2 (Fp2)
    // Fp2: c0 (Fp), c1 (Fp)
    // All BE.
    
    // MCL setStr or similar?
    // Better to set components manually to be safe.
    // miller.c0.c0.c0 (Fp)
    
    bn254_fp_t* p_fp = (bn254_fp_t*)&miller;
    // 12 Fp elements.
    // Order:
    // 0: c0.c0.c0 (Re of c0 of c0)
    // 1: c0.c0.c1 (Im of c0 of c0)
    // ...
    
    // MCL Fp12: a (c0), b (c1).
    // Fp6 a: a (c0), b (c1), c (c2).
    // Fp2 a: a (c0), b (c1).
    
    // So the flat array of 12 Fp matches exactly.
    // We just need to load them.
    
    // Access MCL components via getFp2()?
    // Fp12::getFp2() returns Fp2[6].
    // 0: a.a (c0.c0)
    // 1: a.b (c0.c1)
    // 2: a.c (c0.c2)
    // 3: b.a (c1.c0)
    // 4: b.b (c1.c1)
    // 5: b.c (c1.c2)
    
    // My struct:
    // c0 (Fp6) -> c0, c1, c2 (Fp2)
    // c1 (Fp6) -> c0, c1, c2 (Fp2)
    
    // So indices 0..2 map to a.a..a.c
    // indices 3..5 map to b.a..b.c
    
    Fp2* mcl_fp2s = miller_mcl.getFp2();
    for (int i=0; i<6; i++) {
        bn254_fp2_t* my_fp2 = &((bn254_fp2_t*)&miller)[i];
        char hex_buf[65];
        
        // c0
        for(int j=0; j<32; j++) snprintf(hex_buf + 2*j, 3, "%02x", my_fp2->c0.bytes[j]);
        mcl_fp2s[i].a.setStr(hex_buf, 16);
        
        // c1
        for(int j=0; j<32; j++) snprintf(hex_buf + 2*j, 3, "%02x", my_fp2->c1.bytes[j]);
        mcl_fp2s[i].b.setStr(hex_buf, 16);
    }
    
    std::cout << "Eth Miller Result (in MCL): " << miller_mcl << std::endl;
    
    Fp12 final_res_mcl_from_eth;
    mcl::bn::finalExp(final_res_mcl_from_eth, miller_mcl);
    std::cout << "MCL FinalExp(Eth Miller): " << final_res_mcl_from_eth << std::endl;

    bn254_final_exponentiation(&e_eth, &miller);

    // Scalar Mul Test
    std::cout << "----------------------------------------" << std::endl;
    std::cout << "Scalar Mul Test (Small)..." << std::endl;
    
    // P = (1, 2)
    G1 P_small_mcl; P_small_mcl.set(px, py);
    bn254_g1_t P_small_eth = P_eth;
    
    uint256_t scalar_2; memset(scalar_2.bytes, 0, 32); scalar_2.bytes[31] = 2;
    Fr s2; s2 = 2;
    
    G1 res_small_mcl;
    G1::mul(res_small_mcl, P_small_mcl, s2);
    std::cout << "MCL 2*P: " << res_small_mcl << std::endl;
    
    bn254_g1_t res_small_eth;
    bn254_g1_mul(&res_small_eth, &P_small_eth, &scalar_2);
    
    uint8_t res_small_bytes[64];
    bn254_g1_to_bytes(&res_small_eth, res_small_bytes);
    printf("Eth 2*P: ");
    for(int i=0; i<64; i++) printf("%02x", res_small_bytes[i]);
    printf("\n");

    std::cout << "Scalar Mul Test (IC1)..." << std::endl;
    
    // IC1_X (from zk_verifier_constants.h - partially copy pasted or manually set)
    // 06 1c 3f ...
    const char* ic1_x_hex = "061c3fd0fd3da25d2607c227d090cca750ed36c6ec878755e537c1c48951fb4c";
    const char* ic1_y_hex = "0fa17ae9c2033379df7b5c65eff0e107055e9a273e6119a212dd09eb51707219";
    
    G1 ic1_mcl;
    Fp ic1_x, ic1_y;
    ic1_x.setStr(ic1_x_hex, 16);
    ic1_y.setStr(ic1_y_hex, 16);
    ic1_mcl.set(ic1_x, ic1_y);
    
    bn254_g1_t ic1_eth;
    uint8_t ic1_buf[64];
    hex_to_bytes(ic1_x_hex, ic1_buf);
    hex_to_bytes(ic1_y_hex, ic1_buf+32);
    bn254_g1_from_bytes_be(&ic1_eth, ic1_buf);
    
    // VKeyFr
    const char* vkey_hex = "00a61ad8347fe889261a355403eaef5795d3d6adf039126d55da3fe9aa9f2a54";
    Fr vkey_fr;
    vkey_fr.setStr(vkey_hex, 16);
    
    uint256_t vkey_scalar;
    hex_to_bytes(vkey_hex, vkey_scalar.bytes);
    
    // MCL Mul
    G1 res_mcl;
    G1::mul(res_mcl, ic1_mcl, vkey_fr);
    std::cout << "MCL Mul: " << res_mcl << std::endl;
    
    // Eth Mul
    bn254_g1_t res_eth;
    bn254_g1_mul(&res_eth, &ic1_eth, &vkey_scalar);
    
    // Print Eth Res
    uint8_t res_eth_bytes[64];
    bn254_g1_to_bytes(&res_eth, res_eth_bytes);
    printf("Eth Mul: ");
    for(int i=0; i<64; i++) printf("%02x", res_eth_bytes[i]);
    printf("\n");

    // Print result

    // ... (already printed inside functions)

    return 0;
}

