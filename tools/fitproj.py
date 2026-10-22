from slv import *
from sb import *
from scene import records, camblock
from rot import matrix
from ptrom import obj
def expected_parts(w,page):
    """per visible object part: world vertices (after the object's matrix)"""
    blocks=masterblocks(w)
    out=[]
    for kind,e in records(w,page):
        code=(e[1]+1)&0xffff
        if kind=='O':
            R=[x/32768.0 for x in matrix(e[6:12],e[12])]
        else:
            R=[1,0,0,0,1,0,0,0,1]
        pos=e[3:6]
        for vs,qs,b in obj(code):
            out.append((code,[[sum(R[r*3+k]*v[k] for k in range(3))+pos[r] for r in range(3)] for v in vs]))
    return out
def align(sp, ex):
    """pair slave parts with expected parts: walk both, skipping expected
    parts (culled objects) when vertex counts differ"""
    pairs=[]; j=0
    # skip leading partial object: find first slave part matching an expected start
    i=0
    while i<len(sp):
        if j>=len(ex): break
        if len(sp[i][1])==len(ex[j][1]):
            pairs.append((sp[i][1],ex[j][1])); i+=1; j+=1
        else:
            j+=1
            if j>=len(ex): i+=1; j=0
    return pairs
def align_depth(sp, ex):
    pairs=[]
    for c,s in sp:
        best=None
        for code,wv in ex:
            if len(wv)!=len(s): continue
            d=sum(abs(p[2]-q[2]) for p,q in zip(wv,s))/len(s)
            if best is None or d<best[0]: best=(d,wv)
        if best and best[0]<300: pairs.append((s,best[1]))
    return pairs
