from py_ecc.bn128 import G1, G2, pairing, FQ, FQ2, FQ12, field_modulus
import sys

sys.setrecursionlimit(50000)

# Q from C test
q_x_im = 0x198e9393920d483a7260bfb731fb5d25f1aa493335a9e71297e485b7aef312c2
q_x_re = 0x1800deef121f1e76426a00665e5c4479674322d4f75edadd46debd5cd992f6ed
q_y_im = 0x090689d0585ff075ec9e99ad690c3395bc4b313370b38ef355acdadcd122975b
q_y_re = 0x12c85ea5db8c6deb4aab71808dcb408fe3d1e7690c43d37b4ce6cc0166fa7daa

Q_test = (
    FQ2([q_x_re, q_x_im]),
    FQ2([q_y_re, q_y_im])
)

P_test = (FQ(1), FQ(2))
NegP_test = (FQ(1), FQ(field_modulus - 2))

print("Comparing Q...")
if Q_test == G2:
    print("Q matches py_ecc G2!")
else:
    print("Q does NOT match py_ecc G2")
    print(f"Q_test: {Q_test}")
    print(f"G2:     {G2}")

print("Running pairing check...")
try:
    # Calculate pairing e(P, Q) * e(-P, Q)
    # py_ecc pairing is pairing(G2, G1)
    pair1 = pairing(Q_test, P_test)
    print("Pairing 1 done.")
    pair2 = pairing(Q_test, NegP_test)
    print("Pairing 2 done.")
    result = pair1 * pair2
    
    print(f"Result == 1? {result == FQ12.one()}")
except RecursionError:
    print("RecursionError caught!")
except Exception as e:
    print(f"Error: {e}")
