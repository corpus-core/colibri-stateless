from py_ecc.bn128 import FQ12, FQ2, FQ

def find_w():
    # Try setting 1 at each index
    for i in range(12):
        coeffs = [0] * 12
        coeffs[i] = 1
        x = FQ12(coeffs)
        
        # Compute x^6
        y = x ** 6
        
        print(f"Index {i}: x^6 = {y}")
        
        if y.coeffs[0] == 9 and y.coeffs[1] == 1 and all(c == 0 for c in y.coeffs[2:]):
            print(f"Index {i} corresponds to w!")
            return

find_w()
