void bn254_init(void) {
    static bool initialized = false;
    if (!initialized) {
        memset(bn254_modulus.bytes, 0, 32);
        reverse_copy(bn254_modulus.bytes, BN254_PRIME, 32);
        initialized = true;
        printf("DEBUG: bn254_init modulus[0]=%02x modulus[31]=%02x\n", bn254_modulus.bytes[0], bn254_modulus.bytes[31]);
    }
}

// -----------------------------------------------------------------------------
// Internal Fp Arithmetic
// -----------------------------------------------------------------------------

static void fp_add(bn254_fp_t* r, const bn254_fp_t* a, const bn254_fp_t* b) {
    intx_add_mod(r, a, b, &bn254_modulus);
}

static void fp_sub(bn254_fp_t* r, const bn254_fp_t* a, const bn254_fp_t* b) {
    intx_sub_mod(r, a, b, &bn254_modulus);
}

static void fp_mul(bn254_fp_t* r, const bn254_fp_t* a, const bn254_fp_t* b) {
    intx_mul_mod(r, a, b, &bn254_modulus);
    // DEBUG: Check basic 1*1
    // if (a->bytes[0] == 1 && b->bytes[0] == 1 && a->bytes[1]==0) {
    //    printf("DEBUG: fp_mul 1*1 => %02x\n", r->bytes[0]);
    // }
}
