import sys

P = 21888242871839275222246405745257275088696311157297823662689037894645226208583

def inv(a, n):
    return pow(a, n - 2, n)

# twist_b = 3 / (9 + i)
# = 3 * (9 - i) / (82)
# = 27 * 82^-1 - 3 * 82^-1 i

inv82 = inv(82, P)
re = (27 * inv82) % P
im = (-3 * inv82) % P

print(f"RE: {hex(re)}")
print(f"IM: {hex(im)}")

# Check existing constants in bn254.c
# RE=2b149d40ceb8aaae81be18991be06ac3b5b4c5e559dbefa33267e6dc24a138e5
# IM=009713b03af0fed4cd2cafadeed8fdf4a74fa084e52d1852e4a2bd0685c315d2

expected_re = 0x2b149d40ceb8aaae81be18991be06ac3b5b4c5e559dbefa33267e6dc24a138e5
expected_im = 0x009713b03af0fed4cd2cafadeed8fdf4a74fa084e52d1852e4a2bd0685c315d2

if re == expected_re:
    print("RE matches")
else:
    print("RE MISMATCH")

if im == expected_im:
    print("IM matches")
else:
    print("IM MISMATCH")


