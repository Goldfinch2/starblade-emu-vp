from sb import s16
def slave_parts(frame, path='/mnt/c/tmp/mame/sbslave.txt'):
    words=[int(l.split()[1]) for l in open(path) if int(l.split()[0])==frame]
    parts=[]; i=0
    while i<len(words):
        n=words[i]
        if n==0 or i+n>=len(words)+1: break
        color=words[i+1]; body=[s16(v) for v in words[i+2:i+1+n]]
        parts.append((color,[tuple(body[k:k+3]) for k in range(0,len(body)-2,3)]))
        i+=1+n
    return parts
