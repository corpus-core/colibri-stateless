from py_ecc.bn128 import FQ2, FQ

def check_fp2():
    # FQ2([c0, c1]) corresponds to c0 + c1 * i
    one = FQ2([1, 0])
    i = FQ2([0, 1])
    
    i_sq = i * i
    print(f"i^2 = {i_sq}")
    
    if i_sq == FQ2([field_modulus - 1, 0]):
        print("i^2 = -1 (Standard)")
    else:
        print("i^2 != -1")

    # Check arbitrary multiplication
    a = FQ2([2, 3])
    b = FQ2([4, 5])
    res = a * b
    # (2+3i)(4+5i) = 8 + 10i + 12i + 15i^2 = 8 + 22i - 15 = -7 + 22i
    print(f"(2+3i)*(4+5i) = {res}")
    expected = FQ2([(-7) % field_modulus, 22])
    if res == expected:
        print("Multiplication matches standard complex arithmetic")
    else:
        print("Multiplication differs")

from py_ecc.bn128 import field_modulus
check_fp2()


