#include "zk_verifier.h"
#include "zk_verifier_constants.h"
#include <mcl/bn_c256.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// --- SHA256 Minimal Implementation ---
#define ROTRIGHT(a,b) (((a) >> (b)) | ((a) << (32-(b))))
#define CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x,2) ^ ROTRIGHT(x,13) ^ ROTRIGHT(x,22))
#define EP1(x) (ROTRIGHT(x,6) ^ ROTRIGHT(x,11) ^ ROTRIGHT(x,25))
#define SIG0(x) (ROTRIGHT(x,7) ^ ROTRIGHT(x,18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x,17) ^ ROTRIGHT(x,19) ^ ((x) >> 10))

static const uint32_t k[64] = {
	0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
	0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
	0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
	0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
	0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
	0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
	0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
	0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

typedef struct {
	uint8_t data[64];
	uint32_t datalen;
	uint64_t bitlen;
	uint32_t state[8];
} SHA256_CTX;

static void sha256_transform(SHA256_CTX *ctx, const uint8_t data[]) {
	uint32_t a, b, c, d, e, f, g, h, i, j, t1, t2, m[64];
	for (i = 0, j = 0; i < 16; ++i, j += 4)
		m[i] = (data[j] << 24) | (data[j + 1] << 16) | (data[j + 2] << 8) | (data[j + 3]);
	for ( ; i < 64; ++i)
		m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];
	a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
	e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];
	for (i = 0; i < 64; ++i) {
		t1 = h + EP1(e) + CH(e, f, g) + k[i] + m[i];
		t2 = EP0(a) + MAJ(a, b, c);
		h = g; g = f; f = e; e = d + t1;
		d = c; c = b; b = a; a = t1 + t2;
	}
	ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
	ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(SHA256_CTX *ctx) {
	ctx->datalen = 0;
	ctx->bitlen = 0;
	ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85; ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
	ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c; ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
}

static void sha256_update(SHA256_CTX *ctx, const uint8_t data[], size_t len) {
	size_t i;
	for (i = 0; i < len; ++i) {
		ctx->data[ctx->datalen] = data[i];
		ctx->datalen++;
		if (ctx->datalen == 64) {
			sha256_transform(ctx, ctx->data);
			ctx->bitlen += 512;
			ctx->datalen = 0;
		}
	}
}

static void sha256_final(SHA256_CTX *ctx, uint8_t hash[]) {
	uint32_t i = ctx->datalen;
	if (ctx->datalen < 56) {
		ctx->data[i++] = 0x80;
		while (i < 56) ctx->data[i++] = 0x00;
	} else {
		ctx->data[i++] = 0x80;
		while (i < 64) ctx->data[i++] = 0x00;
		sha256_transform(ctx, ctx->data);
		memset(ctx->data, 0, 56);
	}
	ctx->bitlen += ctx->datalen * 8;
	ctx->data[63] = ctx->bitlen;
	ctx->data[62] = ctx->bitlen >> 8;
	ctx->data[61] = ctx->bitlen >> 16;
	ctx->data[60] = ctx->bitlen >> 24;
	ctx->data[59] = ctx->bitlen >> 32;
	ctx->data[58] = ctx->bitlen >> 40;
	ctx->data[57] = ctx->bitlen >> 48;
	ctx->data[56] = ctx->bitlen >> 56;
	sha256_transform(ctx, ctx->data);
	for (i = 0; i < 4; ++i) {
		hash[i] = (ctx->state[0] >> (24 - i * 8)) & 0x000000ff;
		hash[i + 4] = (ctx->state[1] >> (24 - i * 8)) & 0x000000ff;
		hash[i + 8] = (ctx->state[2] >> (24 - i * 8)) & 0x000000ff;
		hash[i + 12] = (ctx->state[3] >> (24 - i * 8)) & 0x000000ff;
		hash[i + 16] = (ctx->state[4] >> (24 - i * 8)) & 0x000000ff;
		hash[i + 20] = (ctx->state[5] >> (24 - i * 8)) & 0x000000ff;
		hash[i + 24] = (ctx->state[6] >> (24 - i * 8)) & 0x000000ff;
		hash[i + 28] = (ctx->state[7] >> (24 - i * 8)) & 0x000000ff;
	}
}

