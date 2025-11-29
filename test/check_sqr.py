from py_ecc.bn128 import FQ2, field_modulus

# A (Real part of X)
A_int = 0x30363c09c5fcc01c3f5d300534b47a7a4f0390bd7e1a8cbec942f81c5ae9af5d
# B (Imaginary part of X)
B_int = 0x0c2a02ff3f98b4c1c406228fdf45158795144f978887dd1133680719851e5eda

X = FQ2([A_int, B_int])

print(f"X: {hex(X.coeffs[0].n)}, {hex(X.coeffs[1].n)}")

# X^2
X2 = X * X
print(f"X^2: {hex(X2.coeffs[0].n)}, {hex(X2.coeffs[1].n)}")

# M = 3 * X^2
M = X2 + X2 + X2
print(f"M: {hex(M.coeffs[0].n)}, {hex(M.coeffs[1].n)}")

# Compare with C output
# C X^2: 0ef8321c265fbcb5bf380450616f4973c477fd3fb52ed6289308c700c7054dbb, 04b3fb7dde1881d84543d5e82ee4a228152949c1759cf4253eca9b315c55af7c
# C M:   2ce89654731f36213da80cf1244ddc5b4d67f7bf1f8c8279b91a5502550fe931, 0e1bf2799a498588cfcb81b88cade6783f7bdd4460d6dc6fbc5fd19415010e74
