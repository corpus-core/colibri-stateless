# ... (same imports and pow patch) ...
from py_ecc.bn128 import FQ, FQ2, FQ12, field_modulus, G1, G2, bn128_pairing
import sys

sys.setrecursionlimit(50000)

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

def miller_loop_correct(Q, P):
    if Q is None or P is None:
        return FQ12.one()
    R = Q
    f = FQ12.one()
    
    # Cast P to FQ2 for compatibility with linefunc subtraction
    P_fq2 = (FQ2([P[0].n, 0]), FQ2([P[1].n, 0]))
    
    for i in range(63, -1, -1):
        f = f * f
        f = f * bn128_pairing.linefunc(R, R, P_fq2)
        R = bn128_pairing.double(R)
        if 29793968203157093288 & (2**i):
            f = f * bn128_pairing.linefunc(R, Q, P_fq2)
            R = bn128_pairing.add(R, Q)
        
        if i >= 60:
            print(f"TRACE i={i}:")
            # Print accumulator f
            # f is FQ12. It has coeffs.
            print(f"f: {f}")
    return f

def main():
    print("Starting trace...")
    A = parse_g1(A_HEX)
    B = parse_g2_eth(B_HEX)
    
    try:
        miller_loop_correct(B, A)
    except TypeError as e:
        print(f"TypeError: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    main()
