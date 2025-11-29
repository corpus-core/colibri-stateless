from py_ecc.bn128 import FQ, FQ2, FQ12, field_modulus
import sys

# Define the curve parameters and generator point manually if needed, 
# or use the ones from py_ecc
from py_ecc.bn128 import bn128_pairing
from py_ecc.bn128 import bn128_curve

# Values from C debug log (Test Case 1)
# A (G1): [x, y]
A_HEX = [
    "0x1a78f6839bb5d88d1674dc0bb7231aef8ad32aded0418bcd3c0e8186365a448f",
    "0x1d1160f7903c238c2ac8a24938448337620e9e20c1511edb61e14766a8aa3569"
]

# B (G2): [x_im, x_re, y_im, y_re]
B_HEX_PARTS = [
    "0x0c2a02ff3f98b4c1c406228fdf45158795144f978887dd1133680719851e5eda", # X.c1 (Im)
    "0x30363c09c5fcc01c3f5d300534b47a7a4f0390bd7e1a8cbec942f81c5ae9af5d", # X.c0 (Re)
    "0x2fcee692ab4e54d1f8157320419655b49a0ed035ae613fb66983486379a4b7c8", # Y.c1 (Im)
    "0x1dc3f46b40c711f5c6a74083b54ac7a6ad6b469342b4080de759da9b83e78d72"  # Y.c0 (Re)
]

def hex_to_int(h):
    return int(h, 16)

def format_fp(val):
    # Handle FQ objects
    if hasattr(val, 'n'):
        val = val.n
    return f"{val:064x}"

def print_c_format(label, f):
    indices = [0, 2, 4, 1, 3, 5]
    
    print(f"{label}:")
    
    output_parts = []
    for k in indices:
        # Operations on FQ objects
        c_real_fq = f.coeffs[k] + f.coeffs[k+6] * 9
        c_imag_fq = f.coeffs[k+6]
        
        output_parts.append(format_fp(c_real_fq))
        output_parts.append(format_fp(c_imag_fq))
    
    for part in output_parts:
        print(part)

def untwist(P_fq12):
    # P_fq12 is (x, y) where x, y are FQ12 elements (but actually FQ2 embedded)
    
    w = FQ12([0, 1] + [0]*10) 
    
    x_twisted, y_twisted = P_fq12
    
    w2_inv = (w**2).inv()
    w3_inv = (w**3).inv()
    
    x = x_twisted * w2_inv
    y = y_twisted * w3_inv
    
    def extract_fq2(val_fq12):
        b = val_fq12.coeffs[6]
        a = (val_fq12.coeffs[0] + val_fq12.coeffs[6] * 9)
        return FQ2([a.n, b.n])

    return (extract_fq2(x), extract_fq2(y))

def endomorphism(Q):
    # Q is (x, y) in FQ2
    # Compute Q1, Q2
    
    # xi = 9+u
    xi = FQ2([9, 1])
    
    xi_p_3 = xi ** ((field_modulus - 1) // 3)
    xi_p_2 = xi ** ((field_modulus - 1) // 2)
    
    # Conjugate x: (re, im) -> (re, -im)
    def conjugate(val):
        return FQ2([val.coeffs[0], -val.coeffs[1]])
        
    # Q1
    # x' = conj(x) * xi^((p-1)/3)
    # y' = conj(y) * xi^((p-1)/2)
    q1_x = conjugate(Q[0]) * xi_p_3
    q1_y = conjugate(Q[1]) * xi_p_2
    Q1 = (q1_x, q1_y)
    
    # Q2
    # Same map applied to Q1?
    # In C: Q2 derived from Q1 same way.
    # Q2.x = conj(Q1.x) * xi_p_3
    # Q2.y = conj(Q1.y) * xi_p_2
    q2_x = conjugate(q1_x) * xi_p_3
    q2_y = conjugate(q1_y) * xi_p_2
    
    # C code says: Q2.y = -Q2.y for final step
    q2_y = -q2_y
    
    Q2 = (q2_x, q2_y)
    
    return Q1, Q2

def miller_loop_custom(Q, P):
    # Q is G2 (coeffs in FQ2), P is G1 (coeffs in FQ)
    
    # Twist Q into FQ12
    Q_twisted = bn128_pairing.twist(Q) # Returns affine (x, y) in FQ12
    
    # Cast P to FQ12 for operations
    P_fq12 = (FQ12([P[0].n] + [0]*11), FQ12([P[1].n] + [0]*11))
    
    # Current point T in FQ12 (Affine)
    T = Q_twisted
    
    f = FQ12.one()
    
    # Loop
    print(f"Miller loop range: {list(range(63, -1, -1))}")
    
    print_c_format("DEBUG MILLER LOOP START f", f)
    
    for i in range(63, -1, -1):
        # Normal Loop steps...
        l_dbl = bn128_pairing.linefunc(T, T, P_fq12)
        f = f * f * l_dbl
        T = bn128_pairing.double(T)
        
        if bn128_pairing.ate_loop_count & (2**i):
            l_add = bn128_pairing.linefunc(T, Q_twisted, P_fq12)
            f = f * l_add
            T = bn128_pairing.add(T, Q_twisted)
            
    
    print_c_format("DEBUG MILLER LOOP END f", f)
    
    print("--- ENDOMORPHISM ---")
    
    Q1, Q2 = endomorphism(Q)
    Q1_twisted = bn128_pairing.twist(Q1)
    Q2_twisted = bn128_pairing.twist(Q2)
    
    # Steps
    l_end1 = bn128_pairing.linefunc(T, Q1_twisted, P_fq12)
    print_c_format("DEBUG l_end1", l_end1)
    f = f * l_end1
    T = bn128_pairing.add(T, Q1_twisted)
    
    l_end2 = bn128_pairing.linefunc(T, Q2_twisted, P_fq12)
    print_c_format("DEBUG l_end2", l_end2)
    f = f * l_end2
    T = bn128_pairing.add(T, Q2_twisted)
    
    print_c_format("DEBUG MILLER RES", f)
    
    return f

# Main
ax = hex_to_int(A_HEX[0])
ay = hex_to_int(A_HEX[1])
P = (FQ(ax), FQ(ay))

bx_im = hex_to_int(B_HEX_PARTS[0])
bx_re = hex_to_int(B_HEX_PARTS[1])
by_im = hex_to_int(B_HEX_PARTS[2])
by_re = hex_to_int(B_HEX_PARTS[3])

Q = (FQ2([bx_re, bx_im]), FQ2([by_re, by_im]))

print(f"P: {P}")
print(f"Q: {Q}")

f = miller_loop_custom(Q, P)
