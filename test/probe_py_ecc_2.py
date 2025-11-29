from py_ecc.fields import bn128_FQ12, bn128_FQ2

one = bn128_FQ12.one()
FQ6 = type(one.coeffs[0])
print(f"FQ6 degree: {FQ6.degree}")

z = bn128_FQ2.zero()
try:
    t = FQ6.one()
    # t.coeffs = (z, z, z) # tuple
    # FQP usually stores coeffs as tuple.
    # But coeffs is property?
    print(f"Initial coeffs: {t.coeffs}")
    
    # Try to construct using polynomial representation?
    # No, FQP(coeffs) is standard.
    
    # Maybe the error `Expected an int or FQ object` is misleading or comes from deep inside?
    # It says `got object of type <class 'list'>`.
    # If I pass a list, it fails.
    # If I pass a tuple, it fails.
    
    # Maybe I should check if FQ6 is actually FQ?
    # If degree is 1?
except Exception as e:
    print(f"Error: {e}")


