from py_ecc.bn128 import FQ, FQ2, field_modulus

# Xi = 9 + u
# In py_ecc FQ2([c0, c1]) -> c0 + c1 u.
# u^2 = -1.
# So Xi = FQ2([9, 1]).
xi = FQ2([9, 1])

exp1 = (field_modulus - 1) // 3
exp2 = (field_modulus - 1) // 2

xi_p_3 = xi ** exp1
xi_p_2 = xi ** exp2

print(f"xi_p_3: {hex(xi_p_3.coeffs[0].n)}, {hex(xi_p_3.coeffs[1].n)}")
print(f"xi_p_2: {hex(xi_p_2.coeffs[0].n)}, {hex(xi_p_2.coeffs[1].n)}")


