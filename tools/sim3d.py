#!/usr/bin/env python3
# sim3d.py <dspram dump> [out.png]: render3d.cpp + hidden.cpp in Python, on a
# DSP RAM dump (sb_dump.lua / sb_start.lua, or the game's --dump): the finished
# page's objects, drawn as the game draws them (same models, edge dedupe,
# triangles, board keys, hiding rule), into a 496x480 PNG. For checking the
# renderer against MAME's picture of the same frame without a build.
import math, struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ptrom import rom, s16 as _s16
from rot import matrix
from pngio import write_png

NEAR = 16.0
W, H = 496, 480
FX, FY = 582.0, 797.0           # picture pixels (the game uses board units: same ratios)

def s16(v): v &= 0xffff; return v - 65536 if v & 0x8000 else v

def load_model(code):
    """parts: (verts, polys, bias); poly = (idx list deduped, raw4, color, edge bits)"""
    parts = []
    size = 0x100000
    master = rom(code)
    if master < 0 or master >= size: return parts
    for k in range(1024):              # parts until the -1 marker (the reactor has 70+)
        sub = rom(master + k)
        if sub < 0 or sub >= size: break
        n = rom(sub) & 0xffff
        if n <= 2: continue
        d = lambda i: s16(rom(sub + 1 + i)) if i < n else 0
        nv = d(1); p = 3 + 3 * nv
        if nv <= 0 or p >= n: continue
        nq = d(p); p += 1
        if nq <= 0 or p + 5 * nq > n: continue
        verts = [(d(3 + 3 * i), d(4 + 3 * i), d(5 + 3 * i)) for i in range(nv)]
        polys = []
        for q in range(nq):
            raw = [d(p + i) for i in range(4)]
            if any(v < 0 or v >= nv for v in raw): p += 5; continue
            idx = []
            for v in raw:
                if idx and idx[-1] == v: continue
                idx.append(v)
            while len(idx) > 1 and idx[-1] == idx[0]: idx.pop()
            if len(idx) >= 3:
                polys.append([idx, raw, d(p + 4) & 0xffff, (1 << len(idx)) - 1])
            p += 5
        parts.append((verts, polys, d(2)))
    # every edge once in the whole model, by position
    seen = {}
    for pi, (verts, polys, b) in enumerate(parts):
        for f, poly in enumerate(polys):
            idx = poly[0]
            for i in range(len(idx)):
                a, c = verts[idx[i]], verts[idx[(i + 1) % len(idx)]]
                key = tuple(sorted((a, c)))
                if key in seen: poly[3] &= ~(1 << i)
                else: seen[key] = 1
    return parts

_models = {}
def model(code):
    if code not in _models: _models[code] = load_model(code)
    return _models[code]

def mat(m): return [v / 32768.0 for v in m]
def mul(a, b): return [sum(a[i * 3 + k] * b[k * 3 + j] for k in range(3)) for i in range(3) for j in range(3)]
def app(m, p): return tuple(m[r * 3] * p[0] + m[r * 3 + 1] * p[1] + m[r * 3 + 2] * p[2] for r in range(3))

def rotation(w, order):
    sx, cx, sy, cy, sz, cz = [s16(v) / 32768.0 for v in w]
    X = [1, 0, 0, 0, cx, -sx, 0, sx, cx]; Y = [cy, 0, sy, 0, 1, 0, -sy, 0, cy]; Z = [cz, -sz, 0, sz, cz, 0, 0, 0, 1]
    return {0: mul(mul(Z, Y), X), 1: mul(mul(Y, Z), X), 2: mul(mul(Z, X), Y),
            3: mul(mul(X, Z), Y), 4: mul(mul(Y, X), Z), 5: mul(mul(X, Y), Z)}.get(order, mul(mul(Z, Y), X))

def clip_near(pts, bits):
    out, ob = [], 0
    n = len(pts)
    for i in range(n):
        a, b = pts[i], pts[(i + 1) % n]; drawn = (bits >> i) & 1
        ina, inb = a[2] > NEAR, b[2] > NEAR
        if ina:
            if drawn: ob |= 1 << len(out)
            out.append(a)
        if ina != inb:
            t = (NEAR - a[2]) / (b[2] - a[2])
            if not ina and drawn: ob |= 1 << len(out)
            out.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, NEAR))
    return out, ob

def proj(p): return (W / 2 + FX * p[0] / p[2], H / 2 - FY * p[1] / p[2])

def frame(words):
    sel = words[0x103] & 1; base = 0x4000 + (sel ^ 1) * 0x2000
    cam = words[base:base + 0x40]
    co = s16(cam[0x26]); co = co if 0 <= co <= 5 else 0
    M = mul(rotation(cam[3:9], 4), rotation(cam[0x20:0x26], co))
    i = base + 0x100; recs = []
    while words[i] != 0xffff:
        t = words[i]; n = {0: 6, 1: 13, 2: 13, 0x100: 11, 0x101: 14}.get(t)
        if not n: break
        recs.append(words[i:i + n]); i += n
    return M, recs

