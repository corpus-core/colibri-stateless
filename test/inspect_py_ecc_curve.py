from py_ecc.bn128 import bn128_curve, FQ, FQ2
import inspect

print(inspect.getsource(bn128_curve.is_on_curve))
print(inspect.getsource(bn128_curve.double))
print(inspect.getsource(bn128_curve.add))


