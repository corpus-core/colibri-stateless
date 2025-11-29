from py_ecc.bn128 import bn128_pairing
import inspect

print(inspect.getsource(bn128_pairing.pairing))
print(inspect.getsource(bn128_pairing.double))
print(inspect.getsource(bn128_pairing.add))


