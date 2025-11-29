from py_ecc.bn128 import bn128_pairing

c = bn128_pairing.ate_loop_count
print(f"Count: {c}")
print(f"Hex: {hex(c)}")
print(f"Bit length: {c.bit_length()}")


