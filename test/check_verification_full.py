from py_ecc.bn128 import FQ, FQ2, FQ12, bn128_pairing, bn128_curve, field_modulus
import sys

sys.setrecursionlimit(100000)

def parse_g1(hex_str):
    x = int(hex_str[:64], 16)
    y = int(hex_str[64:], 16)
    return (FQ(x), FQ(y))

def parse_g2(hex_str):
    # Eth format: X_im, X_re, Y_im, Y_re
    x_c1 = int(hex_str[:64], 16)
    x_c0 = int(hex_str[64:128], 16)
    y_c1 = int(hex_str[128:192], 16)
    y_c0 = int(hex_str[192:], 16)
    return (FQ2([x_c0, x_c1]), FQ2([y_c0, y_c1]))

def cast_g1_to_fq12(pt):
    # Pt is (x, y) FQ
    return (FQ12([pt[0].n] + [0]*11), FQ12([pt[1].n] + [0]*11))

def miller_loop_custom(Q, P):
    # Q is G2 (coeffs in FQ2), P is G1 (coeffs in FQ)
    # Twist Q into FQ12
    Q_twisted = bn128_pairing.twist(Q) 
    # Cast P to FQ12
    P_fq12 = cast_g1_to_fq12(P)
    
    T = Q_twisted
    f = FQ12.one()
    
    for i in range(63, -1, -1):
        l_dbl = bn128_pairing.linefunc(T, T, P_fq12)
        f = f * f * l_dbl
        T = bn128_pairing.double(T)
        
        if bn128_pairing.ate_loop_count & (2**i):
            l_add = bn128_pairing.linefunc(T, Q_twisted, P_fq12)
            f = f * l_add
            T = bn128_pairing.add(T, Q_twisted)
            
    # Endomorphism steps
    # Q1, Q2
    xi = FQ2([9, 1])
    xi_p_3 = xi ** ((field_modulus - 1) // 3)
    xi_p_2 = xi ** ((field_modulus - 1) // 2)
    
    def conjugate(val):
        return FQ2([val.coeffs[0], -val.coeffs[1]])
        
    q1_x = conjugate(Q[0]) * xi_p_3
    q1_y = conjugate(Q[1]) * xi_p_2
    Q1 = (q1_x, q1_y)
    
    q2_x = conjugate(q1_x) * xi_p_3
    q2_y = conjugate(q1_y) * xi_p_2
    q2_y = -q2_y
    Q2 = (q2_x, q2_y)
    
    Q1_twisted = bn128_pairing.twist(Q1)
    Q2_twisted = bn128_pairing.twist(Q2)
    
    l_end1 = bn128_pairing.linefunc(T, Q1_twisted, P_fq12)
    f = f * l_end1
    T = bn128_pairing.add(T, Q1_twisted)
    
    l_end2 = bn128_pairing.linefunc(T, Q2_twisted, P_fq12)
    f = f * l_end2
    T = bn128_pairing.add(T, Q2_twisted)
    
    return f

# Proof Points
A_hex = "1a78f6839bb5d88d1674dc0bb7231aef8ad32aded0418bcd3c0e8186365a448f1d1160f7903c238c2ac8a24938448337620e9e20c1511edb61e14766a8aa3569"
B_hex = "0c2a02ff3f98b4c1c406228fdf45158795144f978887dd1133680719851e5eda30363c09c5fcc01c3f5d300534b47a7a4f0390bd7e1a8cbec942f81c5ae9af5d2fcee692ab4e54d1f8157320419655b49a0ed035ae613fb66983486379a4b7c81dc3f46b40c711f5c6a74083b54ac7a6ad6b469342b4080de759da9b83e78d72"
C_hex = "223d4000ce77907532337b2cf2f06c16acd4bfae8690c6b8cbfde4d40a3c2715019eba6ef839006a33148941acac52e2e13b2220a3e698f1771868282699cc34"

A = parse_g1(A_hex)
B = parse_g2(B_hex)
C = parse_g1(C_hex)

# VK Points
alpha_hex = "2d4d9aa7e302d9df41749d5507949d05dbea33fbb16c643b22f599a2be6df2e214bedd503c37ceb061d8ec60209fe345ce89830a19230301f076caff004d1926"
beta_neg_hex = "0967032fcbf776d1afc985f88877f182d38480a653f2decaa9794cbc3bf3060c0e187847ad4c798374d0d6732bf501847dd68bc0e071241e0213bc7fc13db7ab001752a100a72fdf1e5a5d6ea841cc20ec838bccfcf7bd559e79f1c9c759b6a0192a8cc13cd9f762871f21e43451c6ca9eeab2cb2987c4e366a185c25dac2e7f"
gamma_neg_hex = "198e9393920d483a7260bfb731fb5d25f1aa493335a9e71297e485b7aef312c21800deef121f1e76426a00665e5c4479674322d4f75edadd46debd5cd992f6ed275dc4a288d1afb3cbb1ac09187524c7db36395df7be3b99e673b13a075a65ec1d9befcd05a5323e6da4d435f3b617cdb3af83285c2df711ef39c01571827f9d"
delta_neg_hex = "1cc7cb8de715675f21f01ecc9b46d236e0865e0cc020024521998269845f74e603ff41f4ba0c37fe2caf27354d28e4b8f83d3b76777a63b327d736bffb0122ed01909cd7827e0278e6b60843a4abc7b111d7f8b2725cd5902a6b20da7a2938fb192bd3274441670227b4f69a44005b8711266e474227c6439ca25ca8e1ec1fc2"

alpha = parse_g1(alpha_hex)
beta_neg = parse_g2(beta_neg_hex)
gamma_neg = parse_g2(gamma_neg_hex)
delta_neg = parse_g2(delta_neg_hex)

# L
L_c_bytes = "089f81c788b51670ac0b2c9124928d8d3bf27090e8286e6fc33e590ad0bea7f122854128324f79c69feee580970d1634b05341d69e5240f6867909886c032826"
L = parse_g1(L_c_bytes)

print("Computing Miller Loops...")
# e(A, B)
ml1 = miller_loop_custom(B, A)

# e(C, delta_neg)
ml2 = miller_loop_custom(delta_neg, C)

# e(alpha, beta_neg)
ml3 = miller_loop_custom(beta_neg, alpha)

# e(L, gamma_neg)
ml4 = miller_loop_custom(gamma_neg, L)

print("Accumulating...")
prod = ml1 * ml2 * ml3 * ml4

print("Final Exponentiation (Manual)...")
# Easy part
# f1 = f.conj() * f.inv()
f_inv = prod.inv()
f_conj = FQ12([x if i not in [1,3,5,7,9,11] else -x for i,x in enumerate(prod.coeffs)])
f1 = f_conj * f_inv
f_easy = f1 * (f1 ** (field_modulus**2))

# Hard part
u = 4965661367192848881
b = f_easy ** u
a = b ** u
a2 = a ** u
b_conj = FQ12([x if i not in [1,3,5,7,9,11] else -x for i,x in enumerate(b.coeffs)])
b = b_conj * a
a2 = a2 * a
a = f_easy ** (field_modulus**2)
a = a * a2
a2 = b ** field_modulus
a = a * a2
b = b ** field_modulus
b = b ** field_modulus
b = b ** field_modulus
y = a * b

if y == FQ12.one():
    print("SUCCESS: Pairing check passed.")
else:
    print("FAILURE: Pairing check failed.")
    print(f"Result: {y}")
