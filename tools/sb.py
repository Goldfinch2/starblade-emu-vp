import glob, os
D='/mnt/c/tmp/mame/'
def load(frame):
    d=open(D+'sbdump_dspram_%06d.bin'%frame,'rb').read()
    return [d[i]<<8|d[i+1] for i in range(0,len(d),2)]
def s16(v): return v-65536 if v>=32768 else v
def W(w,addr): return w[addr-0x8000]        # DSP data address
def objlist(w,page):
    base=0xC100 if page==0 else 0xE100
    out=[]
    a=base
    while W(w,a)!=0xffff and len(out)<200:
        e=[s16(W(w,a+i)) for i in range(13)]
        out.append(e); a+=13
    return out
def camera(w,page):
    base=0xC000 if page==0 else 0xE000
    return [s16(W(w,base+i)) for i in range(0x80)]
def masterblocks(w):
    # entries reached by a goto: ffff <target>; at target: code, len, len words
    blocks=[]
    for i in range(len(w)-1):
        if w[i]==0xffff and 0x8400<=w[i+1]<0xc000:
            t=w[i+1]
            code,ln=W(w,t),W(w,t+1)
            if 0<ln<=0x40 and code not in (0,0xffff):
                blocks.append((t,code,[s16(W(w,t+2+k)) for k in range(ln)]))
    return blocks
def solve(A,b):
    n=len(A[0])+1
    M=[[0.0]*n for _ in range(n)]; v=[0.0]*n
    for row,y in zip(A,b):
        r=list(row)+[1.0]
        for i in range(n):
            v[i]+=r[i]*y
            for j in range(n): M[i][j]+=r[i]*r[j]
    for i in range(n):
        p=max(range(i,n),key=lambda k:abs(M[k][i])); M[i],M[p]=M[p],M[i]; v[i],v[p]=v[p],v[i]
        if abs(M[i][i])<1e-12: return None
        for k in range(i+1,n):
            f=M[k][i]/M[i][i]
            for j in range(i,n): M[k][j]-=f*M[i][j]
            v[k]-=f*v[i]
    x=[0]*n
    for i in reversed(range(n)):
        x[i]=(v[i]-sum(M[i][j]*x[j] for j in range(i+1,n)))/M[i][i]
    return x
def matched(w):
    blocks=[b for b in masterblocks(w) if len(b[2])==15]
    for page in (0,1):
        lst=objlist(w,page)
        n=len(lst)
        if n and len(blocks)>=n and all(lst[i][1]+1==blocks[i][1] for i in range(n)):
            return page,lst,blocks[:n]
    return None,None,None
