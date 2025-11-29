from py_ecc.optimized_bn128 import FQ, FQ2

print("Probing FQ - FQ2")
x = FQ(1)
y = FQ2([1, 1])
try:
    z = x - y
    print(f"x - y = {z}")
except Exception as e:
    print(f"x - y failed: {e}")

try:
    z = y - x
    print(f"y - x = {z}")
except Exception as e:
    print(f"y - x failed: {e}")


