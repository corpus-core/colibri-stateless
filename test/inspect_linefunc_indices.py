from py_ecc.bn128 import bn128_pairing, b, curve_order, G1, G2, FQ, FQ2, FQ12
from py_ecc.bn128.bn128_pairing import linefunc

def twist_Q(Q):
    x, y = Q
    # x * w^2
    # y * w^3
    
    x0 = x.coeffs[0]
    x1 = x.coeffs[1]
    y0 = y.coeffs[0]
    y1 = y.coeffs[1]
    
    # coeffs for Q_twisted_x (x * w^2)
    # w^2 term: x0 - 9*x1
    # w^8 term: x1
    
    xt_coeffs = [0]*12
    xt_coeffs[2] = x0 - 9*x1
    xt_coeffs[8] = x1
    
    yt_coeffs = [0]*12
    yt_coeffs[3] = y0 - 9*y1
    yt_coeffs[9] = y1
    
    return (FQ12(xt_coeffs), FQ12(yt_coeffs))

def inspect():
    Q_twisted = twist_Q(G2)
    T = Q_twisted
    
    # Cast P to FQ12
    Px_fq12 = FQ12([G1[0]] + [0]*11)
    Py_fq12 = FQ12([G1[1]] + [0]*11)
    P_fq12 = (Px_fq12, Py_fq12)
    
    # P1=T, P2=T (Doubling case roughly, but here T=Q_twisted)
    l = linefunc(T, T, P_fq12)
    
    print("Linefunc result coeffs indices:")
    for i, c in enumerate(l.coeffs):
        if c != 0:
            print(f"Index {i} is non-zero: {c}")

inspect()
