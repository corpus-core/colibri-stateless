# ... imports ...
from py_ecc.bn128 import FQ, FQ2, FQ12, field_modulus, G1, G2, bn128_pairing
import sys

sys.setrecursionlimit(50000)

# ... pow patch ...
def iter_pow(self, other):
    if other == 0:
        return self.__class__.one()
    elif other == 1:
        return self
    if other < 0:
        return (self.__class__.one() / self) ** (-other)
    res = self.__class__.one()
    base = self
    exp = int(other)
    while exp > 0:
        if exp % 2 == 1:
            res = res * base
        base = base * base
        exp //= 2
    return res

FQ.__pow__ = iter_pow
FQ2.__pow__ = iter_pow
FQ12.__pow__ = iter_pow

# Points
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

def print_reconstructed(f):
    # Mapping:
    # c0.c0 <-> (C0 + 9C6, C6)
    # c1.c0 <-> (C1 + 9C7, C7)
    # c0.c1 <-> (C2 + 9C8, C8)
    # c1.c1 <-> (C3 + 9C9, C9)
    # c0.c2 <-> (C4 + 9C10, C10)
    # c1.c2 <-> (C5 + 9C11, C11)
    
    coeffs = f.coeffs
    indices = [
        ("c0.c0", 0, 6),
        ("c1.c0", 1, 7),
        ("c0.c1", 2, 8),
        ("c1.c1", 3, 9),
        ("c0.c2", 4, 10),
        ("c1.c2", 5, 11)
    ]
    
    for label, i, j in indices:
        re = (coeffs[i].n + 9 * coeffs[j].n) % field_modulus
        im = coeffs[j].n
        print(f"{label}: {hex(re)}, {hex(im)}")

def main():
    print("Checking Pairing Value...")
    A = parse_g1(A_HEX)
    B = parse_g2_eth(B_HEX)
    
    Q_twisted = bn128_pairing.twist(B)
    P_fq12 = bn128_pairing.cast_point_to_fq12(A)
    
    f = bn128_pairing.miller_loop(Q_twisted, P_fq12)
    
    print("Miller Loop Result (Reconstructed C-Basis):")
    print_reconstructed(f)

if __name__ == "__main__":
    main()
