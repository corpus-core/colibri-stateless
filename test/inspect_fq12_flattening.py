from py_ecc.bn128 import FQ12, FQ2, FQ, field_modulus

def check_mapping():
    print("Checking mapping logic...")
    
    # Case 1: Construct 'u' (i.e. sqrt(-1))
    # In C: c00 = u, all others 0.
    # a00 = 0, b00 = 1.
    # Predicted coeffs:
    # coeffs[0] = -9
    # coeffs[6] = 1
    coeffs_u = [0] * 12
    coeffs_u[0] = -9 % field_modulus
    coeffs_u[6] = 1
    
    val_u = FQ12(coeffs_u)
    
    # Calculate u^2. Should be -1.
    val_sq = val_u * val_u
    print(f"u^2: {val_sq}")
    
    if val_sq == FQ12.one() * -1:
        print("SUCCESS: Mapping for 'u' is consistent.")
    else:
        print("FAILURE: Mapping for 'u' is inconsistent.")

    # Case 2: Construct 'w' (generator)
    # In C: c10 = 1, all others 0.
    # a10 = 1, b10 = 0.
    # Predicted coeffs:
    # coeffs[1] = 1
    coeffs_w = [0] * 12
    coeffs_w[1] = 1
    
    val_w = FQ12(coeffs_w)
    
    # Calculate w^6. Should be 9+u.
    val_w6 = val_w ** 6
    print(f"w^6: {val_w6}")
    
    # 9+u in coeffs:
    # 9 -> coeffs[0] = 9
    # u -> coeffs[0] = -9, coeffs[6] = 1
    # Sum: coeffs[0] = 0, coeffs[6] = 1 ?
    # Wait. 9 is a scalar.
    # Scalar 9 is just coeffs[0]=9.
    # u is coeffs[0]=-9, coeffs[6]=1.
    # So 9+u should be coeffs[0]=0, coeffs[6]=1.
    
    if val_w6.coeffs[0] == 0 and val_w6.coeffs[6] == 1:
         print("SUCCESS: Mapping for 'w' is consistent.")
    else:
         print("FAILURE: Mapping for 'w' is inconsistent.")
         print(f"Expected coeffs[0]=0, coeffs[6]=1")
         print(f"Got coeffs[0]={val_w6.coeffs[0]}, coeffs[6]={val_w6.coeffs[6]}")

check_mapping()
