#!/usr/bin/env python3
# sprview.py <frame>: sprites.cpp's outlines (a Python port) drawn over MAME's
# picture of the same frame (sb_spr.lua dumps), to check positions and sizes.
import struct, sys, zipfile
sys.path.insert(0, '.')
from pngio import read_png, write_png

f = int(sys.argv[1])
raw = open('/mnt/c/tmp/mame/sbspr_%06d.bin' % f, 'rb').read()
W16 = lambda b: list(struct.unpack('>%dH' % (len(b) // 2), b))
ram = W16(raw[:0x20000]); pos = W16(raw[0x20000:0x20008]); pal = W16(raw[0x20008:0x30008]); pext = W16(raw[0x30008:0x40008])
z = zipfile.ZipFile('/mnt/c/tmp/mame/roms/starblad.zip')
o = [z.read('st1-obj%d.bin' % i) for i in range(4)]
tiles = bytearray(0x200000)
for i in range(4): tiles[i::4] = o[i]

def sext(v, bits):
    m = 1 << (bits - 1); v &= (1 << bits) - 1; return (v ^ m) - m
def rgb(i):
    i &= 0x7fff; return (pal[i] >> 8, pal[i] & 0xff, pext[i] & 0xff)
opaque = lambda p: p not in (0xff, 0x00, 0x01)

w, h, img = read_png('/mnt/c/tmp/mame/snap/sbspr_%06d.png' % f)
over = bytearray(img); blank = bytearray(w * h * 3)
def put(buf, x, y, c):
    if 0 <= x < w and 0 <= y < h: buf[(y * w + x) * 3:(y * w + x) * 3 + 3] = bytes(c)
def line(buf, x0, y0, x1, y1, c):
    n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
    for k in range(n + 1):
        t = k / n; put(buf, int(x0 + (x1 - x0) * t), int(y0 + (y1 - y0) * t), c)

xscroll = sext(pos[1], 10); yscroll = sext(pos[0], 9) + 0x20
count = 0; segs_total = 0
for i in range(256):
    which = ram[0x1000 + i]; e = ram[(which & 0xff) * 8:(which & 0xff) * 8 + 8]
    palette = e[6]; link = e[0] & 0x7ff; offset = e[1]
    hpos = sext(e[2] - xscroll, 11); vpos = sext(e[3] - yscroll, 11)
    hsize, vsize = e[4], e[5]
    fm = ram[0x2000 + link * 4:0x2000 + link * 4 + 4]
    tile_index = fm[0]; cols = (fm[1] >> 4) & 15 or 16; rows = fm[1] & 15 or 16
    dx, dy = fm[2] & 0x1ff, fm[3] & 0x1ff
    flipx, flipy = hsize & 0x8000, vsize & 0x8000
    hsize &= 0x3ff; vsize &= 0x3ff
    if hsize and vsize:
        zx = hsize / (cols * 16); zy = vsize / (rows * 16)
        dxz = int((dx & 0xff) * zx + 0.5); dyz = int((dy & 0xff) * zy + 0.5)
        if dx & 0x100: dxz = -dxz
        if dy & 0x100: dyz = -dyz
        hpos += dxz if flipx else -dxz; vpos += dyz if flipy else -dyz
        x0 = hpos - hsize if flipx else hpos; y0 = vpos - vsize if flipy else vpos
        Wp, Hp = cols * 16, rows * 16
        big = max(Wp, Hp); cell = (big + 63) // 64 if big > 64 else 1
        CW, CH = (Wp + cell - 1) // cell, (Hp + cell - 1) // cell
        filled = [0] * (CW * CH); counts = [0] * 256
        for r in range(rows):
            for c in range(cols):
                t = ram[0x4000 + tile_index + r * cols + c]
                if t & 0x8000: continue
                base = ((t + offset) % 8192) * 256
                for y in range(16):
                    for x in range(16):
                        p = tiles[base + y * 16 + x]
                        if opaque(p):
                            counts[p] += 1; filled[((r * 16 + y) // cell) * CW + (c * 16 + x) // cell] += 1
        if max(counts):
            pen = counts.index(max(counts))
            col = rgb(0x1000 + ((palette & 15) << 8) + pen)
            top = max(col) or 1; col = tuple(v * 255 // top for v in col) if max(col) >= 8 else (200, 200, 200)
            mask = [v * 2 >= cell * cell for v in filled]
            at = lambda x, y: 0 <= x < CW and 0 <= y < CH and mask[y * CW + x]
            segs = []
            for y in range(CH + 1):
                s = None
                for x in range(CW + 1):
                    edge = x < CW and at(x, y - 1) != at(x, y)
                    if edge and s is None: s = x
                    if not edge and s is not None: segs.append((s * cell, y * cell, x * cell, y * cell)); s = None
            for x in range(CW + 1):
                s = None
                for y in range(CH + 1):
                    edge = y < CH and at(x - 1, y) != at(x, y)
                    if edge and s is None: s = y
                    if not edge and s is not None: segs.append((x * cell, s * cell, x * cell, y * cell)); s = None
            mapx = lambda px: x0 + ((Wp - px) if flipx else px) * zx
            mapy = lambda py: y0 + ((Hp - py) if flipy else py) * zy
            for a, b, c2, d in segs:
                line(over, mapx(a), mapy(b), mapx(c2), mapy(d), (255, 0, 255))
                line(blank, mapx(a), mapy(b), mapx(c2), mapy(d), col)
            count += 1; segs_total += len(segs)
    if which & 0x100: break
print('frame', f, 'sprites', count, 'segments', segs_total, 'scroll', hex(pos[0]), hex(pos[1]))
write_png('/tmp/claude-1000/-home-mario-vectorpie-design/c0ef1fb6-c4dd-4464-bfc7-cf5fad6a20bb/scratchpad/spr_over_%d.png' % f, w, h, over)
write_png('/tmp/claude-1000/-home-mario-vectorpie-design/c0ef1fb6-c4dd-4464-bfc7-cf5fad6a20bb/scratchpad/spr_vec_%d.png' % f, w, h, blank)
