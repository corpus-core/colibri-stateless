from py_ecc.bn128 import bn128_curve, FQ, FQ2, field_modulus

# Twist curve parameter b2
print(f"b2 in py_ecc: {bn128_curve.b2}")

# Calculate 3/(9+u) manually
xi = FQ2([9, 1])
three = FQ2([3, 0])
b_calc = three / xi

print(f"Calculated 3/xi: {b_calc}")
print(f"Real: {hex(b_calc.coeffs[0].n)}")
print(f"Imag: {hex(b_calc.coeffs[1].n)}")
