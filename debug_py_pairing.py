from py_ecc.bn128 import G1, G2, pairing, FQ, FQ2, curve_order, field_modulus
from py_ecc.bn128.bn128_pairing import miller_loop, final_exponentiate as final_exponentiation
from py_ecc.fields import optimized_bn128_FQ12 as FQ12
import sys
sys.setrecursionlimit(50000)

# P = (1, 2)
P = (FQ(1), FQ(2))

# Q from hex
Q_hex = "198e9393920d483a7260bfb731fb5d25f1aa493335a9e71297e485b7aef312c2" + \
        "1800deef121f1e76426a00665e5c4479674322d4f75edadd46debd5cd992f6ed" + \
        "090689d0585ff075ec9e99ad690c3395bc4b313370b38ef355acdadcd122975b" + \
        "12c85ea5db8c6deb4aab71808dcb408fe3d1e7690c43d37b4ce6cc0166fa7daa"

x_im = int(Q_hex[0:64], 16)
x_re = int(Q_hex[64:128], 16)
y_im = int(Q_hex[128:192], 16)
y_re = int(Q_hex[192:256], 16)

# FQ2([real, imag]) -> real + imag * i
Q_x = FQ2([x_re, x_im])
Q_y = FQ2([y_re, y_im])
Q = (Q_x, Q_y)

print(f"P: {P}")
print(f"Q: {Q}")

# Miller Loop: miller_loop(Q, P)
print("Running Py Miller Loop...")
try:
    ml_res = miller_loop(Q, P)
    print("Py Miller Loop Result:")
    
    # Unpack
    c0 = ml_res.coeffs[0]
    c1 = ml_res.coeffs[1]
    c0_coeffs = c0.coeffs
    c1_coeffs = c1.coeffs
    flat_coeffs = []
    for x in c0_coeffs:
        flat_coeffs.append(x.coeffs[0])
        flat_coeffs.append(x.coeffs[1])
    for x in c1_coeffs:
        flat_coeffs.append(x.coeffs[0])
        flat_coeffs.append(x.coeffs[1])

    for i, v in enumerate(flat_coeffs):
        print(f"{hex(int(v))}")

    # Final Exp
    print("Running Py Final Exp...")
    fe_res = final_exponentiation(ml_res)
    print("Py Final Exp Result:")
    
    c0 = fe_res.coeffs[0]
    c1 = fe_res.coeffs[1]
    c0_coeffs = c0.coeffs
    c1_coeffs = c1.coeffs
    flat_coeffs = []
    for x in c0_coeffs:
        flat_coeffs.append(x.coeffs[0])
        flat_coeffs.append(x.coeffs[1])
    for x in c1_coeffs:
        flat_coeffs.append(x.coeffs[0])
        flat_coeffs.append(x.coeffs[1])

    for i, v in enumerate(flat_coeffs):
        print(f"{hex(int(v))}")

except RecursionError as e:
    print(f"RecursionError: {e}")
except Exception as e:
    print(f"Error: {e}")
