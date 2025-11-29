// Standard Jacobian Doubling for G2
static void bn254_g2_dbl_jacobian(bn254_g2_t* r, const bn254_g2_t* p) {
    bn254_fp2_t t1, t2, t3, M, S, Y4;
    
    // M = 3X^2
    fp2_sqr(&t1, &p->x);
    fp2_add(&M, &t1, &t1);
    fp2_add(&M, &M, &t1);
    
    // S = 4XY^2
    fp2_sqr(&t2, &p->y); // Y^2
    fp2_mul(&S, &p->x, &t2);
    fp2_add(&S, &S, &S);
    fp2_add(&S, &S, &S);
    
    // Z' = 2YZ
    fp2_mul(&r->z, &p->y, &p->z);
    fp2_add(&r->z, &r->z, &r->z);
    
    // X' = M^2 - 2S
    fp2_sqr(&r->x, &M);
    fp2_add(&t3, &S, &S);
    fp2_sub(&r->x, &r->x, &t3);
    
    // Y' = M(S - X') - 8Y^4
    fp2_sqr(&Y4, &t2); // Y^4
    fp2_add(&Y4, &Y4, &Y4); fp2_add(&Y4, &Y4, &Y4); fp2_add(&Y4, &Y4, &Y4); // 8Y^4
    
    fp2_sub(&t3, &S, &r->x);
    fp2_mul(&r->y, &M, &t3);
    fp2_sub(&r->y, &r->y, &Y4);
}

// Standard Mixed Addition for G2 (P is Jacobian, Q is Affine)
static void bn254_g2_add_mixed(bn254_g2_t* r, const bn254_g2_t* p, const bn254_g2_t* q) {
    if (intx_is_zero(&p->z.c0) && intx_is_zero(&p->z.c1)) { *r = *q; return; }
    // Assumes Q.z is 1 (Affine)
    
    bn254_fp2_t Z1Z1, Z1Z1Z1, U2, S2, H, I, J, rr, V, t1;
    
    fp2_sqr(&Z1Z1, &p->z);
    fp2_mul(&Z1Z1Z1, &Z1Z1, &p->z);
    
    fp2_mul(&U2, &q->x, &Z1Z1);
    fp2_mul(&S2, &q->y, &Z1Z1Z1);
    
    fp2_sub(&H, &U2, &p->x); // H = U2 - X1
    fp2_sub(&rr, &S2, &p->y); // r = S2 - Y1
    
    fp2_sqr(&I, &H); // I = H^2
    fp2_mul(&J, &H, &I); // J = H^3
    
    fp2_mul(&V, &p->x, &I); // V = X1 * I
    
    // X3 = r^2 - J - 2V
    fp2_sqr(&r->x, &rr);
    fp2_sub(&r->x, &r->x, &J);
    fp2_add(&t1, &V, &V);
    fp2_sub(&r->x, &r->x, &t1);
    
    // Y3 = r(V - X3) - Y1 J
    fp2_sub(&t1, &V, &r->x);
    fp2_mul(&r->y, &rr, &t1);
    fp2_mul(&t1, &p->y, &J);
    fp2_sub(&r->y, &r->y, &t1);
    
    // Z3 = Z1 * H
    fp2_mul(&r->z, &p->z, &H);
}

