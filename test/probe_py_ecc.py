from py_ecc.fields import bn128_FQ12, bn128_FQ2

print("Probing FQ12 structure...")
one = bn128_FQ12.one()
c0 = one.coeffs[0]
print(f"Type of c0: {type(c0)}")
FQ6 = type(c0)

try:
    print("Trying FQ6([c0, c1, c2])...")
    z = bn128_FQ2.zero()
    t = FQ6([z, z, z])
    print("Success list")
except Exception as e:
    print(f"Fail list: {e}")

try:
    print("Trying FQ6(coeffs=[c0, c1, c2])...")
    t = FQ6(coeffs=[z, z, z])
    print("Success kwarg")
except Exception as e:
    print(f"Fail kwarg: {e}")

try:
    print("Trying FQ6((c0, c1, c2))...")
    t = FQ6((z, z, z))
    print("Success tuple")
except Exception as e:
    print(f"Fail tuple: {e}")


