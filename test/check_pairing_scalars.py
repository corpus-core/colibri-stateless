# ... imports ...
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

# Points and Constants
A_HEX = "1a78f6839bb5d88d1674dc0bb7231aef8ad32aded0418bcd3c0e8186365a448f1d1160f7903c238c2ac8a24938448337620e9e20c1511edb61e14766a8aa3569"
B_HEX = "0c2a02ff3f98b4c1c406228fdf45158795144f978887dd1133680719851e5eda30363c09c5fcc01c3f5d300534b47a7a4f0390bd7e1a8cbec942f81c5ae9af5d2fcee692ab4e54d1f8157320419655b49a0ed035ae613fb66983486379a4b7c81dc3f46b40c711f5c6a74083b54ac7a6ad6b469342b4080de759da9b83e78d72"
C_HEX = "223d4000ce77907532337b2cf2f06c16acd4bfae8690c6b8cbfde4d40a3c2715019eba6ef839006a33148941acac52e2e13b2220a3e698f1771868282699cc34"

DELTA_HEX = "1cc7cb8de715675f21f01ecc9b46d236e0865e0cc020024521998269845f74e603ff41f4ba0c37fe2caf27354d28e4b8f83d3b76777a63b327d736bffb0122ed01909cd7827e0278e6b60843a4abc7b111d7f8b2725cd5902a6b20da7a2938fb192bd3274441670227b4f69a44005b8711266e474227c6439ca25ca8e1ec1fc2"
BETA_HEX = "0967032fcbf776d1afc985f88877f182d38480a653f2decaa9794cbc3bf3060c0e187847ad4c798374d0d6732bf501847dd68bc0e071241e0213bc7fc13db7ab001752a100a72fdf1e5a5d6ea841cc20ec838bccfcf7bd559e79f1c9c759b6a0192a8cc13cd9f762871f21e43451c6ca9eeab2cb2987c4e366a185c25dac2e7f"
GAMMA_HEX = "198e9393920d483a7260bfb731fb5d25f1aa493335a9e71297e485b7aef312c21800deef121f1e76426a00665e5c4479674322d4f75edadd46debd5cd992f6ed275dc4a288d1afb3cbb1ac09187524c7db36395df7be3b99e673b13a075a65ec1d9befcd05a5323e6da4d435f3b617cdb3af83285c2df711ef39c01571827f9d"

ALPHA_X = 0x2d4d9aa7e302d9df41749d5507949d05dbea33fbb16c643b22f599a2be6df2e2
ALPHA_Y = 0x14bedd503c37ceb061d8ec60209fe345ce89830a19230301f076caff004d1926

# Scalars (Bytes from C header)
VK_PROGRAM_HASH_BYTES = bytes.fromhex("00a61ad8347fe889261a355403eaef5795d3d6adf039126d55da3fe9aa9f2a54")
# PUB_HASH from C log (1ddc001001f1d8cdecd432c67ab65e899ed1b97a5753f3b2116699f736161ffb)
# This matches SHA256 output. SHA256 output is BE.
# But we used it as scalar. 
# If we should treat it as LE, we need to reverse it.
PUB_HASH_BYTES = bytes.fromhex("1ddc001001f1d8cdecd432c67ab65e899ed1b97a5753f3b2116699f736161ffb")

# IC Points
VK_IC0_X = 0x26091e1cafb0ad8a4ea0a694cd3743ebf524779233db734c451d28b58aa9758e
VK_IC0_Y = 0x009ff50a6b8b11c3ca6fdb2690a124f8ce25489fefa65a3e782e7ba70b66690e
VK_IC1_X = 0x061c3fd0fd3da25d2607c227d090cca750ed36c6ec878755e537c1c48951fb4c
VK_IC1_Y = 0x0fa17ae9c2033379df7b5c65eff0e107055e9a273e6119a212dd09eb51707219
VK_IC2_X = 0x04eab241388a79817fe0e0e2ead0b2ec4ffdec51a16028dee020634fd129e71c
VK_IC2_Y = 0x07236256d21c60d02f0bdbf95cff83e03ea9e16fca56b18d5544b0889a65c1f5

def parse_g1(hex_str):
    x = int(hex_str[:64], 16)
    y = int(hex_str[64:], 16)
    return (FQ(x), FQ(y))

def parse_g2_eth(hex_str):
    x_im = int(hex_str[:64], 16)
    x_re = int(hex_str[64:128], 16)
    y_im = int(hex_str[128:192], 16)
    y_re = int(hex_str[192:], 16)
    return (FQ2([x_re, x_im]), FQ2([y_re, y_im]))

def main():
    try:
        A = parse_g1(A_HEX)
        C = parse_g1(C_HEX)
        ALPHA = (FQ(ALPHA_X), FQ(ALPHA_Y))
        
        B = parse_g2_eth(B_HEX)
        DELTA = parse_g2_eth(DELTA_HEX)
        BETA = parse_g2_eth(BETA_HEX)
        GAMMA = parse_g2_eth(GAMMA_HEX)
        
        IC0 = (FQ(VK_IC0_X), FQ(VK_IC0_Y))
        IC1 = (FQ(VK_IC1_X), FQ(VK_IC1_Y))
        IC2 = (FQ(VK_IC2_X), FQ(VK_IC2_Y))
        
        # Try BE scalars
        s1_be = int.from_bytes(VK_PROGRAM_HASH_BYTES, 'big')
        s2_be = int.from_bytes(PUB_HASH_BYTES, 'big')
        
        # Try LE scalars
        s1_le = int.from_bytes(VK_PROGRAM_HASH_BYTES, 'little')
        s2_le = int.from_bytes(PUB_HASH_BYTES, 'little')
        
        # Compute L with combinations
        opts = [(s1_be, s2_be, "BE/BE"), (s1_le, s2_le, "LE/LE"), (s1_be, s2_le, "BE/LE"), (s1_le, s2_be, "LE/BE")]
        
        p1 = pairing(B, A)
        p2 = pairing(DELTA, C)
        p3 = pairing(BETA, ALPHA)
        
        for s1, s2, lbl in opts:
            # L = IC0 + s1*IC1 + s2*IC2
            t1 = multiply(IC1, s1)
            t2 = multiply(IC2, s2)
            L_test = add(IC0, add(t1, t2))
            
            print(f"Computed L for {lbl}: {L_test}")
            
            p4 = pairing(GAMMA, L_test)
            
            res = p1 * p2 * p3 * p4
            
            if res == FQ12.one():
                print(f"SUCCESS: Match found with scalar mode {lbl}")
                return
        
        print("FAILURE: No scalar endianness combination worked.")

    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    main()

