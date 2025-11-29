from py_ecc.bn128 import FQ, FQ2, field_modulus

xi = FQ2([9, 1])
exp1 = (field_modulus - 1) // 3
exp2 = (field_modulus - 1) // 2

xi_p_3 = xi ** exp1
xi_p_2 = xi ** exp2

def print_c_array(val, name):
    # val is FQ.
    # Print as 32 bytes hex
    h = hex(val.n)[2:]
    if len(h) < 64:
        h = '0' * (64 - len(h)) + h
    
    print(f"static const uint8_t {name}[] = {{")
    bytes_list = [h[i:i+2] for i in range(0, 64, 2)]
    for i in range(0, 32, 16):
        line = ", ".join("0x" + b for b in bytes_list[i:i+16])
        print(f"    {line},")
    print("};")

print("// XI_P_3 (xi^((p-1)/3))")
print_c_array(xi_p_3.coeffs[0], "XI_P_3_RE") # c0
print_c_array(xi_p_3.coeffs[1], "XI_P_3_IM") # c1

print("// XI_P_2 (xi^((p-1)/2))")
print_c_array(xi_p_2.coeffs[0], "XI_P_2_RE")
print_c_array(xi_p_2.coeffs[1], "XI_P_2_IM")


