from py_ecc.bn128 import field_modulus

lb_c = 0x1352ed7b50f57c9d8d87a36d493cd5263572cc70a720abb1da3f44b02fd2c7de
Py_hex = "0x13f9252977689864716d1d73543572c514698739070172516788533846082705"
Py = int(Py_hex, 16)

print(f"C l_b: {lb_c:x}")
print(f"Py:    {Py:x}")

print(f"Py == lb_c? {Py == lb_c}")
print(f"-Py == lb_c? {field_modulus - Py == lb_c}")
print(f"-lb_c == Py? {field_modulus - lb_c == Py}")

# Maybe it is Py itself?
# 1352ed... != 13f925...

# Is P->y being read correctly?
# Maybe Endianness issue?
# 13f925... vs 1352ed...
# They look somewhat similar in start? 13...
# But distinct.
