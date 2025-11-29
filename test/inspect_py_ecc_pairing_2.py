from py_ecc.bn128 import bn128_pairing
import inspect

print("miller_loop source:")
print(inspect.getsource(bn128_pairing.miller_loop))
print("twist source:")
print(inspect.getsource(bn128_pairing.twist))


