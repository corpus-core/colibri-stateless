from py_ecc.bn128 import field_modulus

inv82 = pow(82, field_modulus - 2, field_modulus)
re = (27 * inv82) % field_modulus
im_neg = (-3 * inv82) % field_modulus
im_pos = (3 * inv82) % field_modulus
print(f"RE: {hex(re)}")
print(f"IM (neg): {hex(im_neg)}")
print(f"IM (pos): {hex(im_pos)}")
print(f"P hex: {hex(field_modulus)}")
print(f"Len IM neg: {len(hex(im_neg)) - 2}")
print(f"Len P: {len(hex(field_modulus)) - 2}")

