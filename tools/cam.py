from rot import matrix
def camera(cam):
    """the master's final camera matrix (Q15 ints, rows): C1 x C2"""
    C1=matrix(cam[3:9],4)
    order=cam[0x26]
    if order<0 or order>5: order=0
    C2=matrix(cam[0x20:0x26],order)
    M=[]
    for r in range(3):
        for c in range(3):
            acc=sum(C1[r*3+k]*C2[k*3+c] for k in range(3))
            v=((acc<<1)>>16)
            M.append(v-65536 if v>=32768 else v)
    return M
