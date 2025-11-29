from py_ecc.bn128 import FQ12, FQ, FQ2, field_modulus

def to_fq(hex_str):
    return FQ(int(hex_str, 16))

# C components from Loop 62 start (v9 logs)
c0_c = to_fq("15e6972b12358521e0d54682cd273798ce4b90c59a5b8c8b0697a05d7cc96aee")
c1_c = to_fq("2b1a9a0913fe7735eaf4299849df853f0c8c9cfdff389d5c42609b11ee3c4b3b")
c2_c = to_fq("302c590739ff9d8e1279661c5674445e754d6531d102d439c3e0f9c59a4ae06d")
c3_c = to_fq("2797bf25ceaef69e195d22800c76e133349e15014902933184f4debf95754e3f")
c4_c = to_fq("12fe7c63855fb9b1b08da728d652ade2f02ff9c46a631c93542904020d230d76")
c5_c = to_fq("221815d2d23b0efeb583be1da27429656d39e41b4b0b63e199a7ab315c3f7b9b")
c6_c = to_fq("19a5c20ca28ec512e782e0e5898f2d0dac0195bb13df59bb15cf080d81e87287")
c7_c = to_fq("22ee8a0070c6b1a16a334c343531fe854ca7e5fcbcaabd72b47dc7e3c56a30fd")
c8_c = to_fq("068e3b747d7acbe21695316f5dcbff856d1768fcd9711fdcf2b58fce1856eef8")
c9_c = to_fq("265466782cfe67e020e4944ade6c50808decfb8002fc6881a6b05627f74ec154")
c10_c = to_fq("0000000000000000000000000000000000000000000000000000000000000000")
c11_c = to_fq("0000000000000000000000000000000000000000000000000000000000000000")

# Map to Python coeffs
coeffs = [FQ(0)] * 12
coeffs[0] = c0_c - 9 * c6_c
coeffs[6] = c6_c
coeffs[1] = c1_c - 9 * c7_c
coeffs[7] = c7_c
coeffs[2] = c2_c - 9 * c8_c
coeffs[8] = c8_c
coeffs[3] = c3_c - 9 * c9_c
coeffs[9] = c9_c
coeffs[4] = c4_c - 9 * c10_c
coeffs[10] = c10_c
coeffs[5] = c5_c - 9 * c11_c
coeffs[11] = c11_c

f = FQ12(coeffs)

# Extract C0 (even) and C1 (odd) components of FQ12 (mimicking Fp6 elements)
# C0 = coeffs[0] + coeffs[2]w^2 + coeffs[4]w^4 + coeffs[6]w^6 + coeffs[8]w^8 + coeffs[10]w^10
# But w^6 = 9+u.
# In C Fp6: c0 + c1 v + c2 v^2. (v=w^2).
# c0 (Fp2): c00 + c01 u. 
# c0 corresponds to w^0, w^6.
# w^0 term is coeffs[0]. w^6 term is coeffs[6].
# c00 = coeffs[0] + 9 coeffs[6] + coeffs[6] u.
# So c00.c0 = coeffs[0] + 9 coeffs[6]. c00.c1 = coeffs[6].
# This is exactly c0_c and c6_c !

# So C0 (Fp6) components:
# c0 (Fp2): c0_c + c6_c u
# c1 (Fp2): c2_c + c8_c u
# c2 (Fp2): c4_c + c10_c u

# C1 (Fp6) components (coeff of w):
# c0 (Fp2): c1_c + c7_c u
# c1 (Fp2): c3_c + c9_c u
# c2 (Fp2): c5_c + c11_c u

def get_fp2(r, i):
    return FQ2([int(r), int(i)])

# Construct Fp6 elements in Python (using list of 3 FQ2)
# C memory layout: c0.c0.c0, c0.c0.c1, c0.c1.c0, c0.c1.c1, c0.c2.c0, c0.c2.c1
#                  c1.c0.c0, c1.c0.c1, c1.c1.c0, c1.c1.c1, c1.c2.c0, c1.c2.c1
A_c0 = get_fp2(c0_c, c1_c)
A_c1 = get_fp2(c2_c, c3_c)
A_c2 = get_fp2(c4_c, c5_c)

B_c0 = get_fp2(c6_c, c7_c)
B_c1 = get_fp2(c8_c, c9_c)
B_c2 = get_fp2(c10_c, c11_c)

# Fp6 Squaring logic (A^2)
# t0 = A.c0^2
# t1 = A.c1^2
# t2 = A.c2^2
# c0 = t0 + (A.c1 * A.c2 * 2 * xi)
# c1 = t2 * xi + 2 * A.c0 * A.c1
# c2 = t1 + 2 * A.c0 * A.c2

# Helper: Fp2 mul
def fp2_mul(a, b):
    return a * b

# Helper: Fp2 mul xi (xi = 9+u = FQ2([9, 1]))
xi = FQ2([9, 1])
def fp2_mul_xi(a):
    return a * xi

# t0 (C t0) corresponds to A * A (XZ where X=Z=A)
# Compute A * A in Fp6
def fp6_sqr(c0, c1, c2):
    s0 = c0 * c0
    s1 = c1 * c1
    s2 = c2 * c2
    
    t0 = c1 * c2
    t0 = t0 + t0
    t0 = t0 * xi
    res_c0 = s0 + t0
    
    t0 = c0 * c1
    t0 = t0 + t0
    t1 = s2 * xi
    res_c1 = t0 + t1
    
    t0 = c0 * c2
    t0 = t0 + t0
    res_c2 = s1 + t0
    return res_c0, res_c1, res_c2

# Calculate t0 (A^2)
t0_c0, t0_c1, t0_c2 = fp6_sqr(A_c0, A_c1, A_c2)

print(f"t0 (A^2) c0 real: {int(t0_c0.coeffs[0]):x}")

# C t0 output (Line 86 in v9): 140ad1bae1c24857a458ef5c2f498e16d857c98160377bd84cb999c378842a57
targ_t0 = 0x140ad1bae1c24857a458ef5c2f498e16d857c98160377bd84cb999c378842a57
print(f"Targ: {targ_t0:x}")
print(f"Match: {int(t0_c0.coeffs[0]) == targ_t0}")