static void sha256(const uint8_t *data, size_t len, uint8_t hash[32]) {
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, hash);
}

// --- End SHA256 ---

static bool mcl_initialized = false;

static bool init_mcl() {
    if (mcl_initialized) return true;
    int ret = mclBn_init(mclBn_CurveFp254BNb, MCLBN_COMPILED_TIME_VAR);
    if (ret != 0) {
        fprintf(stderr, "Failed to initialize MCL: %d\n", ret);
        return false;
    }
    mcl_initialized = true;
    return true;
}

// Helper to set G1 point from bytes [X(32)][Y(32)]
static int set_g1_from_bytes(mclBnG1 *p, const uint8_t *bytes) {
    char buf[256];
    char x_hex[65], y_hex[65];
    int i;
    
    for(i=0; i<32; i++) sprintf(x_hex + i*2, "%02x", bytes[i]);
    x_hex[64] = 0;
    
    for(i=0; i<32; i++) sprintf(y_hex + i*2, "%02x", bytes[32+i]);
    y_hex[64] = 0;
    
    // Format: 0x... 0x...
    sprintf(buf, "0x%s 0x%s", x_hex, y_hex);
    
    return mclBnG1_setStr(p, buf, strlen(buf), 16);
}

// Helper to set G2 point from bytes [X0(32)][X1(32)][Y0(32)][Y1(32)]
static int set_g2_from_bytes(mclBnG2 *p, const uint8_t *bytes) {
    char buf[512];
    char x0_hex[65], x1_hex[65], y0_hex[65], y1_hex[65];
    int i;
    
    // X0
    for(i=0; i<32; i++) sprintf(x0_hex + i*2, "%02x", bytes[i]);
    x0_hex[64] = 0;
    // X1
    for(i=0; i<32; i++) sprintf(x1_hex + i*2, "%02x", bytes[32+i]);
    x1_hex[64] = 0;
    // Y0
    for(i=0; i<32; i++) sprintf(y0_hex + i*2, "%02x", bytes[64+i]);
    y0_hex[64] = 0;
    // Y1
    for(i=0; i<32; i++) sprintf(y1_hex + i*2, "%02x", bytes[96+i]);
    y1_hex[64] = 0;
    
    // Format: 0x... 0x... 0x... 0x...
    sprintf(buf, "0x%s 0x%s 0x%s 0x%s", x0_hex, x1_hex, y0_hex, y1_hex);
    
    return mclBnG2_setStr(p, buf, strlen(buf), 16);
}

