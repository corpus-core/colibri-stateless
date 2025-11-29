import sys
from py_ecc.bn128 import curve_order, field_modulus, FQ, FQ2, FQ12, b, b2, b12

# Inputs from debug_compare_bn254.cpp
P_x = 1
P_y = 2
P = (FQ(P_x), FQ(P_y), FQ(1)) # Jacobian (x, y, z)

Q_x0 = 10857046999023057135944570762232829481370756359578518086990519993285655852781
Q_x1 = 11559732032986387107991004021392285783925812861821192530917403151452391805634
Q_y0 = 14489641022887904620165577529858970960862166400715456244041815460072481058216
Q_y1 = 19198898911466047603531563556829749380369505941267838797287448093746762753933

Q_x = FQ2([Q_x0, Q_x1])
Q_y = FQ2([Q_y0, Q_y1])
Q = (Q_x, Q_y, FQ2.one()) # Jacobian

# Loop parameter
ate_loop_count = 29793968203157093288

def double_step(f, T, P):
    Tx, Ty, Tz = T
    Px, Py, Pz = P
    
    t0 = Tz ** 2
    t4 = Tx * Ty
    t1 = Ty ** 2
    
    t3 = t0 + t0
    t4 = t4 * FQ2([field_modulus + 1 >> 1, 0]) # div2
    t5 = t0 + t1
    t0 = t0 + t3
    
    # mul_twist_b
    xi = FQ2([9, 1])
    twist_b = FQ2([3, 0]) / xi
    
    t2 = t0 * twist_b
    t0 = Tx ** 2
    
    t3 = t2 + t2
    t3 = t3 + t2
    
    Tx_new = t1 - t3
    t3 = t3 + t1
    Tx_new = Tx_new * t4
    
    t3 = t3 * FQ2([field_modulus + 1 >> 1, 0]) # div2
    T0_tmp = t3 ** 2
    T1_tmp = t2 ** 2
    
    T0_tmp = T0_tmp - T1_tmp
    T1_tmp = T1_tmp + T1_tmp
    T0_tmp = T0_tmp - T1_tmp
    
    Ty_new = T0_tmp
    
    t3 = Ty + Tz
    t3 = t3 ** 2
    t3 = t3 - t5
    Tz_new = t1 * t3
    
    l_a = t2 - t1
    l_c = t0
    l_b = t3
    
    # Update Line with P
    # l_c is FQ2, Px is FQ. l_c * Px -> scalar mul
    l_c = l_c * Px
    l_b = l_b * Py
    
    # Map to Fp12
    # l.c1.c1 = l_a
    # l.c0.c0 = l_b
    # l.c1.c0 = l_c
    
    # Flattened coeffs order: c0.c0, c0.c1, c0.c2, c1.c0, c1.c1, c1.c2
    # Each cX.cY is FQ2 (2 FQ)
    # Flattened FQ order: 
    # c0.c0.c0, c0.c0.c1
    # c0.c1.c0, c0.c1.c1
    # ...
    
    coeffs = [
        l_b.coeffs[0], l_b.coeffs[1], # c0.c0
        FQ(0), FQ(0),                 # c0.c1
        FQ(0), FQ(0),                 # c0.c2
        l_c.coeffs[0], l_c.coeffs[1], # c1.c0
        l_a.coeffs[0], l_a.coeffs[1], # c1.c1
        FQ(0), FQ(0)                  # c1.c2
    ]
    
    l_val = FQ12(coeffs)
    f_new = f * l_val
    
    return f_new, (Tx_new, Ty_new, Tz_new)

def add_step(f, T, Q, P):
    Tx, Ty, Tz = T # R
    Qx, Qy, Qz = Q # Q
    Px, Py, Pz = P
    
    t1 = Tz * Qx
    t2 = Tz * Qy
    t1 = Tx - t1
    t2 = Ty - t2
    t3 = t1 ** 2
    Tx_new = t3 * Tx
    t4 = t2 ** 2
    t3 = t3 * t1
    t4 = t4 * Tz
    t4 = t4 + t3
    t4 = t4 - Tx
    t4 = t4 - Tx
    Tx_new = Tx_new - t4
    T1 = t2 * Tx_new
    T2 = t3 * Ty
    T2 = T1 - T2
    Ty_new = T2
    Tx_new = t1 * t4
    Tz_new = t3 * Tz
    
    l_c = -t2
    T1 = t2 * Qx
    T2 = t1 * Qy
    l_a = T1 - T2
    l_b = t1
    
    l_c = l_c * Px
    l_b = l_b * Py
    
    coeffs = [
        l_b.coeffs[0], l_b.coeffs[1], # c0.c0
        FQ(0), FQ(0),                 # c0.c1
        FQ(0), FQ(0),                 # c0.c2
        l_c.coeffs[0], l_c.coeffs[1], # c1.c0
        l_a.coeffs[0], l_a.coeffs[1], # c1.c1
        FQ(0), FQ(0)                  # c1.c2
    ]
    
    l_val = FQ12(coeffs)
    f_new = f * l_val
    
    return f_new, (Tx_new, Ty_new, Tz_new)


def print_fp12(label, f):
    print(f"{label}: ", end="")
    coeffs = f.coeffs
    # FQ12 coeffs are FQ
    for i in range(12):
        val = int(coeffs[i])
        print(f"{val:064x} ", end="")
    print()

# Miller Loop
f = FQ12.one()
T = Q

loop_param = 29793968203157093288

for i in range(64, -1, -1):
    f = f * f
    f, T = double_step(f, T, P)
    
    bit = (loop_param >> i) & 1
    if bit:
        f, T = add_step(f, T, Q, P)
        
    print_fp12(f"ML step {i}", f)
