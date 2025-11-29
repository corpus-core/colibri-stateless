from py_ecc.optimized_bn128 import FQ, FQ2

print("Probing FQ2 - int")
y = FQ2([1, 1])
try:
    z = y - 1
    print(f"y - 1 = {z}")
except Exception as e:
    print(f"y - 1 failed: {e}")

print("Probing FQ2 - FQ.n")
x = FQ(1)
try:
    z = y - x.n
    print(f"y - x.n = {z}")
except Exception as e:
    print(f"y - x.n failed: {e}")


