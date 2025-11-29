from py_ecc.fields import bn128_FQ12, bn128_FQ2

one = bn128_FQ12.one()
print(f"len(one.coeffs): {len(one.coeffs)}")
print(f"type(one.coeffs[0]): {type(one.coeffs[0])}")
print(f"one.coeffs[0]: {one.coeffs[0]}")


