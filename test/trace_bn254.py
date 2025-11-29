from py_ecc.bn128 import FQ, FQ2, FQ12, field_modulus, curve_order, G1, G2, b, pairing, final_exponentiate
from py_ecc.bn128 import bn128_curve
import sys
sys.setrecursionlimit(20000)

# Constants
ate_loop_count = 29793968203157093288
log_ate_loop_count = 63

# Helper to print FQ2 in hex
def fq12_to_hex_fp2(val):
    # val is FQ2 (a + bi)
    # Output: hex(a), hex(b)
    a = val.coeffs[0]
    b_val = val.coeffs[1]
    return f"{int(a):064x}, {int(b_val):064x}"

# Helper to print FQ12 in C-compatible format (c00, c01, c02, c10, c11, c12)
def fq12_to_hex(f):
    # Assuming Py uses basis 1, w, w^2 ... w^11 where w^6 = 9+i.
    # So i = w^6 - 9.
    # c_k (Fp2) = a + bi = a + b(w^6 - 9) = (a - 9b) + b w^6.
    # We reverse this to get a, b from coeffs.
    # a = coeff_low + 9 * coeff_high
    # b = coeff_high
    
    # C order: c00, c01, c02, c10, c11, c12.
    # c00 corresponds to w^0, w^6
    # c01 corresponds to w^2, w^8
    # c02 corresponds to w^4, w^10
    # c10 corresponds to w^1, w^7
    # c11 corresponds to w^3, w^9
    # c12 corresponds to w^5, w^11
    
    def get_fp2(low_idx, high_idx):
        b_val = f.coeffs[high_idx]
        a_val = f.coeffs[low_idx] + FQ(9) * b_val
        return a_val, b_val

    s = ""
    
    # c00
    a, b_val = get_fp2(0, 6)
    s += f"{int(a):064x}{int(b_val):064x}"
    
    # c01
    a, b_val = get_fp2(2, 8)
    s += f"{int(a):064x}{int(b_val):064x}"
    
    # c02
    a, b_val = get_fp2(4, 10)
    s += f"{int(a):064x}{int(b_val):064x}"
    
    # c10
    a, b_val = get_fp2(1, 7)
    s += f"{int(a):064x}{int(b_val):064x}"
    
    # c11
    a, b_val = get_fp2(3, 9)
    s += f"{int(a):064x}{int(b_val):064x}"
    
    # c12
    a, b_val = get_fp2(5, 11)
    s += f"{int(a):064x}{int(b_val):064x}"
    
    return s

# Custom Point classes for G1 (Affine) and G2 (Jacobian)
class PointG1:
    def __init__(self, x, y):
        self.x = x
        self.y = y

class PointG2:
    def __init__(self, x, y, z):
        self.x = x
        self.y = y
        self.z = z

    def is_zero(self):
        return self.z == FQ2.zero()

# Helper to construct FQ12 from FQ2 components
def fq12_from_components(c00, c01, c02, c10, c11, c12):
    # cXX are FQ2 objects (a + bi)
    # Map to Py coeffs using: a + bi = (a - 9b) + b w^6
    
    coeffs = [FQ(0)] * 12
    
    def set_coeffs(low_idx, high_idx, fp2_val):
        a = fp2_val.coeffs[0]
        b_val = fp2_val.coeffs[1]
        coeffs[low_idx] = a - FQ(9) * b_val
        coeffs[high_idx] = b_val

    set_coeffs(0, 6, c00)
    set_coeffs(2, 8, c01)
    set_coeffs(4, 10, c02)
    set_coeffs(1, 7, c10)
    set_coeffs(3, 9, c11)
    set_coeffs(5, 11, c12)
    
    return FQ12(coeffs)

# Twist parameter for BN254 D-type twist
# xi = 9 + i
# twist_b = 3 / xi
xi = FQ2([FQ(9), FQ(1)])
twist_b = FQ2([FQ(3), FQ(0)]) * xi.inv()

