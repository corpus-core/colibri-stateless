# ... imports ...
from py_ecc.bn128 import FQ, FQ2, FQ12, field_modulus, G1, G2, bn128_pairing, pairing
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

# Monkeypatch linefunc to trace arguments
original_linefunc = bn128_pairing.linefunc

def trace_linefunc(P1, P2, T):
    res = original_linefunc(P1, P2, T)
    
    # Check if P1 and P2 are effectively same point (ignoring Z scaling)
    # Projective equality: X1/Z1 = X2/Z2 => X1*Z2 = X2*Z1
    # If different, it's an ADD.
    
    is_add = False
    # Approximate check: if x coordinates differ significantly?
    # Or just trust that Miller loop calls linefunc(R, R, ...) for DBL and linefunc(R, Q, ...) for ADD
    # The third argument is T (the G1 point).
    # First two are G2 points.
    
    # In py_ecc implementation:
    # f = f * f * linefunc(R, R, P)
    # if bit: f = f * linefunc(R, Q, P)
    
    # So we can just check if P1 == P2.
    # But P1 and P2 are tuples of FQ2.
    
    if P1 != P2:
        is_add = True
        print("DEBUG PY ADD line:")
        coeffs = res.coeffs
        # Print in groups of 2 (FQ2 elements)
        # py_ecc FQ12 basis:
        # coeffs[0] + coeffs[1]*w + ...
        # C FQ12 basis:
        # c0 + c1*w (where c0, c1 are Fp6)
        # c0 = c00 + c01*v + c02*v^2
        # c1 = c10 + c11*v + c12*v^2
        # And v is sparse? w is sparse?
        # This mapping is tricky.
        
        for i in range(len(coeffs)):
             print(f"c[{i}]: {hex(coeffs[i].n)}")
            
    return res

bn128_pairing.linefunc = trace_linefunc

def main():
    A = parse_g1(A_HEX)
    B = parse_g2_eth(B_HEX)
    
    print("Tracing p1...")
    try:
        pairing(B, A)
    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    main()
