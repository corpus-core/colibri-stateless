static void fp_div2(bn254_fp_t* r, const bn254_fp_t* a) {
    // (p+1)/2
    static const uint8_t INV2[] = {
        0x18, 0x32, 0x27, 0x39, 0x70, 0x98, 0xd0, 0x14, 0xdc, 0x28, 0x22, 0xdb, 0x40, 0xc0, 0xac, 0x2e,
        0xcb, 0xc0, 0xb5, 0x48, 0xb4, 0x38, 0xe5, 0x46, 0x9e, 0x10, 0x46, 0x0b, 0x6c, 0x3e, 0x7e, 0xa4
    };
    bn254_fp_t inv2;
    memset(inv2.bytes, 0, 32);
    memcpy(inv2.bytes, INV2, 32);
    fp_mul(r, a, &inv2);
}

static void fp2_div2(bn254_fp2_t* r, const bn254_fp2_t* a) {
    fp_div2(&r->c0, &a->c0);
    fp_div2(&r->c1, &a->c1);
}

static void line_func_dbl(bn254_fp12_t* f, bn254_g2_t* Q, const bn254_g1_t* P) {
    bn254_fp2_t t0, t1, t2, t3, t4, t5;
    bn254_fp2_t T0, T1;
    
    fp2_sqr(&t0, &Q->z);
    fp2_mul(&t4, &Q->x, &Q->y);
    fp2_sqr(&t1, &Q->y);
    fp2_add(&t3, &t0, &t0);
    fp2_div2(&t4, &t4);
    fp2_add(&t5, &t0, &t1);
    fp2_add(&t0, &t0, &t3);
    
    fp2_mul_twist_b(&t2, &t0);
    
    fp2_sqr(&t0, &Q->x);
    fp2_add(&t3, &t2, &t2);
    fp2_add(&t3, &t3, &t2);
    fp2_sub(&Q->x, &t1, &t3);
    fp2_add(&t3, &t3, &t1);
    fp2_mul(&Q->x, &Q->x, &t4);
    fp2_div2(&t3, &t3);
    fp2_sqr(&T0, &t3);
    fp2_sqr(&T1, &t2);
    fp2_sub(&T0, &T0, &T1);
    fp2_add(&T1, &T1, &T1);
    fp2_sub(&T0, &T0, &T1);
    fp2_add(&t3, &Q->y, &Q->z);
    Q->y = T0;
    fp2_sqr(&t3, &t3);
    fp2_sub(&t3, &t3, &t5);
    fp2_mul(&Q->z, &t1, &t3);
    
    bn254_fp2_t l_a, l_b, l_c;
    fp2_sub(&l_a, &t2, &t1);
    l_c = t0;
    l_b = t3;
    
    bn254_fp2_t py_fp2; py_fp2.c0 = P->y; memset(&py_fp2.c1, 0, 32);
    fp2_mul(&l_b, &l_b, &py_fp2);
    
    bn254_fp2_t px_fp2; px_fp2.c0 = P->x; memset(&px_fp2.c1, 0, 32);
    fp2_mul(&l_c, &l_c, &px_fp2);
    
    bn254_fp12_t l; memset(&l, 0, sizeof(bn254_fp12_t));
    l.c1.c1 = l_a;
    l.c0.c0 = l_b;
    l.c1.c0 = l_c;
    
    fp12_mul_internal(f, f, &l);
}

static void line_func_add(bn254_fp12_t* f, bn254_g2_t* R, const bn254_g2_t* Q, const bn254_g1_t* P) {
    bn254_fp2_t t1, t2, t3, t4;
    bn254_fp2_t T1, T2;
    
    fp2_mul(&t1, &R->z, &Q->x);
    fp2_mul(&t2, &R->z, &Q->y);
    fp2_sub(&t1, &R->x, &t1);
    fp2_sub(&t2, &R->y, &t2);
    fp2_sqr(&t3, &t1);
    fp2_mul(&R->x, &t3, &R->x);
    fp2_sqr(&t4, &t2);
    fp2_mul(&t3, &t3, &t1);
    fp2_mul(&t4, &t4, &R->z);
    fp2_add(&t4, &t4, &t3);
    fp2_sub(&t4, &t4, &R->x);
    fp2_sub(&t4, &t4, &R->x);
    fp2_sub(&R->x, &R->x, &t4);
    fp2_mul(&T1, &t2, &R->x);
    fp2_mul(&T2, &t3, &R->y);
    fp2_sub(&T2, &T1, &T2);
    R->y = T2;
    fp2_mul(&R->x, &t1, &t4);
    fp2_mul(&R->z, &t3, &R->z);
    
    bn254_fp2_t l_c, l_b, l_a;
    fp2_neg(&l_c, &t2);
    fp2_mul(&T1, &t2, &Q->x);
    fp2_mul(&T2, &t1, &Q->y);
    fp2_sub(&T1, &T1, &T2);
    l_b = t1;
    l_a = T1;
    
    bn254_fp2_t py_fp2; py_fp2.c0 = P->y; memset(&py_fp2.c1, 0, 32);
    fp2_mul(&l_b, &l_b, &py_fp2);
    
    bn254_fp2_t px_fp2; px_fp2.c0 = P->x; memset(&px_fp2.c1, 0, 32);
    fp2_mul(&l_c, &l_c, &px_fp2);
    
    bn254_fp12_t l; memset(&l, 0, sizeof(bn254_fp12_t));
    l.c1.c1 = l_a;
    l.c0.c0 = l_b;
    l.c1.c0 = l_c;
    
    fp12_mul_internal(f, f, &l);
}

