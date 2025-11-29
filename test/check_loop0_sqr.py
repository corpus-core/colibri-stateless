from py_ecc.bn128 import FQ2, FQ, field_modulus

def to_fq2(c0_hex, c1_hex):
    return FQ2([int(c0_hex, 16), int(c1_hex, 16)])

def to_fq(hex_str):
    return FQ(int(hex_str, 16))

# Input P (from Loop 1 Output) = Q for Loop 0
X = to_fq2(
    "24f1c302bc4003d979aa31d77258132a28160170db08b08d6ff4b826fe4247ab",
    "09047b3761316c79af5807e663307206d531f854fb69acf480c6c2b8eee32809"
)
Y = to_fq2(
    "0e82e99f97c2d700101ff05ed3b603b07615f800beed0777a8402c84b3fbc8c3",
    "29ad1f5c70e19a0d92a9d7da50af9350af0b38184648626bee3fe906b2d052f3"
)
Z = to_fq2(
    "05df59f166a7c41e38a0707392cee98a1dedb65fd72d95ad62cc75cba70a3745",
    "13c689b8769021e4a2fc56dda2129f135c1ad41ac49c9878c9ff91ca026dc2c8"
)

# Point P (A) from logs
Px = to_fq("1a78f6839bb5d88d1674dc0bb7231aef8ad32aded0418bcd3c0e8186365a448f")
Py = to_fq("1d1160f7903c238c2ac8a24938448337620e9e20c1511edb61e14766a8aa3569")

# Convert to Affine
Z_inv = Z.inv()
Z2 = Z_inv * Z_inv
Z3 = Z2 * Z_inv

xQ = X * Z2
yQ = Y * Z3

# Lambda = 3 xQ^2 / 2 yQ
num = xQ * xQ * 3
den = yQ * 2
den_inv = den.inv()
lam = num * den_inv

print(f"den: {int(den.coeffs[0]):x}, {int(den.coeffs[1]):x}")

# Check scaled yP
c0_target = 0x1352ed7b50f57c9d8d87a36d493cd5263572cc70a720abb1da3f44b02fd2c7de
print(f"Target c0: {c0_target:x}")

scaled_yP = den * Py
print(f"scaled_yP: {int(scaled_yP.coeffs[0]):x}, {int(scaled_yP.coeffs[1]):x}")

if int(scaled_yP.coeffs[0]) == c0_target:
    print("MATCHES SCALED yP (real part)!")
else:
    print("Does not match scaled yP")
