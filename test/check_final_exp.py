import sys
import os

# Add the project root to the python path
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '../../')))

from py_ecc.bn128 import FQ12, FQ, FQ2, field_modulus, curve_order, bn128_pairing

def print_fq12(label, f):
    print(f"{label}:", end=" ")
    # Print in same order as C: c0.c0, c0.c1, c0.c2, c1.c0, c1.c1, c1.c2
    # But wait, C struct is:
    # fp12 { fp6 c0, fp6 c1 }
    # fp6 { fp2 c0, fp2 c1, fp2 c2 }
    # fp2 { fp c0, fp c1 }
    
    # py_ecc FQ12 coeffs are 12 integers.
    # Map them:
    # c0 = f.coeffs[0] (Fp2)
    # c1 = f.coeffs[1] (Fp2)
    # ...
    # Wait, FQ12 in py_ecc is extension of FQ2 via FQ6?
    # FQ12 = FQ12(coeffs) where coeffs is list of 12 ints? Or FQ6s?
    # Actually py_ecc FQ12 is over FQ. It has 12 coeffs.
    # The basis is likely w^0..w^11?
    # Or constructed recursively?
    
    # Let's trust my previous reconstruction logic which worked for other values.
    # The C layout:
    # c0 = (c00, c01, c02) = (c000, c001), (c010, c011), (c020, c021)
    # c1 = (c10, c11, c12) = ...
    
    # In check_loop62_sqr.py I used:
    # f_poly = FQ12([
    #     c000, c100, c010, c110, c020, c120,  # c0 (real part of Fp12?)
    #     c001, c101, c011, c111, c021, c121   # c1 (imaginary part of Fp12?)
    # ])
    # BUT this mapping was derived for matching the specific trace.
    
    # For FQ12 print, let's just print all 12 coeffs in standard py_ecc order
    # and we can try to match visually or I can reconstruct standard hex format.
    
    # Let's use the format I used for reconstruction to print it back out?
    # No, simpler: just print the FQ12 coeffs as is.
    # We know C order is:
    # c0.c0.c0, c0.c0.c1, c0.c1.c0, c0.c1.c1, c0.c2.c0, c0.c2.c1
    # c1.c0.c0, c1.c0.c1, c1.c1.c0, c1.c1.c1, c1.c2.c0, c1.c2.c1
    
    # In py_ecc FQ12 is:
    # c0 + c1 * w + c2 * w^2 ... + c11 * w^11
    # where w is Fp12 generator.
    
    # My C implementation uses Tower extension:
    # Fp2 = Fp[u] / (u^2 + 1)
    # Fp6 = Fp2[v] / (v^3 - xi) where xi = 9+u
    # Fp12 = Fp6[w] / (w^2 - v)
    
    # Elements are represented as c0 + c1*w where c0, c1 in Fp6.
    # c0 = c00 + c01*v + c02*v^2
    # c1 = c10 + c11*v + c12*v^2
    
    # So we have coefficients for:
    # 1, v, v^2, w, w*v, w*v^2
    # 1, u
    
    # Let's assume the C print output corresponds to the memory layout.
    # I will just print py_ecc's native representation and we can deduce.
    
    # Better: map py_ecc FQ12 back to C components if possible.
    # FQ12 coeffs in py_ecc are coefficients of powers of something.
    # Let's print them all.
    
    # Actually, I want to compare with C output.
    # C output: c00, c01, ... c11
    # These are Fp elements (integers).
    
    # Let's assume my reconstruction mapping was correct.
    # coeffs[0] -> c0.c0.c0
    # coeffs[1] -> c1.c0.c0
    # coeffs[2] -> c0.c1.c0
    # coeffs[3] -> c1.c1.c0
    # coeffs[4] -> c0.c2.c0
    # coeffs[5] -> c1.c2.c0
    # coeffs[6] -> c0.c0.c1
    # coeffs[7] -> c1.c0.c1
    # coeffs[8] -> c0.c1.c1
    # coeffs[9] -> c1.c1.c1
    # coeffs[10] -> c0.c2.c1
    # coeffs[11] -> c1.c2.c1
    
    # Wait, this mapping seems interleaved.
    
    vals = [int(c.n) for c in f.coeffs]
    
    # C order:
    # c0.c0.c0 (real of v0 of w0)
    # c0.c0.c1 (imag of v0 of w0)
    # c0.c1.c0
    # c0.c1.c1
    # c0.c2.c0
    # c0.c2.c1
    # c1.c0.c0
    # c1.c0.c1
    # c1.c1.c0
    # c1.c1.c1
    # c1.c2.c0
    # c1.c2.c1
    
    # My reconstruction used:
    # c000 = coeffs[0]
    # c100 = coeffs[1]
    # c010 = coeffs[2]
    # ...
    
    # If I print using this inverse mapping, I can compare with C.
    
    # Map from py_ecc coeffs index to C index (0..11)
    # c0.c0.c0 is coeffs[0]
    # c0.c0.c1 is coeffs[6]
    # c0.c1.c0 is coeffs[2]
    # c0.c1.c1 is coeffs[8]
    # c0.c2.c0 is coeffs[4]
    # c0.c2.c1 is coeffs[10]
    
    # c1.c0.c0 is coeffs[1]
    # c1.c0.c1 is coeffs[7]
    # c1.c1.c0 is coeffs[3]
    # c1.c1.c1 is coeffs[9]
    # c1.c2.c0 is coeffs[5]
    # c1.c2.c1 is coeffs[11]
    
    c_vals = [0] * 12
    c_vals[0] = vals[0]
    c_vals[1] = vals[6]
    c_vals[2] = vals[2]
    c_vals[3] = vals[8]
    c_vals[4] = vals[4]
    c_vals[5] = vals[10]
    
    c_vals[6] = vals[1]
    c_vals[7] = vals[7]
    c_vals[8] = vals[3]
    c_vals[9] = vals[9]
    c_vals[10] = vals[5]
    c_vals[11] = vals[11]
    
    for v in c_vals:
        print(f"{v:x}", end=" ")
    print("")

