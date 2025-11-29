from py_ecc.bn128 import FQ12, FQ, FQ2, field_modulus

def to_fq(hex_str):
    if hex_str is None: return FQ(0)
    return FQ(int(hex_str, 16))

# Values from C log lines 8 and 9
a_hex = [
    "069fe98d78aa11b658aab847108814f9029e9c5369a678f45e627cb8c45f46a0",
    "2de9c900c604da30555c9e834d4146dde2a98907e826eac8b28981a151e2f4d4",
    "0105ecb0e988716ac35881d31c7874e1e1b19642b6fb7cd6649a36117cebb4b1",
    "148c36017f02812ae3bc8df3d14b6587a2c6df0fad3670cd8d2a886fb28af4c3",
    "0b68125f8467990957d1ab346d669575e43cdd8abfcf2ed11749baac42539a7c",
    "173580f063be03a3173aa42fbc25489159090e817f098ae3784e91fc33387caf",
    "21a01c6caced0f375900dedeeaa55001a7e048eb9f7ad72b85e760fa38d09de3",
    "2a38bfd9711ffbb394af997d306f066543665c9054f01f9014b16ef22cd6853e",
    "17c472a7d68738dd034a61002b0a07e746d538a6e08f87acd80facf212cb6765",
    "1b7778089e13391ed523856781952baff39a05b5c9bb73f0f77e150ba79725ad",
    "0464427e200b4b5e3b863993a5bfc41df4d9caa16dc0216ef6bbcd18d35b3fdc",
    "1b3901fe9c48987b98e2eff42ad73de23b4ee6d1fd8ca829d22baacea460ebed"
]

b_hex = [
    "069fe98d78aa11b658aab847108814f9029e9c5369a678f45e627cb8c45f46a0",
    "2de9c900c604da30555c9e834d4146dde2a98907e826eac8b28981a151e2f4d4",
    "263eb00bf490313d8333a78a18783da8a6a01ec0ac6931d5799630ed30f65c3f",
    "224edf1ba4760d60a7066a294ab7c0fce9eee0d7debde8d9b79f835c2ee79926",
    "04571d1526d952c28ff5bb83f1e79ec9eef45a9c5f87908f8f677cd20a0aaf0b",
    "106621cc3286b114afb63ff55ec714784be9e5479435dfe410b7077439bd9b67",
    "11d5fcab99c726733c43b63038e90d85dca7232e4f0400a49271ca7945982d97",
    "0a93b7f3153230425660321ab55c38fe02ba31f455416f1e88931e7941b6feaf",
    "2189921571dad748fb3e703b7fb4eea7ebca45c62c9ecff096e46435a6c74e05",
    "19551e7c14eb9256ea53dfadb8c916548546334d7dbe53c5a17ebe8071da92eb",
    "08fa709f7900ec87268218a59bf8b392ad993a381f9deb754368493fb2c81b27",
    "16580d9926a1748c095e261e8e606c19c6d18aec91272a0c666e2eb8ee2cfb2d"
]

a_vals = [to_fq(x) for x in a_hex]
b_vals = [to_fq(x) for x in b_hex]

def build_coeffs(c_vals):
    coeffs = [FQ(0)] * 12
    coeffs[0] = c_vals[0] - 9 * c_vals[1]
    coeffs[6] = c_vals[1]
    coeffs[2] = c_vals[2] - 9 * c_vals[3]
    coeffs[8] = c_vals[3]
    coeffs[4] = c_vals[4] - 9 * c_vals[5]
    coeffs[10] = c_vals[5]
    coeffs[1] = c_vals[6] - 9 * c_vals[7]
    coeffs[7] = c_vals[7]
    coeffs[3] = c_vals[8] - 9 * c_vals[9]
    coeffs[9] = c_vals[9]
    coeffs[5] = c_vals[10] - 9 * c_vals[11]
    coeffs[11] = c_vals[11]
    return coeffs

A = FQ12(build_coeffs(a_vals))
B = FQ12(build_coeffs(b_vals))

res = A * B

c00_res = res.coeffs[0] + 9 * res.coeffs[6]
print(f"Result c00: {int(c00_res):x}")

print("Target C c00: 0ed1f35f335e04772377aae72d0d4de7fe92afd3a2317270f9189aff102d9c08")
