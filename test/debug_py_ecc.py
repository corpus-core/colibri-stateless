from py_ecc.bn128 import FQ, FQ2, FQ12, G1, G2, pairing
from py_ecc.bn128.bn128_pairing import miller_loop
import sys
sys.setrecursionlimit(20000)

def fq12_to_hex(f):
    # ... (same as before)
    def get_fp2(low_idx, high_idx):
        b_val = f.coeffs[high_idx]
        a_val = f.coeffs[low_idx] + FQ(9) * b_val
        return a_val, b_val

    s = ""
    # c00
    a, b_val = get_fp2(0, 6)
    s += f"{int(a):064x}{int(b_val):064x}"
    return s # Truncated for brevity

print("Running pairing(G2, G1)...")
try:
    res = pairing(G2, G1)
    print("Pairing Result: Success")
except Exception as e:
    import traceback
    traceback.print_exc()

print("Running miller_loop(G2, G1)...")
try:
    res = miller_loop(G2, G1)
    print("Miller Result: Success")
except Exception as e:
    import traceback
    traceback.print_exc()
print("Done.")
