# ... (previous imports and setup) ...
from py_ecc.bn128 import G1, G2, add, multiply, pairing, FQ, FQ2, FQ12, is_on_curve, field_modulus
import sys

sys.setrecursionlimit(50000)

# ... iter_pow ...
def iter_pow(self, other):
    if other == 0:
        return self.__class__.one()
    elif other == 1:
        return self
    if other < 0:
        return (self.__class__.one() / self) ** (-other)
    res = self.__class__.one()
    base = self
    exp = int(other)
    while exp > 0:
        if exp % 2 == 1:
            res = res * base
        base = base * base
        exp //= 2
    return res

FQ.__pow__ = iter_pow
FQ2.__pow__ = iter_pow
FQ12.__pow__ = iter_pow

# Constants
VK_IC1_X = 0x061c3fd0fd3da25d2607c227d090cca750ed36c6ec878755e537c1c48951fb4c
VK_IC1_Y = 0x0fa17ae9c2033379df7b5c65eff0e107055e9a273e6119a212dd09eb51707219
VK_PROGRAM_HASH_BYTES = bytes.fromhex("00a61ad8347fe889261a355403eaef5795d3d6adf039126d55da3fe9aa9f2a54")

def main():
    IC1 = (FQ(VK_IC1_X), FQ(VK_IC1_Y))
    s1 = int.from_bytes(VK_PROGRAM_HASH_BYTES, 'big')
    
    res = multiply(IC1, s1)
    print(f"Scalar: {hex(s1)}")
    print(f"IC1.x: {hex(VK_IC1_X)}")
    print(f"IC1.y: {hex(VK_IC1_Y)}")
    print(f"Expected IC1*s1 X: {hex(res[0].n)}")
    print(f"Expected IC1*s1 Y: {hex(res[1].n)}")

if __name__ == "__main__":
    main()


