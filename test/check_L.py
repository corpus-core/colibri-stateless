from py_ecc.bn128 import G1, add, multiply, curve_order, field_modulus, is_on_curve, FQ
import sys

sys.setrecursionlimit(5000)

# Constants from zk_verifier_constants.h (Big Endian)
VK_PROGRAM_HASH = 0x00a61ad8347fe889261a355403eaef5795d3d6adf039126d55da3fe9aa9f2a54

VK_IC0_X = 0x26091e1cafb0ad8a4ea0a694cd3743ebf524779233db734c451d28b58aa9758e
VK_IC0_Y = 0x009ff50a6b8b11c3ca6fdb2690a124f8ce25489fefa65a3e782e7ba70b66690e
IC0 = (FQ(VK_IC0_X), FQ(VK_IC0_Y))

VK_IC1_X = 0x061c3fd0fd3da25d2607c227d090cca750ed36c6ec878755e537c1c48951fb4c
VK_IC1_Y = 0x0fa17ae9c2033379df7b5c65eff0e107055e9a273e6119a212dd09eb51707219
IC1 = (FQ(VK_IC1_X), FQ(VK_IC1_Y))

VK_IC2_X = 0x04eab241388a79817fe0e0e2ead0b2ec4ffdec51a16028dee020634fd129e71c
VK_IC2_Y = 0x07236256d21c60d02f0bdbf95cff83e03ea9e16fca56b18d5544b0889a65c1f5
IC2 = (FQ(VK_IC2_X), FQ(VK_IC2_Y))

# From C Log
PUB_HASH = 0x1ddc001001f1d8cdecd432c67ab65e899ed1b97a5753f3b2116699f736161ffb
C_L_HEX = "089f81c788b51670ac0b2c9124928d8d3bf27090e8286e6fc33e590ad0bea7f122854128324f79c69feee580970d1634b05341d69e5240f6867909886c032826"

def main():
    print(f"Field Modulus: {field_modulus}")
    
    # Manual Check IC0
    x, y = IC0
    # FQ objects define equality based on value mod p
    print(f"IC0 valid (FQ): {y*y == x*x*x + FQ(3)}")
    
    print(f"Program Hash (vkey): {hex(VK_PROGRAM_HASH)}")
    print(f"Pub Hash: {hex(PUB_HASH)}")

    try:
        # L = IC0 + vkey * IC1 + pub * IC2
        t1 = multiply(IC1, VK_PROGRAM_HASH)
        t2 = multiply(IC2, PUB_HASH)
        
        L = add(IC0, t1)
        L = add(L, t2)
        
        print(f"Calculated L: {L}")
        
        # Parse C output
        c_l_x = int(C_L_HEX[:64], 16)
        c_l_y = int(C_L_HEX[64:], 16)
        print(f"C Output L: ({c_l_x}, {c_l_y})")
        
        if L[0] == FQ(c_l_x) and L[1] == FQ(c_l_y):
            print("SUCCESS: L matches!")
        else:
            print("FAILURE: L mismatch!")
            print(f"Diff X: {L[0]} vs {c_l_x}")
            print(f"Diff Y: {L[1]} vs {c_l_y}")
            
    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    main()
