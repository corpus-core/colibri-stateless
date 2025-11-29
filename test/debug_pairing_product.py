from py_ecc.bn128 import *
import sys
sys.setrecursionlimit(100000)
from py_ecc.fields import bn128_FQ as FQ, bn128_FQ2 as FQ2, bn128_FQ12 as FQ12

# Helper to parse G1 (Big Endian bytes)
def parse_g1(hex_str):
    b = bytes.fromhex(hex_str)
    if len(b) != 64:
        raise ValueError("Invalid G1 length")
    x = int.from_bytes(b[:32], 'big')
    y = int.from_bytes(b[32:], 'big')
    return (FQ(x), FQ(y))

# Helper to parse G2 (ETH format: X_im, X_re, Y_im, Y_re)
def parse_g2(hex_str):
    b = bytes.fromhex(hex_str)
    if len(b) != 128:
        raise ValueError("Invalid G2 length")
    x_im = int.from_bytes(b[:32], 'big')
    x_re = int.from_bytes(b[32:64], 'big')
    y_im = int.from_bytes(b[64:96], 'big')
    y_re = int.from_bytes(b[96:], 'big')
    
    # py_ecc FQ2(coeffs) -> coeffs[0] + coeffs[1]*i
    # FQ2([re, im])
    return (FQ2([x_re, x_im]), FQ2([y_re, y_im]))

# Logged Values (Period 1600)
A_hex = "1a78f6839bb5d88d1674dc0bb7231aef8ad32aded0418bcd3c0e8186365a448f1d1160f7903c238c2ac8a24938448337620e9e20c1511edb61e14766a8aa3569"
B_hex = "0c2a02ff3f98b4c1c406228fdf45158795144f978887dd1133680719851e5eda30363c09c5fcc01c3f5d300534b47a7a4f0390bd7e1a8cbec942f81c5ae9af5d2fcee692ab4e54d1f8157320419655b49a0ed035ae613fb66983486379a4b7c81dc3f46b40c711f5c6a74083b54ac7a6ad6b469342b4080de759da9b83e78d72"
C_hex = "223d4000ce77907532337b2cf2f06c16acd4bfae8690c6b8cbfde4d40a3c2715019eba6ef839006a33148941acac52e2e13b2220a3e698f1771868282699cc34"

# VK Points
alpha_hex = "2d4d9aa7e302d9df41749d5507949d05dbea33fbb16c643b22f599a2be6df2e214bedd503c37ceb061d8ec60209fe345ce89830a19230301f076caff004d1926"
beta_neg_hex = "0967032fcbf776d1afc985f88877f182d38480a653f2decaa9794cbc3bf3060c0e187847ad4c798374d0d6732bf501847dd68bc0e071241e0213bc7fc13db7ab001752a100a72fdf1e5a5d6ea841cc20ec838bccfcf7bd559e79f1c9c759b6a0192a8cc13cd9f762871f21e43451c6ca9eeab2cb2987c4e366a185c25dac2e7f"
gamma_neg_hex = "198e9393920d483a7260bfb731fb5d25f1aa493335a9e71297e485b7aef312c21800deef121f1e76426a00665e5c4479674322d4f75edadd46debd5cd992f6ed275dc4a288d1afb3cbb1ac09187524c7db36395df7be3b99e673b13a075a65ec1d9befcd05a5323e6da4d435f3b617cdb3af83285c2df711ef39c01571827f9d"
delta_neg_hex = "1cc7cb8de715675f21f01ecc9b46d236e0865e0cc020024521998269845f74e603ff41f4ba0c37fe2caf27354d28e4b8f83d3b76777a63b327d736bffb0122ed01909cd7827e0278e6b60843a4abc7b111d7f8b2725cd5902a6b20da7a2938fb192bd3274441670227b4f69a44005b8711266e474227c6439ca25ca8e1ec1fc2"

# L Point (Calculated)
L_hex = "089f81c788b51670ac0b2c9124928d8d3bf27090e8286e6fc33e590ad0bea7f122854128324f79c69feee580970d1634b05341d69e5240f6867909886c032826"

print("Parsing points...")
A = parse_g1(A_hex)
B = parse_g2(B_hex)
C = parse_g1(C_hex)
alpha = parse_g1(alpha_hex)
beta_neg = parse_g2(beta_neg_hex)
gamma_neg = parse_g2(gamma_neg_hex)
delta_neg = parse_g2(delta_neg_hex)
L = parse_g1(L_hex)

print("Computing pairings...")
e1 = pairing(B, A)
e2 = pairing(delta_neg, C)
e3 = pairing(beta_neg, alpha)
e4 = pairing(gamma_neg, L)

# Result = e1 * e2 * e3 * e4
print("Multiplying results...")
res = e1 * e2 * e3 * e4

print(f"Result: {res}")

if res == FQ12.one():
    print("SUCCESS: Pairing product is 1")
else:
    print("FAILURE: Pairing product is NOT 1")

