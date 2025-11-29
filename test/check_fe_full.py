from py_ecc.bn128 import FQ, FQ2, FQ12, field_modulus
import sys

# Full t0 from DEBUG FE conj (C trace)
HEX_VALS = """
1eec241b45803cf65bd1f466b2a032e25d71e6dbcc89e0a6f87490333b360671
23e3449e26a01686ade907dc49cbde9afa2ca50ddf8ebdf8b91559b44b61af07
04955b48956135e04443759a9c121f4a1396760191165343aae885fbc9b1cc1d
24e3e902518cf0c98f0e52ced59c7ded340cac86132d666858a514cdac7d594b
1d4605b35bb55fc348c75197d608c5d63504900223a66f06c0cf0bbdbb0abee0
0f4f077c0fa056ed509cefaef4bcb3cf7ed0815a5cb43e563e2ab4364689b1d
29662083e60f6a7473a2d823dc79cc351453d04c734cb13465b144d7284ed4cf
067aba689958c4730a5e7ba231ed4633dbe9c3f2a89a313a63669a2079167fea
2ce1bb31f70275cc8ed8006e19623cd733155a2beace658a414dcea11886579f
29d0a2e3fce5cea924a27ebbbf38b1aa69406506e3fdac77eb407e62982f6ab7
26d398eeb35c04d0ac1112c9df8958d14903fa1663e6eddfdc67f3f4b5f624ad
2f5a042a306dc24c5dbae48cabeb230d254bf75914eba5b34c7cad582c4b945b
""".strip().split()

def hex_to_int(h):
    return int(h, 16)

vals = [hex_to_int(x) for x in HEX_VALS]

coeffs = [0] * 12
indices = [0, 2, 4, 1, 3, 5]

for i, k in enumerate(indices):
    a = vals[2*i]
    b = vals[2*i+1]
    coeffs[k+6] = b
    coeffs[k] = (a - 9*b) % field_modulus

t0 = FQ12(coeffs)

f_coeffs = list(t0.coeffs)
for k in [1, 3, 5, 7, 9, 11]:
    f_coeffs[k] = -f_coeffs[k]

f = FQ12(f_coeffs)

# Easy part
f_inv = f.inv()
f1 = t0 * f_inv
f_easy = f1 * (f1 ** (field_modulus**2))

def format_fp(val):
    if hasattr(val, 'n'): val = val.n
    return f"{val:064x}"

def print_c_format(label, f):
    indices = [0, 2, 4, 1, 3, 5]
    print(f"{label}:")
    output_parts = []
    for k in indices:
        c_real_fq = f.coeffs[k] + f.coeffs[k+6] * 9
        c_imag_fq = f.coeffs[k+6]
        output_parts.append(format_fp(c_real_fq))
        output_parts.append(format_fp(c_imag_fq))
    for part in output_parts:
        print(part)

print_c_format("DEBUG f_easy", f_easy)

# Hard part
# u = 4965661367192848881
u = 4965661367192848881

# Based on C implementation:
# bn254_fp12_t a, b, a2, y;
# a = f_easy
# b = a ^ u
# fp12_pow(&b, &a, u); -> b = f_easy^u
b = f_easy ** u
# fp12_pow(&a, &b, u); -> a = b^u = f_easy^(u^2)
a = b ** u
# fp12_pow(&a2, &a, u); -> a2 = a^u = f_easy^(u^3)
a2 = a ** u

# fp12_print("DEBUG FE a (part1)", &a); (This prints a = f_easy^(u^2))
print_c_format("DEBUG FE a (part1)", a)

# fp6_neg(&b.c1, &b.c1); // b = b.conj()
# b.conj()
b_coeffs = list(b.coeffs)
for k in [1, 3, 5, 7, 9, 11]:
    b_coeffs[k] = -b_coeffs[k]
b = FQ12(b_coeffs)
print_c_format("DEBUG FE b (neg)", b)

# fp12_mul_internal(&b, &b, &a); // b = b * a
b = b * a
print_c_format("DEBUG FE b (mul a)", b)

# fp12_mul_internal(&a2, &a2, &a); // a2 = a2 * a
a2 = a2 * a
print_c_format("DEBUG FE a2 (mul a)", a2)

# fp12_frob(&a, &f_easy);
# fp12_frob(&a, &a); // a = f_easy^(p^2)
a = f_easy ** (field_modulus**2)
print_c_format("DEBUG FE a (frob2)", a)

# fp12_mul_internal(&a, &a, &a2); // a = a * a2
a = a * a2
print_c_format("DEBUG FE a (mul a2)", a)

# fp12_frob(&a2, &b);
# a2.frob -> b^p
# fp12_mul_internal(&a, &a, &a2); // a = a * b^p
a2 = b ** field_modulus
a = a * a2
print_c_format("DEBUG FE a (part2)", a)

# b.frob -> b^p
b = b ** field_modulus
# fp12_frob(&b, &b); // b^p^2
b = b ** field_modulus
# fp12_frob(&b, &b); // b^p^3
b = b ** field_modulus
# fp12_mul_internal(&y, &a, &b); // y = a * b^p^3
y = a * b
print_c_format("DEBUG FE y (final)", y)

# Check if y == 1
if y == FQ12.one():
    print("SUCCESS: y is 1")
else:
    print("FAILURE: y is NOT 1")