static void line_func_dbl_safe(bn254_fp12_t* f, bn254_g2_t* Q, const bn254_g1_t* P) {
    // Convert Q to affine
    bn254_fp2_t z_inv, z2, z3, xQ, yQ, t1;
    fp2_inv(&z_inv, &Q->z);
    fp2_sqr(&z2, &z_inv);
    fp2_mul(&z3, &z2, &z_inv);
    fp2_mul(&xQ, &Q->x, &z2);
    fp2_mul(&yQ, &Q->y, &z3);
    
    // Slope lambda = 3 xQ^2 / 2 yQ
    bn254_fp2_t lambda, num, den;
    fp2_sqr(&num, &xQ);
    fp2_add(&t1, &num, &num); fp2_add(&num, &num, &t1);
    fp2_add(&den, &yQ, &yQ);
    fp2_inv(&t1, &den);
    fp2_mul(&lambda, &num, &t1);
    
    // Update Q
    bn254_g2_dbl_jacobian(Q, Q);
    
    // Line evaluation: yP - lambda * xP + (lambda * xQ - yQ)
    bn254_fp12_t l; memset(&l, 0, sizeof(l));
    bn254_fp2_t la, lc, lb;
    
    // w^3 term: lambda * xQ - yQ
    fp2_mul(&la, &lambda, &xQ);
    fp2_sub(&la, &la, &yQ);
    l.c1.c1 = la;
    
    // w term: -lambda * xP
    bn254_fp2_t xP; memset(&xP, 0, sizeof(xP)); xP.c0 = P->x;
    fp2_mul(&lc, &lambda, &xP);
    fp2_neg(&lc, &lc);
    l.c1.c0 = lc;
    
    // 1 term: yP
    memset(&lb, 0, sizeof(lb)); lb.c0 = P->y;
    l.c0.c0 = lb;
    
    fp12_mul_internal(f, f, &l);
    
    // Trace
    printf("DEBUG SAFE DBL: l_a (w^3) = "); for(int i=0; i<32; i++) printf("%02x", la.c0.bytes[i]); printf("\n");
    printf("DEBUG SAFE DBL: l_b (1) = "); for(int i=0; i<32; i++) printf("%02x", lb.c0.bytes[i]); printf("\n");
    printf("DEBUG SAFE DBL: l_c (w) = "); for(int i=0; i<32; i++) printf("%02x", lc.c0.bytes[i]); printf("\n");
}

static void line_func_add_safe(bn254_fp12_t* f, bn254_g2_t* T, const bn254_g2_t* Q, const bn254_g1_t* P) {
    // Convert T to affine
    bn254_fp2_t z_inv, z2, z3, xT, yT, t1;
    fp2_inv(&z_inv, &T->z);
    fp2_sqr(&z2, &z_inv);
    fp2_mul(&z3, &z2, &z_inv);
    fp2_mul(&xT, &T->x, &z2);
    fp2_mul(&yT, &T->y, &z3);
    
    // Q is already affine (Z=1)
    // Slope lambda = (yQ - yT) / (xQ - xT)
    bn254_fp2_t lambda, num, den;
    fp2_sub(&num, &Q->y, &yT);
    fp2_sub(&den, &Q->x, &xT);
    fp2_inv(&t1, &den);
    fp2_mul(&lambda, &num, &t1);
    
    // Update T
    bn254_g2_add_mixed(T, T, Q);
    
    // Line: yP - lambda * xP + (lambda * xT - yT)
    bn254_fp12_t l; memset(&l, 0, sizeof(l));
    bn254_fp2_t la, lc, lb;
    
    // w^3 term: lambda * xT - yT
    fp2_mul(&la, &lambda, &xT);
    fp2_sub(&la, &la, &yT);
    l.c1.c1 = la;
    
    // w term: -lambda * xP
    bn254_fp2_t xP; memset(&xP, 0, sizeof(xP)); xP.c0 = P->x;
    fp2_mul(&lc, &lambda, &xP);
    fp2_neg(&lc, &lc);
    l.c1.c0 = lc;
    
    // 1 term: yP
    memset(&lb, 0, sizeof(lb)); lb.c0 = P->y;
    l.c0.c0 = lb;
    
    fp12_mul_internal(f, f, &l);
    
    printf("DEBUG SAFE ADD: l_a (w^3) = "); for(int i=0; i<32; i++) printf("%02x", la.c0.bytes[i]); printf("\n");
    printf("DEBUG SAFE ADD: l_b (1) = "); for(int i=0; i<32; i++) printf("%02x", lb.c0.bytes[i]); printf("\n");
    printf("DEBUG SAFE ADD: l_c (w) = "); for(int i=0; i<32; i++) printf("%02x", lc.c0.bytes[i]); printf("\n");
}


