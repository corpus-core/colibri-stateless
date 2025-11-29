from py_ecc.bn128 import FQ, FQ2

a = FQ(2)
b = FQ2([3, 4])

try:
    print(f"FQ * FQ2: {a * b}")
except Exception as e:
    print(f"FQ * FQ2 failed: {e}")

try:
    print(f"FQ2 * FQ: {b * a}")
except Exception as e:
    print(f"FQ2 * FQ failed: {e}")
