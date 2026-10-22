# occlusion_check.py: true-depth test on MAME captures: are one set of models' edges in front of
# or behind another set's faces where they overlap on screen (frame dumps from sb_dump.lua).
import sys; sys.path.insert(0, __import__('os').path.dirname(__file__))
from sb import load
from scene import records, camblock
from cam import camera
from rot import matrix
from ptrom import obj
FX,FY=582.0,797.0
def build(w,page):
    M=[v/32768 for v in camera(camblock(w,page))]
    objs=[]
    for kind,e in records(w,page):
        code=(e[1]+1)&0xffff
        R=[v/32768 for v in matrix(e[6:12],e[12])] if kind=='O' else [1,0,0,0,1,0,0,0,1]
        tris=[];edges=[]
        for vs,qs,b in obj(code):
            V=[]
            for v in vs:
                wp=[sum(R[r*3+k]*v[k] for k in range(3))+e[3+r] for r in range(3)]
                V.append([sum(M[r*3+k]*wp[k] for k in range(3)) for r in range(3)])
            for q in qs:
                idx=list(dict.fromkeys(q[:4]))
                if len(idx)<3: continue
                P=[V[i] for i in idx]
                for k in range(len(P)): edges.append((P[k],P[(k+1)%len(P)]))
                tris.append((P[0],P[1],P[2]))
                if len(P)==4: tris.append((P[2],P[3],P[0]))
        objs.append((code,kind,tris,edges))
    return objs
def proj(p): return (FX*p[0]/p[2], -FY*p[1]/p[2])
def depth(t,sx,sy):
    if any(p[2]<=16 for p in t): return None
    a,b,c=[proj(p) for p in t]
    d=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
    if d==0: return None
    l1=((b[1]-c[1])*(sx-c[0])+(c[0]-b[0])*(sy-c[1]))/d
    l2=((c[1]-a[1])*(sx-c[0])+(a[0]-c[0])*(sy-c[1]))/d
    l3=1-l1-l2
    if min(l1,l2,l3)<-1e-9: return None
    return 1/(l1/t[0][2]+l2/t[1][2]+l3/t[2][2])
def test(frame,edge_codes,face_codes,label):
    objs=build(load(frame),0)
    E=[o for o in objs if o[0] in edge_codes]; F=[o for o in objs if o[0] in face_codes]
    front=back=0
    for _,_,_,edges in E:
        for a,b in edges:
            for k in range(11):
                t=k/10; p=[a[i]+(b[i]-a[i])*t for i in range(3)]
                if p[2]<=16: continue
                sx,sy=proj(p)
                for _,_,tris,_ in F:
                    for tr in tris:
                        z=depth(tr,sx,sy)
                        if z is None: continue
                        if p[2]<z-1: front+=1
                        elif p[2]>z+1: back+=1
    print('%s (frame %d): edge points in front %d, behind %d'%(label,frame,front,back))
test(1800,set(range(1463,1740)),{1257,1258},'logo letters vs bar')
test(3600,{2},set(c for c in range(1,2400) if c not in (2,858,859,860,861)),'model 2 vs other objects')
test(3600,set(c for c in range(1,2400) if c not in (2,858,859,860,861)),{2},'other objects vs model 2')
