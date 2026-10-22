from scene import *
from rot import matrix
from pngio import *
FX, FY, CX, CY = 583.3, 793.7, 248.0, 240.0
def overlay(shot, listframe, oy=-45.0):
    w=load(listframe); page=listframe&1
    cam=camblock(w,page)
    C=[v/32768.0 for v in matrix(cam[0x20:0x26],4)]
    quads=world_parts(w,page)
    W0,H0,bg=read_png('/mnt/c/tmp/mame/sbsnap/sbsnap_%06d.png'%shot)
    img=bytearray(v//3 for v in bg)
    def put(x,y):
        if 0<=x<W0 and 0<=y<H0: o=(y*W0+x)*3; img[o:o+3]=b'\x00\xff\xff'
    def proj(p):
        v=[sum(C[r*3+k]*p[k] for k in range(3)) for r in range(3)]
        if v[2]<=20: return None
        return (CX+FX*v[0]/v[2], CY+oy-FY*v[1]/v[2])
    for pts,c in quads:
        P=[proj(p) for p in pts]
        if None in P: continue
        for j in range(4):
            a,b=P[j],P[(j+1)%4]
            n=int(max(abs(b[0]-a[0]),abs(b[1]-a[1])))+1
            if n>3000: continue
            for i in range(n+1):
                t=i/n; put(int(a[0]+(b[0]-a[0])*t),int(a[1]+(b[1]-a[1])*t))
    return W0,H0,img
