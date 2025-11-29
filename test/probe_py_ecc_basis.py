from py_ecc.bn128 import FQ, FQ2, FQ12

def print_fq12(name, x):
    s = ""
    for c in x.coeffs:
        if c == FQ(0): s += "0, "
        elif c == FQ(1): s += "1, "
        else: s += "X, "
    print(f"{name}: [{s}]")

# Construct 1
one = FQ12.one()
print_fq12("1", one)

# Find w. Usually w is the element such that Fp12 = Fp6[w].
# In py_ecc, FQ12 is extension of FQ.
# Let's find the "generator" or base element.
# coeffs[1] = 1?
coeffs_w = [FQ(0)] * 12
coeffs_w[1] = FQ(1) # If basis is 1, i, ... ?
# Wait, FQ12 is 12 coeffs.
# Is it 1, u, u^2... u^11?
# Or 1, w, w^2... with w in Fp2?

# Let's try to construct an element with 1 at index 1.
coeffs = [FQ(0)] * 12
coeffs[1] = FQ(1)
w = FQ12(coeffs)
print_fq12("Element at index 1", w)

# Square it
w2 = w * w
print_fq12("Square of index 1", w2)

# If index 1 is w, then w^2 should be index 2?
# If basis is 1, w, w^2...

# Try index 2
coeffs = [FQ(0)] * 12
coeffs[2] = FQ(1)
v = FQ12(coeffs)
print_fq12("Element at index 2", v)

# Square index 1
# Check if it equals index 2
if w2 == v:
    print("Index 1 squared equals Index 2. Suggests power basis 1, x, x^2...")
else:
    print("Index 1 squared does NOT equal Index 2.")

# Let's try to match C basis:
# C: c0.c0 (1), c0.c1 (v), c0.c2 (v^2), c1.c0 (w), c1.c1 (wv), c1.c2 (wv^2)
# where w^2 = v.
# So C basis: 1, w^2, w^4, w, w^3, w^5.

# If Py uses 1, w, w^2, w^3, w^4, w^5.
# Then Py Index 1 is w. Py Index 2 is w^2.
# If C Index 1 is w^2.
# Then C Index 1 should map to Py Index 2.



