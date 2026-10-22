# modelview.py out.png code...: point ROM objects, each in a panel: every quad edge.
import sys, math, struct, zlib
from ptrom import obj
W,H=400,320
codes=[int(c,16) for c in sys.argv[2:]]
img=bytearray(W*len(codes)*H*3)
TW=W*len(codes)
def put(p,x,y,c):
    if 0<=x<W and 0<=y<H:
        o=(y*TW+p*W+x)*3; img[o:o+3]=bytes(c)
def line(p,a,b,c):
    n=int(max(abs(b[0]-a[0]),abs(b[1]-a[1])))+1
    for i in range(n+1):
        t=i/n; put(p,int(a[0]+(b[0]-a[0])*t),int(a[1]+(b[1]-a[1])*t),c)
for p,code in enumerate(codes):
    parts=obj(code)
    pts=[v for vs,qs,b in parts for v in vs]
    if not pts: continue
    yaw,pitch=math.radians(35),math.radians(25)
    def view(v):
        x,y,z=v
        x1,z1=math.cos(yaw)*x+math.sin(yaw)*z,-math.sin(yaw)*x+math.cos(yaw)*z
        return x1, math.cos(pitch)*y-math.sin(pitch)*z1
    P=[view(v) for v in pts]
    xs=[q[0] for q in P]; ys=[q[1] for q in P]
    s=0.85*min(W/max(1,max(xs)-min(xs)),H/max(1,max(ys)-min(ys)))
    cx,cy=(max(xs)+min(xs))/2,(max(ys)+min(ys))/2
    proj=lambda v:(W/2+(view(v)[0]-cx)*s, H/2-(view(v)[1]-cy)*s)
    for vs,qs,b in parts:
        for q in qs:
            idx=[i for i in q[:4]]
            for j in range(4):
                a,bb=idx[j],idx[(j+1)%4]
                if a!=bb and 0<=a<len(vs) and 0<=bb<len(vs):
                    line(p,proj(vs[a]),proj(vs[bb]),(0,200,255))
raw=b''.join(b'\x00'+bytes(img[y*TW*3:(y+1)*TW*3]) for y in range(H))
ch=lambda t,d: struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
open(sys.argv[1],'wb').write(b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',TW,H,8,2,0,0,0))+ch(b'IDAT',zlib.compress(raw,9))+ch(b'IEND',b''))
