from py_ecc.optimized_bn128 import G1, G2, curve_order, field_modulus, b2
from py_ecc.optimized_bn128 import optimized_pairing
import sys

def print_fp(label, val):
    print(f"{label}: {val:064x}")

def print_fp2(label, val):
    c0 = val.coeffs[0]
    c1 = val.coeffs[1]
    print(f"{label} c0: {int(c0):064x}")
    print(f"{label} c1: {int(c1):064x}")

def print_fp12(label, val):
    print(f"{label}:")
    coeffs = val.coeffs
    print(f"  Length: {len(coeffs)}")
    for i, c in enumerate(coeffs):
        if hasattr(c, 'coeffs'):
            print_fp2(f"  c{i}", c)
        else:
            # c is int or FQ
            print(f"  c{i}: {int(c):064x}")

print(f"Field Modulus: {field_modulus:x}")

# G2 is (x, y, z) in Jacobian?
# optimized_bn128 G2 is (x, y, z) Jacobian FQ2.
Q = G2
P = G1

print("Calling pairing(G2, G1)...")
try:
    res = optimized_pairing.pairing(G2, G1)
    print("Pairing(G2, G1) successful")
    print_fp12("Pairing Result", res)
except Exception as e:
    print(f"Pairing(G2, G1) failed: {e}")

print("Calling pairing(G1, G2)...")
try:
    res = optimized_pairing.pairing(G1, G2)
    print("Pairing(G1, G2) successful")
except Exception as e:
    print(f"Pairing(G1, G2) failed: {e}")

print("Calling linefunc(Q, Q, P_int)...")
P_int = (int(P[0]), int(P[1]), int(P[2]))
ret = optimized_pairing.linefunc(Q, Q, P_int)
print(f"Return type: {type(ret)}")
print(f"Return len: {len(ret)}")

out_point = ret[0]
line_eval = ret[1]

print(f"out_point type: {type(out_point)}")
print(f"line_eval type: {type(line_eval)}")

if isinstance(line_eval, (list, tuple)):
    print(f"line_eval len: {len(line_eval)}")
    for i, x in enumerate(line_eval):
        print(f"line_eval[{i}] type: {type(x)}")
        if hasattr(x, 'coeffs'):
             print_fp2(f"line_eval[{i}]", x)

# print_fp12("Line Eval (DBL)", line_eval)

# Also verify Q_out matches my manual DBL
X_out = out_point[0]
Y_out = out_point[1]
Z_out = out_point[2]

# Convert to Affine
Z2 = Z_out * Z_out
Z3 = Z2 * Z_out
X_aff = X_out / Z2
Y_aff = Y_out / Z3

print_fp2("Q_out.x (Affine)", X_aff)
print_fp2("Q_out.y (Affine)", Y_aff)
