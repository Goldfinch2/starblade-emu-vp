#!/usr/bin/env python3
# measure_text.py <starblad.zip> <capture.txt> [key prefix...]: for pictures of text
# (sb_arrow.lua / sb_redeye.lua captures), each text line's pieces (split at gaps
# wider than GAP pixels): rows, left, right, and the sprite's first tile and pen:
# the numbers a label in font_from_tiles.py needs.
import sys, zipfile, collections
GAP = 7
T = (0xFF, 0x00, 0x01)
z = zipfile.ZipFile(sys.argv[1])
o = [z.read('st1-obj%d.bin' % i) for i in range(4)]
buf = bytearray(0x200000)
for i in range(4): buf[i::4] = o[i]
want = sys.argv[3:]
for line in open(sys.argv[2]):
    f = line.split(); key, cols, rows, off = f[0], int(f[1]), int(f[2]), int(f[3], 16)
    if want and not any(key.startswith(w) for w in want): continue
    words = [int(t, 16) for t in f[f.index('tiles') + 1:]]
    W, H = cols * 16, rows * 16; px = [0xFF] * (W * H); first = None
    for k, w in enumerate(words):
        if w & 0x8000: continue
        t = (w + off) % 8192
        if first is None: first = t
        c, r = k % cols, k // cols
        for y in range(16): px[(r * 16 + y) * W + c * 16:(r * 16 + y) * W + c * 16 + 16] = buf[t * 256 + y * 16:t * 256 + y * 16 + 16]
    pen = collections.Counter(p for p in px if p not in T).most_common(1)[0][0]
    print('== %s first tile %04x pen %d' % (key[:6].upper(), first, pen))
    filled = [any(px[y * W + x] not in T for x in range(W)) for y in range(H)]
    y = 0
    while y < H:
        if not filled[y]: y += 1; continue
        y0 = y
        while y < H and filled[y]: y += 1
        colsf = [any(px[yy * W + x] not in T for yy in range(y0, y)) for x in range(W)]
        pieces, x = [], 0
        while x < W:
            if not colsf[x]: x += 1; continue
            x0 = x; last = x
            while x < W and (colsf[x] or x - last <= GAP):
                if colsf[x]: last = x
                x += 1
            pieces.append((x0, last + 1))
        print('  rows %3d-%3d  pieces %s' % (y0, y, pieces))
