from py_ecc.bn128 import FQ, bn128_curve, field_modulus

# From C output
# IC points (G1) in hex [x, y]
IC0_bytes = "26091e1cafb0ad8a4ea0a694cd3743ebf524779233db734c451d28b58aa9758e009ff50a6b8b11c3ca6fdb2690a124f8ce25489fefa65a3e782e7ba70b66690e"
IC1_bytes = "061c3fd0fd3da25d2607c227d090cca750ed36c6ec878755e537c1c48951fb4c0fa17ae9c2033379df7b5c65eff0e107055e9a273e6119a212dd09eb51707219"
IC2_bytes = "04eab241388a79817fe0e0e2ead0b2ec4ffdec51a16028dee020634fd129e71c07236256d21c60d02f0bdbf95cff83e03ea9e16fca56b18d5544b0889a65c1f5"

def parse_g1(hex_str):
    x = int(hex_str[:64], 16)
    y = int(hex_str[64:], 16)
    return (FQ(x), FQ(y))

IC0 = parse_g1(IC0_bytes)
IC1 = parse_g1(IC1_bytes)
IC2 = parse_g1(IC2_bytes)

# Scalars
vkey_fr_hex = "00a61ad8347fe889261a355403eaef5795d3d6adf039126d55da3fe9aa9f2a54"
pub_hash_hex = "1ddc001001f1d8cdecd432c67ab65e899ed1b97a5753f3b2116699f736161ffb"

vkey_fr = int(vkey_fr_hex, 16)
pub_hash = int(pub_hash_hex, 16)

# C result L
L_c_bytes = "089f81c788b51670ac0b2c9124928d8d3bf27090e8286e6fc33e590ad0bea7f122854128324f79c69feee580970d1634b05341d69e5240f6867909886c032826"
L_c = parse_g1(L_c_bytes)

print(f"IC0: {IC0}")
print(f"IC1: {IC1}")
print(f"IC2: {IC2}")
print(f"vkey_fr: {vkey_fr:x}")
print(f"pub_hash: {pub_hash:x}")

# L = IC0 + IC1*vkey_fr + IC2*pub_hash
# bn128_curve.multiply(P, s)
# bn128_curve.add(P1, P2)

T1 = bn128_curve.multiply(IC1, vkey_fr)
T2 = bn128_curve.multiply(IC2, pub_hash)

L_py = bn128_curve.add(IC0, T1)
L_py = bn128_curve.add(L_py, T2)

print(f"L (Python): {L_py}")
print(f"L (C):      {L_c}")

if L_py == L_c:
    print("SUCCESS: L computation matches.")
else:
    print("FAILURE: L computation MISMATCH.")


