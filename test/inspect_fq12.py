from py_ecc.bn128 import FQ12

x = FQ12.one()
print(f"Type of coeffs: {type(x.coeffs)}")
print(f"Length of coeffs: {len(x.coeffs)}")
print(f"Type of element: {type(x.coeffs[0])}")


