from py_ecc.bn128 import bn128_curve, FQ, FQ2, field_modulus

xi = FQ2([9, 1])
three = FQ2([3, 0])
b_calc = three / xi

im = b_calc.coeffs[1].n
h = hex(im)[2:]
print(f"Imag Hex: {h}")
print(f"Length (chars): {len(h)}")
print(f"Length (bytes): {len(h)/2}")