bool verify_zk_proof(const uint8_t* proof_bytes, size_t proof_len, const uint8_t* public_inputs, size_t inputs_len) {
    if (!init_mcl()) return false;

    // 1. Parse Proof
    // Expected size: 4 (selector) + 64 (A) + 128 (B) + 64 (C) = 260
    if (proof_len != 260) {
        fprintf(stderr, "Invalid proof length: %zu (expected 260)\n", proof_len);
        return false;
    }

    mclBnG1 A, C;
    mclBnG2 B;
    
    // Skip selector (4 bytes)
    const uint8_t *p = proof_bytes + 4;
    
    if (set_g1_from_bytes(&A, p) != 0) {
        fprintf(stderr, "Failed to parse A\n");
        return false;
    }
    p += 64;
    
    if (set_g2_from_bytes(&B, p) != 0) {
        fprintf(stderr, "Failed to parse B\n");
        return false;
    }
    p += 128;
    
    if (set_g1_from_bytes(&C, p) != 0) {
        fprintf(stderr, "Failed to parse C\n");
        return false;
    }
    
    // 2. Compute Public Inputs Hash
    uint8_t pub_hash_bytes[32];
    sha256(public_inputs, inputs_len, pub_hash_bytes);
    
    // Mask to 253 bits
    pub_hash_bytes[0] &= 0x1f; // Keep lower 5 bits of first byte (Big Endian)
    
    mclBnFr pub_hash;
    if (mclBnFr_setBigEndianMod(&pub_hash, pub_hash_bytes, 32) != 0) {
        fprintf(stderr, "Failed to set pub hash\n");
        return false;
    }

    // 3. Load VK Constants
    mclBnG1 alpha, ic0, ic1, ic2;
    mclBnG2 beta_neg, gamma_neg, delta_neg;
    
    {
        uint8_t tmp[64];
        memcpy(tmp, VK_ALPHA_X, 32);
        memcpy(tmp+32, VK_ALPHA_Y, 32);
        if(set_g1_from_bytes(&alpha, tmp)) return false;
        
        memcpy(tmp, VK_IC0_X, 32); memcpy(tmp+32, VK_IC0_Y, 32);
        if(set_g1_from_bytes(&ic0, tmp)) return false;

        memcpy(tmp, VK_IC1_X, 32); memcpy(tmp+32, VK_IC1_Y, 32);
        if(set_g1_from_bytes(&ic1, tmp)) return false;

        memcpy(tmp, VK_IC2_X, 32); memcpy(tmp+32, VK_IC2_Y, 32);
        if(set_g1_from_bytes(&ic2, tmp)) return false;
    }
    
    {
        uint8_t tmp[128];
        memcpy(tmp, VK_BETA_NEG_X0, 32); memcpy(tmp+32, VK_BETA_NEG_X1, 32);
        memcpy(tmp+64, VK_BETA_NEG_Y0, 32); memcpy(tmp+96, VK_BETA_NEG_Y1, 32);
        if(set_g2_from_bytes(&beta_neg, tmp)) return false;

        memcpy(tmp, VK_GAMMA_NEG_X0, 32); memcpy(tmp+32, VK_GAMMA_NEG_X1, 32);
        memcpy(tmp+64, VK_GAMMA_NEG_Y0, 32); memcpy(tmp+96, VK_GAMMA_NEG_Y1, 32);
        if(set_g2_from_bytes(&gamma_neg, tmp)) return false;

        memcpy(tmp, VK_DELTA_NEG_X0, 32); memcpy(tmp+32, VK_DELTA_NEG_X1, 32);
        memcpy(tmp+64, VK_DELTA_NEG_Y0, 32); memcpy(tmp+96, VK_DELTA_NEG_Y1, 32);
        if(set_g2_from_bytes(&delta_neg, tmp)) return false;
    }
    
    // 4. Compute L
    // L = IC[0] + vkey * IC[1] + pub_hash * IC[2]
    mclBnFr vkey_fr;
    if (mclBnFr_setBigEndianMod(&vkey_fr, VK_PROGRAM_HASH, 32) != 0) return false;
    
    mclBnG1 L, t1, t2;
    L = ic0;
    
    mclBnG1_mul(&t1, &ic1, &vkey_fr);
    mclBnG1_mul(&t2, &ic2, &pub_hash);
    
    mclBnG1_add(&L, &L, &t1);
    mclBnG1_add(&L, &L, &t2);
    
    // 5. Pairing Check
    // e(A, B) * e(C, DeltaNeg) * e(Alpha, BetaNeg) * e(L, GammaNeg) == 1
    mclBnGT e1, e2, e3, e4, res;
    
    mclBn_pairing(&e1, &A, &B);
    mclBn_pairing(&e2, &C, &delta_neg);
    mclBn_pairing(&e3, &alpha, &beta_neg);
    mclBn_pairing(&e4, &L, &gamma_neg);
    
    mclBnGT_mul(&res, &e1, &e2);
    mclBnGT_mul(&res, &res, &e3);
    mclBnGT_mul(&res, &res, &e4);
    
    if (mclBnGT_isOne(&res)) {
        printf("ZK Verification SUCCESS!\n");
        return true;
    } else {
        fprintf(stderr, "ZK Verification FAILED.\n");
        return false;
    }
}
