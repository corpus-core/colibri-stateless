from py_ecc.optimized_bn128 import optimized_pairing
import inspect

print(inspect.getsource(optimized_pairing.miller_loop))
print(inspect.getsource(optimized_pairing.linefunc))