def reconstruct_f_easy():
    # Reconstruct f from C log (f_easy)
    # 13|DEBUG f_easy: 0ed1f35f335e04772377aae72d0d4de7fe92afd3a2317270f9189aff102d9c08 0888db44e812d1d0f6e680308c3134d80a3bb726abe725047f36a5e763451248 2cb8e26fb098851fd373bd1ba1ca21244cbb5049642a3d80660105b6b6594179 03d14ddcdcc53d18f41a6503e274b46239438a0d0f33fd202c8ecb475e3a3212 2e94cab7f487418fac7025c71a516c25cb589bd6da92dda478a7619a711e3008 1c38c42ed9af9de65cd873702763324f5dbf6613950a1301fbdcf046ba96bbd5 0e79acc9211de9bfb59e9d23469ee76c472576e734473c45103c534586fa0d00 2c74beaa28ed9b02fff875c0cd439b46b0ce45415ab75978816e9e91b4c2ac2c 05f274a3467b583c9e728acee71fef3930d14f7183877cfb9cf0f31ab270267f 27f3f42a7f71fbe747c5c75dd96db91aeb7c816bc3bb71ad835d5299ed29566b 11c0527d00a11a68cb9cd12a88aaee9668025b0a3fbbed0ff16a03c2d3696e4c 1cbbf615425cf9f8e38e8ec249d0dd12a66dee6ae3105a7b658ce808ec8b6e65 
    
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

def hard_part(f):
    u = 4965661367192848881
    
    x = f
    
    # b = x^u
    b = x ** u
    print_fq12("DEBUG FE x^u", b)
    
    # b = b^2 (x^2u)
    b = b * b
    print_fq12("DEBUG FE x^2u", b)
    
    # a = b^2
    a = b * b
    
    # a = a * b (x^6u)
    a = a * b
    
    # a2 = a^u
    a2 = a ** u
    
    # a = a * a2
    a = a * a2
    
    # a3 = a2^2
    a3 = a2 * a2
    
    # a3 = a3^u
    a3 = a3 ** u
    
    # a = a * a3
    a = a * a3
    print_fq12("DEBUG FE a (part1)", a)
    
    # b (neg) - in C this is just negating c1 (imaginary part of Fp12)
    # In py_ecc, this means negating coeffs[1], coeffs[3], coeffs[5], coeffs[7]...
    # Wait, Fp12 = C0 + C1*w. C1 is coefficients 1, 3, 5, 7, 9, 11 in the interleaved mapping?
    # C1 = c10, c11, c12.
    # c10 = c100 + c101*u
    # c11 = c110 + c111*u
    # c12 = c120 + c121*u
    
    # My reconstruction:
    # c100 = coeffs[1]
    # c101 = coeffs[7]
    # c110 = coeffs[3]
    # c111 = coeffs[9]
    # c120 = coeffs[5]
    # c121 = coeffs[11]
    
    # So yes, indices 1, 3, 5, 7, 9, 11.
    
    # Let's simulate conjugation (negating C1)
    new_coeffs = [x for x in b.coeffs]
    for i in [1, 3, 5, 7, 9, 11]:
        new_coeffs[i] = field_modulus - new_coeffs[i]
    b = FQ12(new_coeffs)
    print_fq12("DEBUG FE b (neg)", b)
    
    # b (mul a)
    b = b * a
    print_fq12("DEBUG FE b (mul a)", b)

    # a2 (mul a)
    a2 = a2 * a
    print_fq12("DEBUG FE a2 (mul a)", a2)

    # a = a^p^2 (frob(frob(a)))
    # a = a ** (field_modulus * field_modulus)
    # Since this is slow, let's implement frob2 logic manually for trace speed if possible?
    # No, stick to correctness.
    a = a ** (field_modulus * field_modulus)
    print_fq12("DEBUG FE a (frob2)", a)

    # a (mul a2)
    a = a * a2
    print_fq12("DEBUG FE a (mul a2)", a)
    
    # a (mul x)
    a = a * x
    print_fq12("DEBUG FE a (part2)", a)
    
    y = x
    # y (neg)
    new_coeffs = [val for val in y.coeffs]
    for i in [1, 3, 5, 7, 9, 11]:
        new_coeffs[i] = field_modulus - new_coeffs[i]
    y = FQ12(new_coeffs)
    
    y = y * b
    
    # b frob
    b = b ** field_modulus
    
    a = a * b
    
    # y frob 3
    y = y ** field_modulus
    y = y ** field_modulus
    y = y ** field_modulus
    
    y = y * a
    
    print_fq12("DEBUG FE y (final)", y)

f = reconstruct_f_easy()
hard_part(f)