def build(words):
    M, recs = frame(words)
    tris, segs = [], []
    obj = 0
    for r in recs:
        if r[0] in (0x100, 0x101): continue
        code = (r[1] + 1) & 0xffff
        pos = (s16(r[3]), s16(r[4]), s16(r[5]))
        R = rotation(r[6:12], s16(r[12]) if 0 <= s16(r[12]) <= 5 else 0) if len(r) == 13 else [1, 0, 0, 0, 1, 0, 0, 0, 1]
        C = [1, 0, 0, 0, 1, 0, 0, 0, 1] if r[0] == 2 else M      # type 2: camera space (the player's shots)
        MR = mul(C, R); t = app(C, pos)
        parts = model(code)
        views = [[tuple(app(MR, v)[k] + t[k] for k in range(3)) for v in verts] for verts, polys, b in parts]
        if max((p[2] for vs in views for p in vs), default=0) <= NEAR: continue
        obj += 1
        for pidx, ((verts, polys, bias), V) in enumerate(zip(parts, views)):
            for idx, raw, color, bits in polys:
                key = bias + sum(V[k][2] for k in raw) / 4
                P = [V[k] for k in idx]
                trs = [((P[0], P[1], P[2]), bits & (3 if len(P) == 4 else 7))]
                if len(P) == 4: trs.append(((P[2], P[3], P[0]), (bits >> 2) & 3))
                for tri, tb in trs:
                    pts, eb = clip_near(list(tri), tb)
                    if len(pts) < 3: continue
                    for k in range(1, len(pts) - 1): tris.append((pts[0], pts[k], pts[k + 1], (obj, pidx), key, code))
                    for k in range(len(pts)):
                        if (eb >> k) & 1: segs.append((pts[k], pts[(k + 1) % len(pts)], (obj, pidx), key, color, code))
    return tris, segs

def visible(tris, segs):
    T = []
    for a, b, c, o, key, code in tris:
        P = [proj(a), proj(b), proj(c)]
        area = (P[1][0] - P[0][0]) * (P[2][1] - P[0][1]) - (P[2][0] - P[0][0]) * (P[1][1] - P[0][1])
        if abs(area) < 1e-6: continue
        if area > 0: P = [P[0], P[2], P[1]]     # picture y is down: make them clockwise in picture = anticlockwise board
        ab = [b[k] - a[k] for k in range(3)]; ac = [c[k] - a[k] for k in range(3)]
        n = (ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0])
        L = math.sqrt(sum(x * x for x in n)) or 1; n = tuple(x / L for x in n)
        T.append((P, n, sum(n[k] * a[k] for k in range(3)), o, key, code, (a, b, c)))
    out = []
    for a, b, o, key, color, code in segs:
        x0, y0 = proj(a); x1, y1 = proj(b)
        hid = []
        for P, n, k, to, tkey, tcode, corners in T:
            # (object, part): the board's key rule between objects and parts,
            # never by a face the line is an edge of; exact depth within a part
            own = sum(1 for cc in corners for e in (a, b) if cc == e) >= 2
            if to != o and (own or tkey >= key): continue
            if max(p[0] for p in P) < min(x0, x1) or min(p[0] for p in P) > max(x0, x1): continue
            if max(p[1] for p in P) < min(y0, y1) or min(p[1] for p in P) > max(y0, y1): continue
            s0, s1, ok = 0.0, 1.0, True
            dx, dy = x1 - x0, y1 - y0
            for i in range(3):
                j = (i + 1) % 3
                nx, ny = (P[j][1] - P[i][1]), -(P[j][0] - P[i][0])     # inward for picture-y-down ordering
                num = nx * (x0 - P[i][0]) + ny * (y0 - P[i][1]); den = nx * dx + ny * dy
                if den == 0:
                    if num < 0: ok = False; break
                else:
                    rr = -num / den
                    if den > 0: s0 = max(s0, rr)
                    else: s1 = min(s1, rr)
                if s0 >= s1: ok = False; break
            if not ok: continue
            if to != o: hid.append((s0, s1)); continue
            side = -1.0 if k > 0 else 1.0
            g0 = (sum(n[i] * a[i] for i in range(3)) - k) * side
            g1 = (sum(n[i] * b[i] for i in range(3)) - k) * side
            eps = 1e-3 * abs(k)
            if g0 < -eps and g1 < -eps: u0, u1 = 0.0, 1.0
            elif g0 >= -eps and g1 >= -eps: continue
            else:
                uc = (-eps - g0) / (g1 - g0)
                u0, u1 = (0.0, uc) if g0 < -eps else (uc, 1.0)
            ts = lambda u: u * b[2] / (a[2] + u * (b[2] - a[2]))
            lo, hi = max(s0, ts(u0)), min(s1, ts(u1))
            if lo < hi: hid.append((lo, hi))
        hid.sort(); at = 0.0; pieces = []
        for lo, hi in hid:
            if lo > at: pieces.append((at, lo))
            at = max(at, hi)
        if at < 1.0: pieces.append((at, 1.0))
        for p0, p1 in pieces:
            out.append(((x0 + dx * p0, y0 + dy * p0), (x0 + dx * p1, y0 + dy * p1), code))
    return out

def draw(lines, path):
    img = bytearray(W * H * 3)
    for (ax, ay), (bx, by), code in lines:
        n = int(max(abs(bx - ax), abs(by - ay))) + 1
        for i in range(n + 1):
            x = int(ax + (bx - ax) * i / n); y = int(ay + (by - ay) * i / n)
            if 0 <= x < W and 0 <= y < H: img[(y * W + x) * 3:(y * W + x) * 3 + 3] = b'\xff\xff\xff'
    write_png(path, W, H, img)

if __name__ == '__main__':
    words = struct.unpack('>32768H', open(sys.argv[1], 'rb').read())
    tris, segs = build(words)
    lines = visible(tris, segs)
    print('objects', len(set(t[3] for t in tris)), 'faces', len(tris), 'edges', len(segs), 'visible pieces', len(lines))
    if len(sys.argv) > 2: draw(lines, sys.argv[2])
