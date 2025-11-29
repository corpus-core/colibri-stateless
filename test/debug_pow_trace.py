import sys
import os

# Add the project root to the python path
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '../../')))

from py_ecc.bn128 import FQ12, FQ, FQ2, field_modulus, curve_order, bn128_pairing

def reconstruct_f_easy():
    c000 = 0x0ed1f35f335e04772377aae72d0d4de7fe92afd3a2317270f9189aff102d9c08
    c001 = 0x0888db44e812d1d0f6e680308c3134d80a3bb726abe725047f36a5e763451248
    c010 = 0x2cb8e26fb098851fd373bd1ba1ca21244cbb5049642a3d80660105b6b6594179
    c011 = 0x03d14ddcdcc53d18f41a6503e274b46239438a0d0f33fd202c8ecb475e3a3212
    c020 = 0x2e94cab7f487418fac7025c71a516c25cb589bd6da92dda478a7619a711e3008
    c021 = 0x1c38c42ed9af9de65cd873702763324f5dbf6613950a1301fbdcf046ba96bbd5
    c100 = 0x0e79acc9211de9bfb59e9d23469ee76c472576e734473c45103c534586fa0d00
    c101 = 0x2c74beaa28ed9b02fff875c0cd439b46b0ce45415ab75978816e9e91b4c2ac2c
    c110 = 0x05f274a3467b583c9e728acee71fef3930d14f7183877cfb9cf0f31ab270267f
    c111 = 0x27f3f42a7f71fbe747c5c75dd96db91aeb7c816bc3bb71ad835d5299ed29566b
    c120 = 0x11c0527d00a11a68cb9cd12a88aaee9668025b0a3fbbed0ff16a03c2d3696e4c
    c121 = 0x1cbbf615425cf9f8e38e8ec249d0dd12a66dee6ae3105a7b658ce808ec8b6e65

    f_coeffs = [
        c000, c100, c010, c110, c020, c120,
        c001, c101, c011, c111, c021, c121
    ]
    return FQ12(f_coeffs)

def print_c00(label, f):
    # c0.c0.c0 corresponds to coeffs[0]
    val = int(f.coeffs[0].n)
    print(f"{label}: {val:x}")

def trace_pow():
    f = reconstruct_f_easy()
    u = 4965661367192848881
    
    res = FQ12.one()
    base = f
    
    exp = u
    step = 0
    
    print(f"DEBUG START POW u={u:x}")
    
    while exp > 0:
        if exp & 1:
            res = res * base
            print_c00(f"Step {step} MUL (res)", res)
        
        base = base * base
        print_c00(f"Step {step} SQR (base)", base)
        
        exp >>= 1
        step += 1

trace_pow()


