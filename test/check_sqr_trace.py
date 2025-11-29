import sys
import os

# Add the project root to the python path
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '../../')))

from py_ecc.bn128 import FQ12, FQ, FQ2, field_modulus

def to_int(hex_str):
    return int(hex_str, 16)

def map_c_to_py(c_vals):
    # c_vals is a dictionary or list of (re, im) tuples for:
    # c00, c10, c01, c11, c02, c12
    # indexed by (i, j) where c_ij corresponds to C struct nesting.
    
    # Keys:
    # c00 -> (c00_re, c00_im)
    # c01 -> ...
    # ...
    
    coeffs = [0] * 12
    
    # k = 0 (c00)
    coeffs[0] = (c_vals['c00'][0] - 9 * c_vals['c00'][1]) % field_modulus
    coeffs[6] = c_vals['c00'][1]
    
    # k = 1 (c10)
    coeffs[1] = (c_vals['c10'][0] - 9 * c_vals['c10'][1]) % field_modulus
    coeffs[7] = c_vals['c10'][1]
    
    # k = 2 (c01)
    coeffs[2] = (c_vals['c01'][0] - 9 * c_vals['c01'][1]) % field_modulus
    coeffs[8] = c_vals['c01'][1]
    
    # k = 3 (c11)
    coeffs[3] = (c_vals['c11'][0] - 9 * c_vals['c11'][1]) % field_modulus
    coeffs[9] = c_vals['c11'][1]
    
    # k = 4 (c02)
    coeffs[4] = (c_vals['c02'][0] - 9 * c_vals['c02'][1]) % field_modulus
    coeffs[10] = c_vals['c02'][1]
    
    # k = 5 (c12)
    coeffs[5] = (c_vals['c12'][0] - 9 * c_vals['c12'][1]) % field_modulus
    coeffs[11] = c_vals['c12'][1]
    
    return FQ12(coeffs)

def map_py_to_c(f):
    # Inverse mapping
    # c_im = coeffs[k+6]
    # c_re = coeffs[k] + 9 * c_im
    
    vals = [int(c.n) for c in f.coeffs]
    c_out = {}
    
    def get_re_im(k):
        im = vals[k+6]
        re = (vals[k] + 9 * im) % field_modulus
        return (re, im)
        
    c_out['c00'] = get_re_im(0)
    c_out['c10'] = get_re_im(1)
    c_out['c01'] = get_re_im(2)
    c_out['c11'] = get_re_im(3)
    c_out['c02'] = get_re_im(4)
    c_out['c12'] = get_re_im(5)
    return c_out

def check_sqr_correctly():
    # Inputs from C trace (f_easy)
    c000 = to_int("0ed1f35f335e04772377aae72d0d4de7fe92afd3a2317270f9189aff102d9c08")
    c001 = to_int("0888db44e812d1d0f6e680308c3134d80a3bb726abe725047f36a5e763451248")
    c010 = to_int("2cb8e26fb098851fd373bd1ba1ca21244cbb5049642a3d80660105b6b6594179")
    c011 = to_int("03d14ddcdcc53d18f41a6503e274b46239438a0d0f33fd202c8ecb475e3a3212")
    c020 = to_int("2e94cab7f487418fac7025c71a516c25cb589bd6da92dda478a7619a711e3008")
    c021 = to_int("1c38c42ed9af9de65cd873702763324f5dbf6613950a1301fbdcf046ba96bbd5")
    
    c100 = to_int("0e79acc9211de9bfb59e9d23469ee76c472576e734473c45103c534586fa0d00")
    c101 = to_int("2c74beaa28ed9b02fff875c0cd439b46b0ce45415ab75978816e9e91b4c2ac2c")
    c110 = to_int("05f274a3467b583c9e728acee71fef3930d14f7183877cfb9cf0f31ab270267f")
    c111 = to_int("27f3f42a7f71fbe747c5c75dd96db91aeb7c816bc3bb71ad835d5299ed29566b")
    c120 = to_int("11c0527d00a11a68cb9cd12a88aaee9668025b0a3fbbed0ff16a03c2d3696e4c")
    c121 = to_int("1cbbf615425cf9f8e38e8ec249d0dd12a66dee6ae3105a7b658ce808ec8b6e65")
    
    c_vals = {
        'c00': (c000, c001),
        'c01': (c010, c011),
        'c02': (c020, c021),
        'c10': (c100, c101),
        'c11': (c110, c111),
        'c12': (c120, c121)
    }
    
    x = map_c_to_py(c_vals)
    x2 = x * x
    
    res_c = map_py_to_c(x2)
    
    print(f"DEBUG Python x^2 c00: {res_c['c00'][0]:x}")
    print(f"DEBUG Python x^2 c01: {res_c['c00'][1]:x}")

check_sqr_correctly()
