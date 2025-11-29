#include <mcl/bn.hpp>
#include <iostream>
#include <fstream>
#include <vector>
#include <stdio.h>

extern "C" {
#include "../../src/chains/eth/zk_verifier/zk_verifier_constants.h"
#include "../../src/util/crypto.h"
}

using namespace mcl::bn;

// Helper to read binary file
std::vector<uint8_t> read_file(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open " << path << std::endl;
        return {};
    }
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

void load_g1(G1& P, const uint8_t* bytes_x, const uint8_t* bytes_y) {
    char buf[128];
    for(int i=0; i<32; i++) snprintf(buf+2*i, 3, "%02x", bytes_x[i]);
    Fp x; x.setStr(buf, 16);
    for(int i=0; i<32; i++) snprintf(buf+2*i, 3, "%02x", bytes_y[i]);
    Fp y; y.setStr(buf, 16);
    P.set(x, y);
}

// Trying X1 as Imaginary, X0 as Real (EVM standard)
void load_g2_evm(G2& P, const uint8_t* x1, const uint8_t* x0, const uint8_t* y1, const uint8_t* y0) {
    char buf[128];
    // X
    for(int i=0; i<32; i++) snprintf(buf+2*i, 3, "%02x", x0[i]); // Re
    Fp x_re; x_re.setStr(buf, 16);
    for(int i=0; i<32; i++) snprintf(buf+2*i, 3, "%02x", x1[i]); // Im
    Fp x_im; x_im.setStr(buf, 16);
    Fp2 x; x.set(x_re, x_im);

    // Y
    for(int i=0; i<32; i++) snprintf(buf+2*i, 3, "%02x", y0[i]); // Re
    Fp y_re; y_re.setStr(buf, 16);
    for(int i=0; i<32; i++) snprintf(buf+2*i, 3, "%02x", y1[i]); // Im
    Fp y_im; y_im.setStr(buf, 16);
    Fp2 y; y.set(y_re, y_im);

    P.set(x, y);
}

// Helper for input from file
void load_g1_from_stream(G1& P, const uint8_t* p) {
    load_g1(P, p, p+32);
}

void load_g2_from_stream(G2& P, const uint8_t* p) {
    // EVM format: X_im, X_re, Y_im, Y_re
    load_g2_evm(P, p, p+32, p+64, p+96);
}

int main() {
    initPairing(mcl::BN_SNARK1);

    // 1. Load Data
    std::string proof_path = "test/data/zk_data/proof_1600_raw.bin";
    std::string pub_path = "test/data/zk_data/public_values_1600.bin";
    
    auto proof = read_file(proof_path.c_str());
    auto pub = read_file(pub_path.c_str());

    if (proof.empty() || pub.empty()) {
        // Try build relative
         proof = read_file(("../" + proof_path).c_str());
         pub = read_file(("../" + pub_path).c_str());
    }
    
    if (proof.size() != 260) {
        std::cerr << "Invalid proof size: " << proof.size() << std::endl;
        return 1;
    }

    // 2. Parse Proof
    G1 A, C;
    G2 B;
    const uint8_t* p = proof.data() + 4;
    load_g1_from_stream(A, p); p += 64;
    load_g2_from_stream(B, p); p += 128;
    load_g1_from_stream(C, p);

    // 3. Public Inputs Hash
    uint8_t pub_hash_bytes[32];
    bytes_t pub_bytes = { (uint32_t)pub.size(), pub.data() };
    sha256(pub_bytes, pub_hash_bytes);
    pub_hash_bytes[0] &= 0x1f;

    Fr pub_hash;
    char buf[128];
    for(int i=0; i<32; i++) snprintf(buf+2*i, 3, "%02x", pub_hash_bytes[i]);
    pub_hash.setStr(buf, 16);
    std::cout << "PubHash: " << pub_hash << std::endl;

    // 4. VK Constants
    G1 alpha, ic0, ic1, ic2;
    G2 beta_neg, gamma_neg, delta_neg;
    
    load_g1(alpha, VK_ALPHA_X, VK_ALPHA_Y);
    load_g1(ic0, VK_IC0_X, VK_IC0_Y);
    load_g1(ic1, VK_IC1_X, VK_IC1_Y);
    load_g1(ic2, VK_IC2_X, VK_IC2_Y);
    
    // Try loading G2 using X1=Im, X0=Re
    load_g2_evm(beta_neg, VK_BETA_NEG_X1, VK_BETA_NEG_X0, VK_BETA_NEG_Y1, VK_BETA_NEG_Y0);
    load_g2_evm(gamma_neg, VK_GAMMA_NEG_X1, VK_GAMMA_NEG_X0, VK_GAMMA_NEG_Y1, VK_GAMMA_NEG_Y0);
    load_g2_evm(delta_neg, VK_DELTA_NEG_X1, VK_DELTA_NEG_X0, VK_DELTA_NEG_Y1, VK_DELTA_NEG_Y0);

    // 5. Compute L
    Fr vkey_fr;
    for(int i=0; i<32; i++) snprintf(buf+2*i, 3, "%02x", VK_PROGRAM_HASH[i]);
    vkey_fr.setStr(buf, 16);
    std::cout << "VKeyFr: " << vkey_fr << std::endl;

    G1 L = ic0;
    G1 t1, t2;
    G1::mul(t1, ic1, vkey_fr);
    G1::mul(t2, ic2, pub_hash);
    L = L + t1 + t2;
    L.normalize(); // Ensure affine for printing
    
    std::cout << "L: " << L << std::endl;

    // 6. Pairing Check
    // e(A, B) * e(C, delta_neg) * e(alpha, beta_neg) * e(L, gamma_neg) == 1
    Fp12 e1, e2, e3, e4;
    pairing(e1, A, B);
    pairing(e2, C, delta_neg);
    pairing(e3, alpha, beta_neg);
    pairing(e4, L, gamma_neg);

    Fp12 result = e1 * e2 * e3 * e4;
    
    std::cout << "Result isOne: " << result.isOne() << std::endl;
    if (!result.isOne()) {
        std::cout << "Result: " << result << std::endl;
    }

    return result.isOne() ? 0 : 1;
}

