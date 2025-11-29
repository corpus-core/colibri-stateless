from py_ecc.bn128 import FQ12, FQ, field_modulus

def to_fq(hex_str):
    if hex_str is None: return FQ(0)
    return FQ(int(hex_str, 16))

# C Loop 63 values
# L_DBL
# l_a (c1.c1)
la_dbl_c0 = to_fq("23e436d04ab88db6ee05346199d632f2a969e7ea4754f8c072c2938c8b863098")
la_dbl_c1 = to_fq("1afa89682363645e1569493e6245e6f40906e5f810ec1d456ef0c0638ccea6dc")
# l_c (c1.c0)
lc_dbl_c0 = to_fq("04ea72552dab2205a779a55c26ebdb421593f4fb597774f3cd03b6085e9218e9")
lc_dbl_c1 = to_fq("249e9d5a2ece2cda34c794f4a7e573f94882126052729b2be5f34eebdfb3923e")
# l_b (c0.c0)
lb_dbl_c0 = to_fq("1352ed7b50f57c9d8d87a36d493cd5263572cc70a720abb1da3f44b02fd2c7de")

# Construct l_dbl FQ12
coeffs_dbl = [FQ(0)] * 12
# w^0: c0 - 9*c1. Here l_b.c1 is 0.
coeffs_dbl[0] = lb_dbl_c0
# w^3: la.c0 - 9*la.c1
coeffs_dbl[3] = la_dbl_c0 - 9 * la_dbl_c1
# w^9: la.c1
coeffs_dbl[9] = la_dbl_c1
# w^1: lc.c0 - 9*lc.c1
coeffs_dbl[1] = lc_dbl_c0 - 9 * lc_dbl_c1
# w^7: lc.c1
coeffs_dbl[7] = lc_dbl_c1

l_dbl = FQ12(coeffs_dbl)


# L_ADD
# l_a (c1.c1)
la_add_c0 = to_fq("0715642eb535db9e820f390d2da685720730773e481d205ff3cd93cd5f433c0b")
la_add_c1 = to_fq("2e818f5ceceb5be92df49353afe8c0c90d1f21912cacd51587e44bd28e2123c7")
# l_c (c1.c0)
lc_add_c0 = to_fq("2936fe82335532a27c183102bcbbbe971aa81b2901af1a3713357316a7914ebf")
lc_add_c1 = to_fq("1be82408b32d93f1a623fcb410499b7dc9ea425150b4d6de0d7d7528c37dc5b6")
# l_b (c0.c0)
lb_add_c0 = to_fq("1352ed7b50f57c9d8d87a36d493cd5263572cc70a720abb1da3f44b02fd2c7de")

# Construct l_add FQ12
coeffs_add = [FQ(0)] * 12
coeffs_add[0] = lb_add_c0
coeffs_add[3] = la_add_c0 - 9 * la_add_c1
coeffs_add[9] = la_add_c1
coeffs_add[1] = lc_add_c0 - 9 * lc_add_c1
coeffs_add[7] = lc_add_c1

l_add = FQ12(coeffs_add)

# Multiply
res = l_dbl * l_add

print(f"res c0: {int(res.coeffs[0]):x}")
print(f"res c6: {int(res.coeffs[6]):x}")

# Mapping back to C c0 (Fp2) components
# C c0.c0 = res.c0 + 9*res.c6
# C c0.c1 = res.c6

c0_c0_val = res.coeffs[0] + 9 * res.coeffs[6]
print(f"C c0.c0 (reconstructed): {int(c0_c0_val):x}")
print(f"C c0.c1 (reconstructed): {int(res.coeffs[6]):x}")

# Target C result
print("Target C c0.c0: 15e6972b12358521e0d54682cd273798ce4b90c59a5b8c8b0697a05d7cc96aee")
