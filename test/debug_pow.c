#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../src/chains/eth/bn254/bn254.c"

// Helper to construct fp12 from hex strings (via python-like logic) is hard in C.
// I will just copy the bytes from the trace for x (f_easy).

// x = f_easy
// 13|DEBUG f_easy: 0ed1f35f335e04772377aae72d0d4de7fe92afd3a2317270f9189aff102d9c08 ...

// I will parse the hex string manually or just hardcode the byte array?
// Hex string is easier to copy-paste.

void hex_to_bytes(const char* hex, uint8_t* bytes) {
    for (int i = 0; i < 32; i++) {
        sscanf(hex + 2*i, "%02hhx", &bytes[i]);
    }
}

int main() {
    bn254_init();
    
    bn254_fp12_t x;
    bn254_fp_t* p = (bn254_fp_t*)&x;
    
    // Copy from trace
    const char* c000 = "0ed1f35f335e04772377aae72d0d4de7fe92afd3a2317270f9189aff102d9c08";
    const char* c001 = "0888db44e812d1d0f6e680308c3134d80a3bb726abe725047f36a5e763451248";
    const char* c010 = "2cb8e26fb098851fd373bd1ba1ca21244cbb5049642a3d80660105b6b6594179";
    const char* c011 = "03d14ddcdcc53d18f41a6503e274b46239438a0d0f33fd202c8ecb475e3a3212";
    const char* c020 = "2e94cab7f487418fac7025c71a516c25cb589bd6da92dda478a7619a711e3008";
    const char* c021 = "1c38c42ed9af9de65cd873702763324f5dbf6613950a1301fbdcf046ba96bbd5";
    const char* c100 = "0e79acc9211de9bfb59e9d23469ee76c472576e734473c45103c534586fa0d00";
    const char* c101 = "2c74beaa28ed9b02fff875c0cd439b46b0ce45415ab75978816e9e91b4c2ac2c";
    const char* c110 = "05f274a3467b583c9e728acee71fef3930d14f7183877cfb9cf0f31ab270267f";
    const char* c111 = "27f3f42a7f71fbe747c5c75dd96db91aeb7c816bc3bb71ad835d5299ed29566b";
    const char* c120 = "11c0527d00a11a68cb9cd12a88aaee9668025b0a3fbbed0ff16a03c2d3696e4c";
    const char* c121 = "1cbbf615425cf9f8e38e8ec249d0dd12a66dee6ae3105a7b658ce808ec8b6e65";
    
    hex_to_bytes(c000, x.c0.c0.c0.bytes);
    hex_to_bytes(c001, x.c0.c0.c1.bytes);
    hex_to_bytes(c010, x.c0.c1.c0.bytes);
    hex_to_bytes(c011, x.c0.c1.c1.bytes);
    hex_to_bytes(c020, x.c0.c2.c0.bytes);
    hex_to_bytes(c021, x.c0.c2.c1.bytes);
    
    hex_to_bytes(c100, x.c1.c0.c0.bytes);
    hex_to_bytes(c101, x.c1.c0.c1.bytes);
    hex_to_bytes(c110, x.c1.c1.c0.bytes);
    hex_to_bytes(c111, x.c1.c1.c1.bytes);
    hex_to_bytes(c120, x.c1.c2.c0.bytes);
    hex_to_bytes(c121, x.c1.c2.c1.bytes);
    
    printf("DEBUG x (f_easy):\n");
    fp12_print("x", &x);
    
    // Compute x^2
    bn254_fp12_t x2 = x;
    fp12_sqr(&x2, &x2);
    
    printf("DEBUG x^2:\n");
    fp12_print("x^2", &x2);
    
    return 0;
}