# Line Function for Doubling (Jacobian G2)
def line_func_dbl(f, T, P):
    # ... (same as before, omitted for brevity, relying on import/exec context? No, must redefine)
    # Copying body from previous version...
    
    x2 = T.x * T.x
    m = x2 + x2 + x2 
    y2 = T.y * T.y 
    s = T.x * y2 
    s = s + s + s + s 
    z2 = T.z * T.z 
    l_c = -(m * z2)
    l_b_temp = T.y * T.z 
    l_b_temp = l_b_temp + l_b_temp 
    l_b = l_b_temp * z2 
    t0 = y2 + y2 
    l_a = m * T.x - t0 
    T_new_x = m * m - (s + s)
    t0 = s - T_new_x 
    t1 = m * t0 
    t0 = y2 * y2 
    t0 = t0 + t0 + t0 + t0 + t0 + t0 + t0 + t0 
    T_new_y = t1 - t0
    T_new_z = l_b_temp 
    T.x = T_new_x
    T.y = T_new_y
    T.z = T_new_z
    
    print(f"DEBUG DBL T_new x: {fq12_to_hex_fp2(T.x)}")
    print(f"DEBUG DBL T_new y: {fq12_to_hex_fp2(T.y)}")
    print(f"DEBUG DBL T_new z: {fq12_to_hex_fp2(T.z)}")
    
    py_fp2 = FQ2([P.y, FQ(0)])
    l_b = l_b * py_fp2
    px_fp2 = FQ2([P.x, FQ(0)])
    l_c = l_c * px_fp2
    
    print(f"DEBUG: l_a = {l_a.coeffs[0].n:x}, {l_a.coeffs[1].n:x}")
    print(f"DEBUG: l_b = {l_b.coeffs[0].n:x}, {l_b.coeffs[1].n:x}")
    print(f"DEBUG: l_c = {l_c.coeffs[0].n:x}, {l_c.coeffs[1].n:x}")
    
    zero_fq2 = FQ2([FQ(0), FQ(0)])
    l = fq12_from_components(l_b, l_c, zero_fq2, zero_fq2, l_a, zero_fq2)
    return f * l

# Line Function for Addition
def line_func_add(f, R, Q, P):
    t1 = R.z * Q.x
    t2 = R.z * Q.y
    t1 = R.x - t1
    t2 = R.y - t2
    t3 = t1 * t1
    R_new_x_temp = t3 * R.x
    t4 = t2 * t2
    t3 = t3 * t1
    t4 = t4 * R.z
    t4 = t4 + t3
    t4 = t4 - R_new_x_temp
    t4 = t4 - R_new_x_temp
    R_new_x = R_new_x_temp - t4
    T1 = t2 * R_new_x
    T2 = t3 * R.y
    T2 = T1 - T2
    R_new_y = T2
    R_new_x_final = t1 * t4
    R_new_z = t3 * R.z
    R.x = R_new_x_final
    R.y = R_new_y
    R.z = R_new_z
    l_c = -t2
    T1 = t2 * Q.x
    T2 = t1 * Q.y
    T1 = T1 - T2
    l_a = T1
    l_b = t1
    py_fp2 = FQ2([P.y, FQ(0)])
    l_b = l_b * py_fp2
    px_fp2 = FQ2([P.x, FQ(0)])
    l_c = l_c * px_fp2
    
    print(f"DEBUG ADD: l_a = {l_a.coeffs[0].n:x}, {l_a.coeffs[1].n:x}")
    print(f"DEBUG ADD: l_b = {l_b.coeffs[0].n:x}, {l_b.coeffs[1].n:x}")
    print(f"DEBUG ADD: l_c = {l_c.coeffs[0].n:x}, {l_c.coeffs[1].n:x}")

    print(f"DEBUG ADD R x: {fq12_to_hex_fp2(R.x)}")
    print(f"DEBUG ADD R y: {fq12_to_hex_fp2(R.y)}")
    print(f"DEBUG ADD R z: {fq12_to_hex_fp2(R.z)}")
    
    zero_fq2 = FQ2([FQ(0), FQ(0)])
    l = fq12_from_components(l_b, l_c, zero_fq2, zero_fq2, l_a, zero_fq2)
    return f * l

def trace_miller_loop(P, Q):
    f = FQ12.one()
    T = PointG2(Q.x, Q.y, FQ2([FQ(1), FQ(0)]))
    loop_param = 29793968203157093288
    print("Starting Python Miller Loop Trace...")
    for i in range(64, -1, -1):
        f = f * f
        f = line_func_dbl(f, T, P)
        print(f"Step {i} after DBL:")
        print(fq12_to_hex(f))
        bit = (loop_param >> i) & 1
        if bit:
            f = line_func_add(f, T, Q, P)
            print(f"Step {i} after ADD:")
            print(fq12_to_hex(f))
    print("Trace finished loop")
    return f

print("Starting Python Trace...")
print(f"Type G1[0]: {type(G1[0])}")
print(f"Type G2[0]: {type(G2[0])}")
p_pt = PointG1(G1[0], G1[1])
q_pt = PointG2(G2[0], G2[1], FQ2([FQ(1), FQ(0)]))
try:
    final_f = trace_miller_loop(p_pt, q_pt)
    
    print("Applying final_exponentiate...")
    final_f = final_exponentiate(final_f)
    
    # Verify against py_ecc pairing
    print("Calling py_ecc pairing...")
    expected = pairing(G2, G1)
    print("Pairing done.")
    sys.stdout.flush()
    print("Expected (py_ecc):")
    print(fq12_to_hex(expected))
    
    if final_f == expected:
        print("Python Trace matches py_ecc pairing!")
    else:
        print("Python Trace DOES NOT match py_ecc pairing!")

    print("My Manual Miller Loop Result:")
    print(fq12_to_hex(final_f))
except BaseException as e:
    import traceback
    print("EXCEPTION CAUGHT:")
    traceback.print_exc(file=sys.stdout)
print("Python Trace Done.")
