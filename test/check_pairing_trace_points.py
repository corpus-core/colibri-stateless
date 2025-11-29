# ... imports ...
from py_ecc.bn128 import FQ, FQ2, FQ12, field_modulus, G1, G2, bn128_pairing
import sys

sys.setrecursionlimit(50000)

# Points
A_HEX = "1a78f6839bb5d88d1674dc0bb7231aef8ad32aded0418bcd3c0e8186365a448f1d1160f7903c238c2ac8a24938448337620e9e20c1511edb61e14766a8aa3569"
B_HEX = "0c2a02ff3f98b4c1c406228fdf45158795144f978887dd1133680719851e5eda30363c09c5fcc01c3f5d300534b47a7a4f0390bd7e1a8cbec942f81c5ae9af5d2fcee692ab4e54d1f8157320419655b49a0ed035ae613fb66983486379a4b7c81dc3f46b40c711f5c6a74083b54ac7a6ad6b469342b4080de759da9b83e78d72"

def parse_g2_eth(hex_str):
    x_im = int(hex_str[:64], 16)
    x_re = int(hex_str[64:128], 16)
    y_im = int(hex_str[128:192], 16)
    y_re = int(hex_str[192:], 16)
    return (FQ2([x_re, x_im]), FQ2([y_re, y_im]))

def untwist_and_print(R, label):
    # X = R[0] (FQ12). Expect coeffs at 2 and 8.
    # X = (a - 9b) w^2 + b w^8
    # b = C8, a = C2 + 9b
    xc = R[0].coeffs
    b_x = xc[8].n
    a_x = (xc[2].n + 9 * b_x) % field_modulus
    
    # Y = R[1] (FQ12). Expect coeffs at 3 and 9.
    # Y = (c - 9d) w^3 + d w^9
    # d = C9, c = C3 + 9d
    yc = R[1].coeffs
    d_y = yc[9].n
    c_y = (yc[3].n + 9 * d_y) % field_modulus
    
    print(f"{label}:")
    print(f"T.x: {hex(a_x)}, {hex(b_x)}")
    print(f"T.y: {hex(c_y)}, {hex(d_y)}")

def miller_loop_trace_points(Q):
    # Q is FQ2 point. Twist it to FQ12.
    R = bn128_pairing.twist(Q)
    
    # We trace the twisted point R, but untwist for display
    untwist_and_print(R, "TRACE START")
    
    SIX_T_PLUS_2 = 29793968203157093288
    
    for i in range(63, -1, -1):
        # Double step
        R = bn128_pairing.double(R)

        if i >= 60:
            untwist_and_print(R, f"TRACE i={i} after DBL")

        if SIX_T_PLUS_2 & (2**i):
            # Add step (add Q_twisted)
            Q_twisted = bn128_pairing.twist(Q)
            R = bn128_pairing.add(R, Q_twisted)

            if i >= 60:
                untwist_and_print(R, f"TRACE i={i} after ADD")

def main():
    print("Starting Point Trace (Twist-Aware)...")
    B = parse_g2_eth(B_HEX)
    
    try:
        miller_loop_trace_points(B)
    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    main()
