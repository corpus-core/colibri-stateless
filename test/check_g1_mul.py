from py_ecc.bn128 import FQ, bn128_curve, field_modulus

P_hex_x = "0x1a78f6839bb5d88d1674dc0bb7231aef8ad32aded0418bcd3c0e8186365a448f"
P_hex_y = "0x1d1160f7903c238c2ac8a24938448337620e9e20c1511edb61e14766a8aa3569"

P = (FQ(int(P_hex_x, 16)), FQ(int(P_hex_y, 16)))
s = 12345

R = bn128_curve.multiply(P, s)

print(f"R.x: {R[0].n:064x}")
print(f"R.y: {R[1].n:064x}")


