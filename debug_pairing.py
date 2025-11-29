from py_ecc.bn128 import G1, G2, pairing, multiply, add, curve_order, field_modulus, neg
from py_ecc.bn128 import bn128_curve, bn128_pairing
from py_ecc.fields import bn128_FQ as FQ, bn128_FQ2 as FQ2, bn128_FQ12 as FQ12
import sys

# Parameters
# P = (1, 2)
P_raw = (FQ(1), FQ(2))
# Embedding P into G2 (FQ2) just for type compatibility if needed
P = (FQ2([P_raw[0].n, 0]), FQ2([P_raw[1].n, 0]))

# -P
negP_raw = neg(P_raw)
negP = (FQ2([negP_raw[0].n, 0]), FQ2([negP_raw[1].n, 0]))


# Q given in hex in test_precompile_ecpairing_valid
# x_im: 198e9393920d483a7260bfb731fb5d25f1aa493335a9e71297e485b7aef312c2
# x_re: 1800deef121f1e76426a00665e5c4479674322d4f75edadd46debd5cd992f6ed
# y_im: 090689d0585ff075ec9e99ad690c3395bc4b313370b38ef355acdadcd122975b
# y_re: 12c85ea5db8c6deb4aab71808dcb408fe3d1e7690c43d37b4ce6cc0166fa7daa

Q_x_im = 0x198e9393920d483a7260bfb731fb5d25f1aa493335a9e71297e485b7aef312c2
Q_x_re = 0x1800deef121f1e76426a00665e5c4479674322d4f75edadd46debd5cd992f6ed
Q_y_im = 0x090689d0585ff075ec9e99ad690c3395bc4b313370b38ef355acdadcd122975b
Q_y_re = 0x12c85ea5db8c6deb4aab71808dcb408fe3d1e7690c43d37b4ce6cc0166fa7daa

# py_ecc uses (x, y) where x, y are FQ2
# FQ2(coeffs) -> coeffs[0] + coeffs[1] * i
# C implementation uses Re + Im * i? Or Im + Re * i?
# Precompile input format:
# "The coordinates of the point Q are (x, y) where x = x_re + x_im * i and y = y_re + y_im * i"
# Wait, EVM spec usually says:
# G2 points are encoded as (x_im, x_re, y_im, y_re).
# Let's check precompiles_ec_pairing.c usage.
# It calls bn254_g2_from_bytes_eth.
# bn254_g2_from_bytes_eth reads 128 bytes.
# It calls bn254_g2_from_bytes_raw which expects Re, Im?
# Let's check bn254.c

# ...
# So let's assume standard EVM encoding:
# x = x_re + x_im * i
# But encoded as: x_im, x_re, y_im, y_re.
# So Q_x = Q_x_re + Q_x_im * i
# Q_y = Q_y_re + Q_y_im * i

# However, py_ecc FQ2 elements are represented as (c0, c1) where element = c0 + c1 * X.
# X is the element such that X^2 = -1? No, X^2 + 1 = 0 in bn128?
# In bn128_curve.py: FQ2.modulus_coeffs = [1, 0] which implies X^2 + 1 = 0.
# So element is c0 + c1 * i.

# So Q should be:
# Q = (FQ2([Q_x_re, Q_x_im]), FQ2([Q_y_re, Q_y_im]))
# BUT! py_ecc might use different order or basis.
# Let's check if this point is on curve.

Q_val = (
    FQ2([Q_x_re, Q_x_im]),
    FQ2([Q_y_re, Q_y_im])
)

# Verify on curve
# y^2 = x^3 + 3 / (9 + u)
# Wait, twist equation is y^2 = x^3 + 3/(9+u)?
# bn128_curve.py says:
# b2 = FQ2([3, 0]) / FQ2([9, 1])
# curve_order = ...

b2 = bn128_curve.b2
lhs = Q_val[1] ** 2
rhs = Q_val[0] ** 3 + b2
if lhs == rhs:
    print("Q is on curve (py_ecc)")
else:
    print("Q is NOT on curve (py_ecc)")
    print(f"LHS: {lhs}")
    print(f"RHS: {rhs}")
    # Try swapping Re/Im
    Q_val_swap = (
        FQ2([Q_x_im, Q_x_re]),
        FQ2([Q_y_im, Q_y_re])
    )
    lhs = Q_val_swap[1] ** 2
    rhs = Q_val_swap[0] ** 3 + b2
    if lhs == rhs:
        print("Q (swapped) is on curve")
        Q_val = Q_val_swap

# Miller Loop Trace
# bn128_pairing.miller_loop(Q, P) 
# Note: py_ecc pairing is pairing(Q, P). 
# But wait, Ethereum uses pairing(P, Q) where P in G1, Q in G2?
# EIP-197: "input is a sequence of (P, Q) pairs"
# P in G1, Q in G2.
# Optimal Ate Pairing: e(P, Q).
# py_ecc pairing(Q, P) takes G2, G1.
# So we call bn128_pairing.miller_loop(Q, P).

print("Computing Miller Loop...")
f = bn128_pairing.miller_loop(Q_val, P)
print(f"Miller Loop Result (Fp12): {f}")
# Print Fp12 coeffs
coeffs = f.coeffs
# Fp12 coeffs are Fp6 coeffs, which are Fp2 coeffs.
# Structure:
# f = c0 + c1 * w
# c0 = c00 + c01 * v + c02 * v^2
# c1 = c10 + c11 * v + c12 * v^2
# ...
# We need to map this to C struct.

# C struct: c0 (Fp6), c1 (Fp6).
# Fp6: c0, c1, c2 (Fp2).
# Fp2: c0, c1 (Fp).

# Let's dump all 12 Fp coeffs.
def print_fp12(val, name="f"):
    c = val.coeffs
    print(f"{name} raw coeffs: {c}")

print_fp12(f, "f_miller")

print("Computing Final Exponentiation...")
result = bn128_pairing.final_exponentiation(f)
print_fp12(result, "f_final")

if result == FQ12.one():
    print("Pairing result is 1")
else:
    print("Pairing result is NOT 1")

# Also compute e(P, Q) * e(-P, Q)
f_neg = bn128_pairing.miller_loop(Q_val, negP)
res_neg = bn128_pairing.final_exponentiation(f_neg)
prod = result * res_neg
print_fp12(prod, "product")
if prod == FQ12.one():
    print("Product is 1")
else:
    print("Product is NOT 1")
