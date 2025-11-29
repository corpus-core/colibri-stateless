from py_ecc.bn128 import G1, G2, pairing
from py_ecc.fields import optimized_bn128_FQ12

# print("Debugging BN254 Pairing with py_ecc")
# print(f"G1: {G1}")
# print(f"G2: {G2}")

# Compute Pairing e(G2, G1)
result = pairing(G2, G1)

# print("Result (Fp12):")
# print(result)

def print_fq12_hex(f):
    # coeffs is list of coeffs for the tower.
    # py_ecc FQ12 structure:
    # FQ12 is extension of FQ2 of degree 6? 
    # Actually py_ecc implementation details:
    # optimized_bn128_FQ12.coeffs
    # It seems to be a polynomial over FQ2?
    
    # Let's just dump the raw integers in order.
    # The order in coeffs usually matches the power of the extension variable.
    # Our C struct:
    # c0 (Fp6) = c0 (Fp2) + c1*v + c2*v^2
    # c1 (Fp6) = ...
    # Fp12 = c0 + c1*w.
    
    # Let's print all coefficients.
    # Assuming they are flattened.
    if hasattr(f, 'coeffs'):
        for c in f.coeffs:
            # c is FQ6? Or FQ2?
            # Let's recurse or just print n.
            if hasattr(c, 'coeffs'):
                for c2 in c.coeffs:
                    if hasattr(c2, 'coeffs'):
                         for c3 in c2.coeffs:
                             print(f"{c3.n:064x}", end="")
                    else:
                        # FQ2 coeffs are FQ (integers)
                        print(f"{c2.n:064x}", end="")
            else:
                print(f"{c.n:064x}", end="")
    print("")

# print_fq12_hex(result)

# Simpler: Just print the integer value of the first coordinate to compare.
# c0.c0.c0 ...
# Let's try to map py_ecc output to our struct.
# py_ecc FQ12 elements are (c0, c1) where c0, c1 are FQ6.
# FQ6 elements are (c0, c1, c2) where c0, c1, c2 are FQ2.
# FQ2 elements are (c0, c1) where c0, c1 are integers.

# Flattening:
# FQ12 = c0 + c1*w
# c0 = x0 + x1*v + x2*v^2
# x0 = a0 + a1*u
# ...
# Our C struct bn254_fp12_t:
# bn254_fp6_t c0;
# bn254_fp6_t c1;
# Matches (c0, c1).
# bn254_fp6_t:
# bn254_fp2_t c0, c1, c2;
# Matches (c0, c1, c2) order?
# bn254_fp2_t:
# bn254_fp_t c0, c1;
# Matches (c0, c1) order?

# Let's print out the flattened list of 12 integers from py_ecc result.
def flatten(obj):
    if hasattr(obj, 'coeffs'):
        res = []
        for c in obj.coeffs:
            res.extend(flatten(c))
        return res
    elif hasattr(obj, 'n'):
        return [obj.n]
    else:
        return [obj] # int

flat = flatten(result)
print("FLAT_RESULT_START")
for x in flat:
    print(f"{x:064x}")
print("FLAT_RESULT_END")
