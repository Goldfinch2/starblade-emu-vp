# Star Blade point ROMs: object code -> list of parts, each part (verts, quads)
# verts: (x, y, z); quads: (i0, i1, i2, i3, color); data words are 16-bit. The format from MAME's
# namcos21 notes (Aaron Giles) and transfer_dsp_data.
import struct
raw=open('/mnt/c/tmp/mame/sbdump_point24.bin','rb').read()   # saved by sb_dump.lua
N=len(raw)//4
def rom(i):
    v=struct.unpack_from('>I',raw,4*(i&0xfffff))[0]&0xffffff
    return v-0x1000000 if v&0x800000 else v
def s16(v):
    v&=0xffff
    return v-65536 if v&0x8000 else v
def obj(code):
    parts=[]
    master=rom(code)
    for k in range(1024):              # parts until the -1 marker (the reactor has 70+)
        sub=rom(master+k)
        if sub in (-1, 0xffffff) or sub==-1: break
        n=rom(sub)&0xffff
        if n<=2: continue
        d=[s16(rom(sub+1+i)) for i in range(n)]
        quadidx, nv, bias = d[0], d[1], d[2]
        verts=[tuple(d[3+3*i:6+3*i]) for i in range(nv)]
        p=3+3*nv
        nq=d[p]; p+=1
        quads=[tuple(d[p+5*i:p+5*i+5]) for i in range(nq)]
        parts.append((verts,quads,bias))
    return parts
