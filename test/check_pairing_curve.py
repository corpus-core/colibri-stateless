# ... imports ...
from py_ecc.bn128 import G1, G2, add, multiply, pairing, FQ, FQ2, FQ12, is_on_curve, field_modulus, b2
import sys

sys.setrecursionlimit(50000)

# ... iter_pow ...
def iter_pow(self, other):
    if other == 0:
        return self.__class__.one()
    elif other == 1:
        return self
    
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

BETA_HEX = "0967032fcbf776d1afc985f88877f182d38480a653f2decaa9794cbc3bf3060c0e187847ad4c798374d0d6732bf501847dd68bc0e071241e0213bc7fc13db7ab001752a100a72fdf1e5a5d6ea841cc20ec838bccfcf7bd559e79f1c9c759b6a0192a8cc13cd9f762871f21e43451c6ca9eeab2cb2987c4e366a185c25dac2e7f"

def parse_g2_eth(hex_str):
    # First 32 bytes: Im
    # Second 32 bytes: Re
    # ...
    x_im = int(hex_str[:64], 16)
    x_re = int(hex_str[64:128], 16)
    y_im = int(hex_str[128:192], 16)
    y_re = int(hex_str[192:], 16)
    return (FQ2([x_re, x_im]), FQ2([y_re, y_im]))

def parse_g2_swapped(hex_str):
    # Interpret First 32 bytes as Re, Second as Im
    # BETA_HEX has 09... then 0e...
    # 09... comes from X1 in C. 0e... from X0.
    # If we assume X0 is Re, X1 is Im:
    # then X0 (0e) is Re, X1 (09) is Im.
    # This matches parse_g2_eth because BETA_HEX put X1 first (Im).
    
    # What if X0 is Im, X1 is Re?
    # Then Re=X1 (09), Im=X0 (0e).
    # Hex string has X1 (09) then X0 (0e).
    # So First 32 is Re, Second 32 is Im.
    
    x_re = int(hex_str[:64], 16)
    x_im = int(hex_str[64:128], 16)
    y_re = int(hex_str[128:192], 16)
    y_im = int(hex_str[192:], 16)
    return (FQ2([x_re, x_im]), FQ2([y_re, y_im]))

def main():
    P_standard = parse_g2_eth(BETA_HEX)
    print(f"Standard interpretation (Re=0e.., Im=09..): {is_on_curve(P_standard, b2)}")
    
    P_swapped = parse_g2_swapped(BETA_HEX)
    print(f"Swapped interpretation (Re=09.., Im=0e..): {is_on_curve(P_swapped, b2)}")

if __name__ == "__main__":
    main()


