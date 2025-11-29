from py_ecc.bn128 import FQ, FQ2, FQ12, bn128_pairing, bn128_curve, field_modulus
import sys

A_HEX = "1a78f6839bb5d88d1674dc0bb7231aef8ad32aded0418bcd3c0e8186365a448f1d1160f7903c238c2ac8a24938448337620e9e20c1511edb61e14766a8aa3569"
B_HEX = "0c2a02ff3f98b4c1c406228fdf45158795144f978887dd1133680719851e5eda30363c09c5fcc01c3f5d300534b47a7a4f0390bd7e1a8cbec942f81c5ae9af5d2fcee692ab4e54d1f8157320419655b49a0ed035ae613fb66983486379a4b7c81dc3f46b40c711f5c6a74083b54ac7a6ad6b469342b4080de759da9b83e78d72"

def parse_g1(hex_str):
    x = int(hex_str[:64], 16)
    y = int(hex_str[64:], 16)
    return (FQ(x), FQ(y))

def parse_g2_eth(hex_str):
    x_im = int(hex_str[:64], 16)
    x_re = int(hex_str[64:128], 16)
    y_im = int(hex_str[128:192], 16)
    y_re = int(hex_str[192:], 16)
    return (FQ2([x_re, x_im]), FQ2([y_re, y_im]))

def print_fq2(val, label):
    # val is FQ12 (if twisted) or FQ2 (if not)
    # If twisted, coeffs at 2, 8 or 3, 9.
    # But here we use twisted Q.
    # Twist map: x' = x w^2. y' = y w^3.
    # So we should look at coeffs 2, 8 for X and 3, 9 for Y.
    # BUT we want to trace num/den which are in Fp12.
    # num = 3 x^2. x is twisted. x^2 has w^4.
    # den = 2 y. y is twisted. y has w^3.
    # m = num/den -> w^4/w^3 = w.
    
    # To debug arithmetic, I should UNTWIST X and Y first, compute in Fp2, and check.
    # Because my C code does that (converts to Affine Fp2).
    pass

def trace_lambda():
    A = parse_g1(A_HEX)
    B = parse_g2_eth(B_HEX)
    
    # Work in Fp2 (untwisted) to match C Safe Implementation
    x = B[0]
    y = B[1]
    
    # Standard Affine Formula in Fp2
    # num = 3 x^2
    x2 = x * x
    num = x2 * 3
    
    # den = 2 y
    den = y * 2
    
    # inv den
    den_inv = den ** (field_modulus ** 2 - 2) # Fp2 inverse
    
    lam = num * den_inv
    
    print("Fp2 Calculation:")
    print(f"x: {hex(x.coeffs[0].n)}, {hex(x.coeffs[1].n)}")
    print(f"y: {hex(y.coeffs[0].n)}, {hex(y.coeffs[1].n)}")
    print(f"num: {hex(num.coeffs[0].n)}, {hex(num.coeffs[1].n)}")
    print(f"den: {hex(den.coeffs[0].n)}, {hex(den.coeffs[1].n)}")
    print(f"den_inv: {hex(den_inv.coeffs[0].n)}, {hex(den_inv.coeffs[1].n)}")
    print(f"lambda: {hex(lam.coeffs[0].n)}, {hex(lam.coeffs[1].n)}")

if __name__ == "__main__":
    trace_lambda()


