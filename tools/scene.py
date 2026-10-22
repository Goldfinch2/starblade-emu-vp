from sb import *
from rot import matrix
from ptrom import obj
def records(w,page):
    a=0xC100 if page==0 else 0xE100; out=[]
    while W(w,a)!=0xffff and len(out)<400:
        t=W(w,a)
        if t in (1,2): out.append(('O',[s16(W(w,a+i)) for i in range(13)])); a+=13
        elif t==0: out.append(('P',[s16(W(w,a+i)) for i in range(6)])); a+=6
        elif t==0x100: a+=11
        elif t==0x101: a+=14
        else: break
    return out
def camblock(w,page):
    base=0xC000 if page==0 else 0xE000
    return [s16(W(w,base+i)) for i in range(0x40)]
def world_parts(w,page):
    """every quad of every object, in world coordinates: (pts4, color)"""
    out=[]
    for kind,e in records(w,page):
        code=(e[1]+1)&0xffff
        if kind=='O':
            M=[v/32768.0 for v in matrix(e[6:12],e[12])]; pos=e[3:6]
        else:
            M=[1,0,0,0,1,0,0,0,1]; pos=e[3:6]
        for vs,qs,b in obj(code):
            for q in qs:
                pts=[]
                for i in q[:4]:
                    if not 0<=i<len(vs): break
                    x,y,z=vs[i]
                    pts.append(tuple(M[r*3]*x+M[r*3+1]*y+M[r*3+2]*z+pos[r] for r in range(3)))
                if len(pts)==4: out.append((pts,q[4]))
    return out
