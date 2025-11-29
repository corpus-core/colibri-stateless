import sys
from py_ecc.bn128 import bn128_curve, FQ, FQ2

# Constants
modulus = 21888242871839275222246405745257275088696311157297823662689037894645226208583

# Helper to parse hex string to int
def hex2int(h):
    return int(h.replace(" ", "").replace("\n", ""), 16)

# Inputs from C logs (Step 1 ADD)
# p->x: 2f59133021e8d29c06970272cc6b846f324813adfa27de52d5b5bc63c14247f6, 04e1badea162ab869b0ff9b2dafdfc742515b5c5c67477aa71c08d70e5d1f588
px_c0 = hex2int("2f59133021e8d29c06970272cc6b846f324813adfa27de52d5b5bc63c14247f6")
px_c1 = hex2int("04e1badea162ab869b0ff9b2dafdfc742515b5c5c67477aa71c08d70e5d1f588")

# p->y: 205d04965ffdb09577a1631dad84eb1694e1d70de643a93a61feb453969f33db, 091792c42e7fd5fde8da995919ef4cd557311c70f5f9fbbe336a744ac0f4bfc4
py_c0 = hex2int("205d04965ffdb09577a1631dad84eb1694e1d70de643a93a61feb453969f33db")
py_c1 = hex2int("091792c42e7fd5fde8da995919ef4cd557311c70f5f9fbbe336a744ac0f4bfc4")

# p->z: 0b239a63a05c83c1d4fe3b50e91436efc35522951cf6458e929329202f521d9d, 2f397eb2756b097a37daa08a01ab530b9c9c35d9f450b4df96e604b01acc7249
pz_c0 = hex2int("0b239a63a05c83c1d4fe3b50e91436efc35522951cf6458e929329202f521d9d")
pz_c1 = hex2int("2f397eb2756b097a37daa08a01ab530b9c9c35d9f450b4df96e604b01acc7249")

# q->x: 30363c09c5fcc01c3f5d300534b47a7a4f0390bd7e1a8cbec942f81c5ae9af5d, 0c2a02ff3f98b4c1c406228fdf45158795144f978887dd1133680719851e5eda
qx_c0 = hex2int("30363c09c5fcc01c3f5d300534b47a7a4f0390bd7e1a8cbec942f81c5ae9af5d")
qx_c1 = hex2int("0c2a02ff3f98b4c1c406228fdf45158795144f978887dd1133680719851e5eda")

# q->y (from yQ in log): 1dc3f46b40c711f5c6a74083b54ac7a6ad6b469342b4080de759da9b83e78d72, 2fcee692ab4e54d1f8157320419655b49a0ed035ae613fb66983486379a4b7c8
qy_c0 = hex2int("1dc3f46b40c711f5c6a74083b54ac7a6ad6b469342b4080de759da9b83e78d72")
qy_c1 = hex2int("2fcee692ab4e54d1f8157320419655b49a0ed035ae613fb66983486379a4b7c8")

def to_fq2(c0, c1):
    return FQ2([c0, c1])

P_x = to_fq2(px_c0, px_c1)
P_y = to_fq2(py_c0, py_c1)
P_z = to_fq2(pz_c0, pz_c1)

Q_x = to_fq2(qx_c0, qx_c1)
Q_y = to_fq2(qy_c0, qy_c1)

# Simulation of bn254_g2_add_mixed
def add_mixed(p_x, p_y, p_z, q_x, q_y):
    Z1Z1 = p_z * p_z
    Z1Z1Z1 = Z1Z1 * p_z
    
    U2 = q_x * Z1Z1
    S2 = q_y * Z1Z1Z1
    
    H = U2 - p_x
    r = S2 - p_y
    
    print(f"H: {int(H.coeffs[0]):x}, {int(H.coeffs[1]):x}")
    print(f"r: {int(r.coeffs[0]):x}, {int(r.coeffs[1]):x}")
    
    I = H * H
    J = H * I
    
    V = p_x * I
    
    print(f"J: {int(J.coeffs[0]):x}, {int(J.coeffs[1]):x}")
    print(f"V: {int(V.coeffs[0]):x}, {int(V.coeffs[1]):x}")
    
    # X3 = r^2 - J - 2V
    X3 = r * r - J - V - V
    
    # Y3 = r(V - X3) - Y1 * J
    T1 = V - X3
    T2 = r * T1
    
    # Debug T3 calculation
    # T3 = p_y * J
    # T3_c0 = y0*j0 - y1*j1
    # T3_c1 = y0*j1 + y1*j0
    t3_a = p_y.coeffs[0] * J.coeffs[0]
    t3_b = p_y.coeffs[1] * J.coeffs[1]
    t3_c = p_y.coeffs[0] * J.coeffs[1]
    t3_d = p_y.coeffs[1] * J.coeffs[0]
    
    print(f"T3 components:")
    print(f"y0*j0: {int(t3_a):x}")
    print(f"y1*j1: {int(t3_b):x}")
    print(f"y0*j1: {int(t3_c):x}")
    print(f"y1*j0: {int(t3_d):x}")
    
    T3 = p_y * J
    Y3 = T2 - T3

    print(f"T1: {int(T1.coeffs[0]):x}, {int(T1.coeffs[1]):x}")
    print(f"T2: {int(T2.coeffs[0]):x}, {int(T2.coeffs[1]):x}")
    print(f"T3: {int(T3.coeffs[0]):x}, {int(T3.coeffs[1]):x}")
    
    # Z3 = Z1 * H
    Z3 = p_z * H
    
    return X3, Y3, Z3

print("--- PYTHON CALCULATION ---")
X3, Y3, Z3 = add_mixed(P_x, P_y, P_z, Q_x, Q_y)
print(f"X3: {int(X3.coeffs[0]):x}, {int(X3.coeffs[1]):x}")
print(f"Y3: {int(Y3.coeffs[0]):x}, {int(Y3.coeffs[1]):x}")
print(f"Z3: {int(Z3.coeffs[0]):x}, {int(Z3.coeffs[1]):x}")
