import zlib, struct
def read_png(path):
    d=open(path,'rb').read(); i=8; w=h=0; idat=b''; ct=2; bd=8
    while i<len(d):
        n,t=struct.unpack('>I4s',d[i:i+8]); body=d[i+8:i+8+n]; i+=12+n
        if t==b'IHDR': w,h,bd,ct=struct.unpack('>IIBB',body[:10])
        elif t==b'IDAT': idat+=body
    bpp={2:3,6:4}[ct]; raw=zlib.decompress(idat); stride=w*bpp
    out=bytearray(); prev=bytearray(stride); p=0
    for y in range(h):
        f=raw[p]; line=bytearray(raw[p+1:p+1+stride]); p+=1+stride
        for x in range(stride):
            a=line[x-bpp] if x>=bpp else 0; b=prev[x]; c=prev[x-bpp] if x>=bpp else 0
            if f==1: line[x]=(line[x]+a)&255
            elif f==2: line[x]=(line[x]+b)&255
            elif f==3: line[x]=(line[x]+(a+b)//2)&255
            elif f==4:
                pa,pb,pc=abs(b-c),abs(a-c),abs(a+b-2*c)
                pr=a if pa<=pb and pa<=pc else (b if pb<=pc else c)
                line[x]=(line[x]+pr)&255
        rgb=bytearray()
        for x in range(w): rgb+=line[x*bpp:x*bpp+3]
        out+=rgb; prev=line
    return w,h,out
def write_png(path,w,h,rgb):
    raw=b''.join(b'\x00'+bytes(rgb[y*w*3:(y+1)*w*3]) for y in range(h))
    ch=lambda t,d: struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
    open(path,'wb').write(b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+ch(b'IDAT',zlib.compress(raw,6))+ch(b'IEND',b''))
