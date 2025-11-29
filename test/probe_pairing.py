from py_ecc.optimized_bn128 import G1, G2, optimized_pairing

print("Running optimized_pairing(G2, G1)...")
try:
    res = optimized_pairing.pairing(G2, G1)
    print("Success!")
except Exception as e:
    print(f"Failed: {e}")


