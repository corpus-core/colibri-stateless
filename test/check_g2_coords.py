from py_ecc.bn128 import G2

# G2 is (X, Y, Z)
# X, Y, Z are FQ2 objects.
x_fq2 = G2[0]
y_fq2 = G2[1]

x_re = x_fq2.coeffs[0].n
x_im = x_fq2.coeffs[1].n
y_re = y_fq2.coeffs[0].n
y_im = y_fq2.coeffs[1].n

print(f"Py G2 X Re: {x_re:064x}")
print(f"Py G2 X Im: {x_im:064x}")
print(f"Py G2 Y Re: {y_re:064x}")
print(f"Py G2 Y Im: {y_im:064x}")

# C strings from debug_bn254_manual.c
c_x_im = "198e9393920d483a7260bfb731fb5d25f1aa493335a9e71297e485b7aef312c2"
c_x_re = "1800deef121f1e76426a00665e5c4479674322d4f75edadd46debd5cd992f6ed"
c_y_im = "090689d0585ff075ec9e99ad690c3395bc4b313370b38ef355acdadcd122975b"
c_y_re = "12c85ea5db8c6deb4aab71808dcb408fe3d1e7690c43d37b4ce6cc0166fa7daa"

print("-" * 20)
print(f"C  G2 X Re: {c_x_re}")
print(f"C  G2 X Im: {c_x_im}")
print(f"C  G2 Y Re: {c_y_re}")
print(f"C  G2 Y Im: {c_y_im}")

print("-" * 20)
if f"{x_re:064x}" == c_x_re and f"{x_im:064x}" == c_x_im:
    print("X MATCH")
else:
    print("X MISMATCH")

if f"{y_re:064x}" == c_y_re and f"{y_im:064x}" == c_y_im:
    print("Y MATCH")
else:
    print("Y MISMATCH")
