#!/usr/bin/env python3
# font_from_tiles.py <starblad.zip> <sprite editor folder> [preview.png] [--fonts]
# Outline traces generated from the sprite ROM: each tile's contour, pixel
# staircases turned into straight and 45-degree lines (a beveled outline).
# By default the graphic tiles that go with text (the namco logo, the
# box pieces); the game draws the text itself in the SDK's Asteroids font
# (sprites.cpp). --fonts: the fonts too, as outlines (the game's ASCII font
# traced, the other fonts fitted to it). Written as tile traces into the sprite
# editor's work file (out/starblade_vectors.json; tiles already traced there are
# left alone) and its export, out/sprites.vec.
import json, os, struct, sys, zipfile

TRANSPARENT = (0xFF, 0x00, 0x01)
FIRST, LAST = 0x20, 0x7F
CLEAN_TOLERANCE = 1.5    # pixels: "clean" outlines stay this close to the picture
CLEAN_MIN_AREA = 6       # square pixels: smaller pieces of a "clean" outline are dropped
MAX_STEP = 4            # pixels: zigzag staircase steps up to this long become one line


def tiles_from_zip(path):
    z = zipfile.ZipFile(path)
    parts = [z.read('st1-obj%d.bin' % i) for i in range(4)]
    buf = bytearray(0x200000)
    for i in range(4):
        buf[i::4] = parts[i]
    return bytes(buf)


def loops(mask, W=16, H=16):
    """Closed contours of the filled pixels, as lists of pixel-corner points,
    filled area on the left (outer loops anticlockwise in picture space)."""
    at = lambda x, y: 0 <= x < W and 0 <= y < H and mask[y * W + x]
    # directed boundary edges: start corner -> end corner
    edges = {}
    for y in range(H):
        for x in range(W):
            if not at(x, y):
                continue
            if not at(x, y - 1): edges.setdefault((x + 1, y), []).append((x, y))          # top, right to left
            if not at(x, y + 1): edges.setdefault((x, y + 1), []).append((x + 1, y + 1))  # bottom, left to right
            if not at(x - 1, y): edges.setdefault((x, y), []).append((x, y + 1))          # left, top to bottom
            if not at(x + 1, y): edges.setdefault((x + 1, y + 1), []).append((x + 1, y))  # right, bottom to top
    out = []
    while edges:
        start = next(iter(edges))
        loop = [start]
        cur = start
        prev_dir = None
        while True:
            nexts = edges.get(cur)
            if not nexts:
                break
            # at a corner touching two loops, turn the same way every time
            if len(nexts) > 1 and prev_dir is not None:
                def turn(n):
                    d = (n[0] - cur[0], n[1] - cur[1])
                    return prev_dir[0] * d[1] - prev_dir[1] * d[0]
                nexts.sort(key=turn)
            nxt = nexts.pop(0)
            if not nexts:
                del edges[cur]
            prev_dir = (nxt[0] - cur[0], nxt[1] - cur[1])
            cur = nxt
            if cur == start:
                break
            loop.append(cur)
        if len(loop) >= 3:
            out.append(loop)
    return out


def corners(loop):
    """The loop's real corners (where it turns), in order."""
    n = len(loop)
    out = []
    for i in range(n):
        a, b, c = loop[i - 1], loop[i], loop[(i + 1) % n]
        if (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0]) != 0:
            out.append(b)
    return out


def bevel(loop, max_step=MAX_STEP):
    """Staircases (short edges turning left, right, left...) become one line:
    a 1x1 step at a corner a 45-degree bevel, the 2-1-2-1 steps of V or X
    their slanted stroke. Long edges stay horizontal or vertical."""
    v = corners(loop)
    n = len(v)
    if n < 4:
        return v + [v[0]]
    def turn(i):            # at corner i: sign of the turn from edge i-1 to edge i
        a, b, c = v[i - 1], v[i], v[(i + 1) % n]
        return (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0])
    def length(i):          # edge i: corner i to corner i+1
        a, b = v[i], v[(i + 1) % n]
        return abs(b[0] - a[0]) + abs(b[1] - a[1])
    step = [length(i) <= max_step and (turn(i) > 0) != (turn((i + 1) % n) > 0) for i in range(n)]
    if all(step):
        return v + [v[0]]
    # start on a corner that begins a non-step edge
    start = next(i for i in range(n) if not step[i - 1])
    # A run of one or two steps is a bevel only if its steps are small (1-2
    # pixels): the 4x4 inner corners of + # $ are corners, not a slope.
    i = 0
    while i < n:
        k = (start + i) % n
        if not step[k]:
            i += 1
            continue
        run = []
        while i < n and step[(start + i) % n]:
            run.append((start + i) % n)
            i += 1
        if len(run) <= 2 and max(length(e) for e in run) > 2:
            for e in run:
                step[e] = False
    out = []
    i = 0
    while i < n:
        k = (start + i) % n
        out.append(v[k])
        if step[k]:
            while i < n and step[(start + i) % n]:   # skip the staircase's inner corners
                i += 1
        else:
            i += 1
    return out + [out[0]]


def glyph(tiles, t):
    px = tiles[t * 256:(t + 1) * 256]
    mask = [p not in TRANSPARENT for p in px]
    counts = {}
    for p in px:
        if p not in TRANSPARENT:
            counts[p] = counts.get(p, 0) + 1
    if not counts:
        return None, 0
    pen = max(counts, key=counts.get)
    polys = [{"pen": pen, "points": [(float(x), float(y)) for x, y in bevel(l)]}
             for l in loops(mask)]
    return polys, pen


def export_text(work):
    lines = ["# Star Blade vector sprites (sprite_editor.py). S: a whole sprite image (its key),",
             "# T: one 16x16 tile; then E (draw nothing) or P lines: pen, count, x y pairs in pixels.",
             "# X: a picture of text (its first tile, pen), then L lines: x0 x1 baseline height text."]
    for kind, tag in (("sprites", "S"), ("tiles", "T")):
        for k, r in sorted(work[kind].items()):
            if not r.get("polys") and not r.get("empty"):
                continue
            lines.append("%s %s" % (tag, k))
            if r.get("empty"):
                lines.append("E")
                continue
            for p in r["polys"]:
                if len(p["points"]) < 2:
                    continue
                lines.append("P %d %d %s" % (p["pen"], len(p["points"]), " ".join("%g %g" % (x, y) for x, y in p["points"])))
    for k, lb in sorted(work.get("labels", {}).items()):
        lines.append("X %s %d" % (k, lb["pen"]))
        for x0, x1, base, height, text in lb["lines"]:
            lines.append("L %g %g %g %g %s" % (x0, x1, base, height, text))
    for g in work.get("gauges", []):
        lines.append("G %s %s %d %d %g %g %g %g %g %g %g" % (g["lo"], g["hi"], g["pen"], g["segments"], g["cx"], g["cy"],
                                                           g["yscale"], g["r0"], g["r1"], g["a0"], g["a1"]))
    return "\n".join(lines) + "\n"


def preview(tiles, glyphs, path):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from pngio import write_png
    S = 4
    ts = sorted(glyphs)
    cols = 24
    W, H = cols * 18 * S, ((len(ts) + cols - 1) // cols) * 18 * S
    img = bytearray(W * H * 3)
    def put(x, y, c):
        if 0 <= x < W and 0 <= y < H: img[(y * W + x) * 3:(y * W + x) * 3 + 3] = bytes(c)
    for i, t in enumerate(ts):
        ox, oy = (i % cols) * 18 * S + S, (i // cols) * 18 * S + S
        px = tiles[t * 256:(t + 1) * 256]
        for y in range(16):
            for x in range(16):
                if px[y * 16 + x] not in TRANSPARENT:
                    for dy in range(S):
                        for dx in range(S): put(ox + x * S + dx, oy + y * S + dy, (50, 50, 80))
        for p in glyphs.get(t) or []:
            pts = p["points"]
            for (ax, ay), (bx, by) in zip(pts, pts[1:]):
                steps = int(max(abs(bx - ax), abs(by - ay)) * S) + 1
                for k in range(steps + 1):
                    put(int(ox + (ax + (bx - ax) * k / steps) * S), int(oy + (ay + (by - ay) * k / steps) * S), (255, 255, 255))
    write_png(path, W, H, img)


# The other fonts: A-Z then 0-9 from these tiles, drawn with the main font's
# outlines fitted to each original glyph (the reference look: one outlined
# font for all text, at the game's own sizes and places).
# (tile of A, the characters from there: A-Z, 0-9, then each font's symbols)
ALPHA_FONTS = [(0x194, "-&/"), (0x1BB, "-&/"), (0x1E2, "-&/"), (0x209, "-&"), (0x22F, ".!"), (0x255, ".")]
DIGIT_FONTS = [0x399, 0x3A3, 0x3AD]     # score digits, two thin digit sets (distance)
# Graphic tiles traced as outlines like the font: the namco logo, the
# distance panel's ".".
OUTLINE_TILES = (list(range(0x5B, 0x60)) + [0xCBE]   # ... the briefing's pointer line
                 + [0xD42, 0xD43])                                                     # the engine power bars (stretched)

# Whole sprites traced as one outline (no seams between their tiles), by key
# (sprites.cpp's): size, tile offset, raw tile words.
SPRITE_OUTLINES = {
    # the briefing's arrow between Red Eye and Mother Planet
    "c65f06013fd9d52f": (7, 4, 0x0000, "8020 8020 02f4 0cbf 0cc0 0cc1 8020 0cc2 0cc3 0cc4 0cc5 0cc5 0cc5 0cc6 0cc5 0cc5 0cc5 0cc5 0cc7 0cc8 8020 0cc9 0cc9 0cc9 0cc9 0cca 0ccb 8020"),
    # the Red Eye page: the red cross on the planet
    "b307dd0470bb4e03": (2, 3, 0x0000, "034c 034d 034e 034f 0350 0351"),
    # the Octopus page: the larger red cross
    "d70c9fe5f2593827": (3, 4, 0x0000, "0352 0353 0354 0355 0356 0357 0358 0359 035a 035b 035c 035d"),
    # the HUD
    "8f39ffebba15e336": (2, 1, 0x0000, "03c2 03c2", "thin"),   # the distance panel divider
    "c87c60d34f8b3e26": (6, 7, 0x0000, "8020 0366 0367 0368 0369 8020 036a 036b 036c 036d 036e 8020 036f 0370 8020 0371 0372 8020 0373 0374 8020 0375 0376 0377 0378 0379 8020 8020 037a 037b 037c 037d 037e 037f 0380 8020 8020 0381 0382 8020 8020 8020", "thin", {224}),   # the shield frame (its arcs; FULL / EMPTY are a label)
    # the tunnel scene (preflight)
    "c66f6484c5af843b": (10, 5, 0x0000, "0d4f 8020 8020 8020 8020 8020 8020 8020 8020 8020 0d50 0d51 0d52 0d53 0d54 0d55 0d56 0d57 0d58 8020 0d59 0d5a 0d5b 0d5c 0d5d 0d5e 0d5f 0d60 0d61 0d62 8020 0d63 0d64 0d65 0d66 0d67 0d68 8020 8020 8020 8020 0d69 0d6a 0d6b 8020 8020 8020 8020 8020 8020", "clean"),   # the Geosword
    "6f338b2312697bf9": (8, 2, 0x0000, "0d6c 0d6d 0d6e 0d6f 0d70 0d71 0d72 0d73 0d74 0d75 0d76 0d77 0d78 0d79 0d7a 0d7b"),   # the catapult bar
}


def sprite_image(tiles, cols, rows, offset, words):
    W, H = cols * 16, rows * 16
    px = [0xFF] * (W * H)
    for k, w in enumerate(words):
        if w & 0x8000:
            continue
        t = (w + offset) % 8192
        c, r = k % cols, k // cols
        for y in range(16):
            px[(r * 16 + y) * W + c * 16:(r * 16 + y) * W + c * 16 + 16] = tiles[t * 256 + y * 16:t * 256 + y * 16 + 16]
    return px, W, H


def thin_mask(mask, W, H):
    """Zhang-Suen thinning: strokes a few pixels wide become 1-pixel center lines."""
    m = [1 if v else 0 for v in mask]
    at = lambda x, y: m[y * W + x] if 0 <= x < W and 0 <= y < H else 0
    changed = True
    while changed:
        changed = False
        for step in (0, 1):
            kill = []
            for y in range(H):
                for x in range(W):
                    if not m[y * W + x]:
                        continue
                    n = [at(x, y - 1), at(x + 1, y - 1), at(x + 1, y), at(x + 1, y + 1),
                         at(x, y + 1), at(x - 1, y + 1), at(x - 1, y), at(x - 1, y - 1)]
                    b = sum(n)
                    a = sum(1 for i in range(8) if n[i] == 0 and n[(i + 1) % 8] == 1)
                    if not (2 <= b <= 6 and a == 1):
                        continue
                    if step == 0 and n[0] * n[2] * n[4] == 0 and n[2] * n[4] * n[6] == 0:
                        kill.append(y * W + x)
                    if step == 1 and n[0] * n[2] * n[6] == 0 and n[0] * n[4] * n[6] == 0:
                        kill.append(y * W + x)
            for i in kill:
                m[i] = 0
            changed = changed or bool(kill)
    return m


def center_lines(mask, W, H, tol=0.8):
    """Polylines along the center of thin strokes: the thinned pixels followed
    from end to end (and around loops), then straightened."""
    m = thin_mask(mask, W, H)
    on = lambda x, y: 0 <= x < W and 0 <= y < H and m[y * W + x]
    def nbrs(x, y):
        return [(x + dx, y + dy) for dy in (-1, 0, 1) for dx in (-1, 0, 1) if (dx or dy) and on(x + dx, y + dy)]
    used = set()
    def edge(a, b): return (a, b) if a < b else (b, a)
    lines = []
    pixels = [(x, y) for y in range(H) for x in range(W) if m[y * W + x]]
    # start at ends and junctions first, then whatever is left (loops)
    starts = [p for p in pixels if len(nbrs(*p)) != 2] + pixels
    for s in starts:
        for n in nbrs(*s):
            if edge(s, n) in used:
                continue
            chain = [s, n]; used.add(edge(s, n)); cur, prev = n, s
            while len(nbrs(*cur)) == 2:
                nxt = [q for q in nbrs(*cur) if q != prev][0]
                if edge(cur, nxt) in used:
                    break
                used.add(edge(cur, nxt)); chain.append(nxt); prev, cur = cur, nxt
            lines.append(chain)
    out = []
    for c in lines:
        pts = [(x + 0.5, y + 0.5) for x, y in c]
        out.append(simplify_open(pts, tol))
    return out


def simplify_open(pts, tol):
    if len(pts) < 3:
        return pts
    def dist(p, a, b):
        dx, dy = b[0] - a[0], b[1] - a[1]
        if dx == 0 and dy == 0:
            return ((p[0] - a[0]) ** 2 + (p[1] - a[1]) ** 2) ** 0.5
        t = max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / (dx * dx + dy * dy)))
        return ((p[0] - a[0] - t * dx) ** 2 + (p[1] - a[1] - t * dy) ** 2) ** 0.5
    i, best = max(((i, dist(pts[i], pts[0], pts[-1])) for i in range(1, len(pts) - 1)), key=lambda e: e[1])
    if best <= tol:
        return [pts[0], pts[-1]]
    return simplify_open(pts[:i + 1], tol)[:-1] + simplify_open(pts[i:], tol)


def dots(mask, W, H):
    """Each speck (a star) as a small cross at its center."""
    seen = set(); out = []
    for y in range(H):
        for x in range(W):
            if not mask[y * W + x] or (x, y) in seen:
                continue
            stack, blob = [(x, y)], []
            seen.add((x, y))
            while stack:
                cx, cy = stack.pop(); blob.append((cx, cy))
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        q = (cx + dx, cy + dy)
                        if 0 <= q[0] < W and 0 <= q[1] < H and mask[q[1] * W + q[0]] and q not in seen:
                            seen.add(q); stack.append(q)
            mx = sum(p[0] for p in blob) / len(blob) + 0.5
            my = sum(p[1] for p in blob) / len(blob) + 0.5
            out.append([(mx - 0.25, my), (mx + 0.25, my)])     # a point
    return out


def close_mask(mask, W, H, r=1):
    """Dithered pictures: grow by r pixels, then shrink back: the checkerboard
    becomes the solid shape it draws, outlined once instead of speck by speck."""
    def grow(m, value):
        out = list(m)
        for y in range(H):
            for x in range(W):
                if m[y * W + x] == value:
                    continue
                for dy in range(-r, r + 1):
                    for dx in range(-r, r + 1):
                        xx, yy = x + dx, y + dy
                        if 0 <= xx < W and 0 <= yy < H and m[yy * W + xx] == value:
                            out[y * W + x] = value
                            break
                    else:
                        continue
                    break
        return out
    return grow(grow(mask, True), False)


def dp_closed(pts, tol):
    """Douglas-Peucker on a closed loop (last point = first)."""
    def dist(p, a, b):
        dx, dy = b[0] - a[0], b[1] - a[1]
        if dx == 0 and dy == 0:
            return ((p[0] - a[0]) ** 2 + (p[1] - a[1]) ** 2) ** 0.5
        t = max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / (dx * dx + dy * dy)))
        return ((p[0] - a[0] - t * dx) ** 2 + (p[1] - a[1] - t * dy) ** 2) ** 0.5
    def dp(seg):
        if len(seg) < 3:
            return seg
        i, best = max(((i, dist(seg[i], seg[0], seg[-1])) for i in range(1, len(seg) - 1)), key=lambda e: e[1])
        if best <= tol:
            return [seg[0], seg[-1]]
        return dp(seg[:i + 1])[:-1] + dp(seg[i:])
    loop = pts[:-1]
    far = max(range(len(loop)), key=lambda i: (loop[i][0] - loop[0][0]) ** 2 + (loop[i][1] - loop[0][1]) ** 2)
    a = dp(loop[:far + 1]); b = dp(loop[far:] + [loop[0]])
    out = a[:-1] + b[:-1]
    return out + [out[0]]


def loop_area(pts):
    return abs(sum(pts[i][0] * pts[i + 1][1] - pts[i + 1][0] * pts[i][1] for i in range(len(pts) - 1))) / 2


def loop_pen(px, W, H, pts):
    """The main color of the pixels a loop encloses (pixel centers inside it)."""
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    counts = {}
    for y in range(int(min(ys)), int(max(ys)) + 1):
        cy = y + 0.5
        for x in range(int(min(xs)), int(max(xs)) + 1):
            if not (0 <= x < W and 0 <= y < H) or px[y * W + x] in TRANSPARENT:
                continue
            cx, inside = x + 0.5, False
            for i in range(len(pts) - 1):
                (ax, ay), (bx, by) = pts[i], pts[i + 1]
                if (ay > cy) != (by > cy) and cx < ax + (cy - ay) * (bx - ax) / (by - ay):
                    inside = not inside
            if inside:
                counts[px[y * W + x]] = counts.get(px[y * W + x], 0) + 1
    return max(counts, key=counts.get) if counts else None


def outline_image(px, W, H, close=False, clean=False, mode=None, pens=None, min_thick=0):
    mask = [p not in TRANSPARENT and (pens is None or p in pens) for p in px]
    if mode in ("thin", "dots"):
        counts = {}
        for p, k in zip(px, mask):
            if k: counts[p] = counts.get(p, 0) + 1
        if not counts:
            return []
        pen = max(counts, key=counts.get)
        lines = center_lines(mask, W, H) if mode == "thin" else dots(mask, W, H)
        return [{"pen": pen, "points": l} for l in lines if len(l) >= 2]
    if clean:
        # long clean strokes (a vector reference's look): straightened, specks
        # dropped, each piece in the color it encloses
        polys = []
        for l in loops(mask, W, H):
            pts = [(float(x), float(y)) for x, y in bevel(l)]
            pts = dp_closed(pts, CLEAN_TOLERANCE)
            if len(pts) < 4 or loop_area(pts) < CLEAN_MIN_AREA:
                continue
            if min_thick and min(max(p[0] for p in pts) - min(p[0] for p in pts),
                                 max(p[1] for p in pts) - min(p[1] for p in pts)) < min_thick:
                continue                    # a sliver at a tile's edge
            pen = loop_pen(px, W, H, pts)
            if pen is not None:
                polys.append({"pen": pen, "points": pts})
        return polys
    if close:
        mask = close_mask(mask, W, H)
    counts = {}
    for p in px:
        if p not in TRANSPARENT: counts[p] = counts.get(p, 0) + 1
    if not counts:
        return []
    pen = max(counts, key=counts.get)
    return [{"pen": pen, "points": [(float(x), float(y)) for x, y in bevel(l)]} for l in loops(mask, W, H)]


# Single tiles traced by a mode: "thin" (center lines of thin strokes), "dots"
# (each speck a small cross: stars), "clean" (straightened outline).
TILE_MODES = {}
TILE_MODES.update({t: "thin" for t in range(0x35E, 0x366)})       # the HUD's tick marks
# Stars: only the bright ones (pens 244-251 in the stars' palette: 84..178
# grey; 252-254 are 63, 42, 20). The single stars (0286-028D) step down that
# ramp as they twinkle: their dim steps draw nothing. The star fields
# (0291-02C7, dozens of stars each) keep their few bright stars.
BRIGHT_STAR_PENS = set(range(244, 252))
TILE_MODES.update({t: "dots" for t in range(0x286, 0x28E)})       # single stars
TILE_MODES.update({t: "dots" for t in range(0x291, 0x2C8)})       # star fields
EMPTY_STAR_FIELDS = []
TILE_MODES.update({0x105: "thin", 0x106: "thin"})                 # the crosshair's lock-on mark

# A whole picture drawn in its first tile, its other tiles drawing nothing:
# every variant of the picture that starts with that tile gets the drawing.
# Points in pixels from the first tile's corner (they may go past the tile).
_CROSSHAIR = [   # the centers of its 2-pixel strokes, in its 64 x 48 sprite
    [(32.5, 0.5), (32.5, 6.5)], [(32.5, 40.5), (32.5, 46.5)], [(1.5, 23.5), (7.5, 23.5)], [(56.5, 23.5), (62.5, 23.5)],
    [(21.0, 7.5), (10.0, 18.5), (10.0, 28.5), (21.0, 39.5)],
    [(44.0, 7.5), (55.0, 18.5), (55.0, 28.5), (44.0, 39.5)],
]
# Pictures the game puts together from one-tile sprites in a row (16 pixels
# apart): outlined as a whole into the first tile, the others draw nothing.
ROW_PICTURES = [
    list(range(0xA8, 0xB0)),         # the namco logo
]

TILE_DRAWINGS = {
    0x100: (233, _CROSSHAIR),        # the crosshair
    # the HUD's outer side marks: two thin rows on the raster screen (one thick
    # line there), a single line here
    0x362: (224, [[(0.0, 7.5), (16.0, 7.5)]]),
    # the mark below: 0364 (a double row, stretched 1.5x tall) over 0365; one
    # line down both, 24 of 0364's rows (36 pixels)
    0x364: (224, [[(8.5, 0.0), (8.5, 24.0)]]),
    # the distance panel's frame (0383 0384 0384 0385; the bottom one is it
    # mirrored): /=====\ , the legs slanting down from the ends of the double
    # line, the second line running between them
    0x383: (224, [[(0.5, 11.5), (3.5, 4.5), (60.5, 4.5), (63.5, 11.5)],
                  [(2.64, 6.5), (61.36, 6.5)]]),

}
EMPTY_TILES = [0x101, 0x102, 0x103, 0x104, 0x107, 0x109, 0x10A, 0x10B, 0x10C,   # the crosshair's other tiles
               0x363,                                                              # the side marks' second piece: 0362 alone
               0x365,                                                              # the lower mark's second piece: 0364 alone
               0x384, 0x385]                                                       # the distance frame's other tiles: 0383 alone


# Pictures drawn by hand after a vector reference (a port of the game): the
# sprite key, its pen, the box the drawing fills (sprite pixels: x0, y0, x1,
# y1), then polylines in 0..1 of that box.
def _ellipse(cx, cy, rx, ry, n=20):
    import math
    return [(cx + rx * math.cos(2 * math.pi * i / n), cy + ry * math.sin(2 * math.pi * i / n)) for i in range(n + 1)]

def _probe():
    # the tunnel scene's target circle: an ellipse, a probe across it
    import math
    W, H = 41.0, 52.0                       # the box, to work in pixels for the angles
    p0, p1 = (0.24 * W, 0.80 * H), (0.60 * W, 0.44 * H)
    dx, dy = p1[0] - p0[0], p1[1] - p0[1]; L = math.hypot(dx, dy); ux, uy = dx / L, dy / L; nx, ny = -uy, ux
    def rect(a, b, half):
        pts = [(a[0] + nx * half, a[1] + ny * half), (b[0] + nx * half, b[1] + ny * half),
               (b[0] - nx * half, b[1] - ny * half), (a[0] - nx * half, a[1] - ny * half)]
        pts.append(pts[0]); return [(x / W, y / H) for x, y in pts]
    p2 = (p1[0] + 11 * ux, p1[1] + 11 * uy)
    ring = (p1[0] + 7.5 * ux, p1[1] + 7.5 * uy)
    return [_ellipse(0.5, 0.5, 0.5, 0.5, 28), rect(p0, p1, 1.5), rect(p1, p2, 4.0),
            [((ring[0] + 2.2 * math.cos(a * math.pi / 6)) / W, (ring[1] + 2.2 * math.sin(a * math.pi / 6)) / H) for a in range(13)]]

_OCTOPUS = [
    [(0.30, 0.05), (0.70, 0.05), (0.88, 0.30), (0.95, 0.62), (0.80, 0.72), (0.50, 0.76), (0.20, 0.72),
     (0.05, 0.62), (0.12, 0.30), (0.30, 0.05)],                                  # the bell
    [(0.12, 0.30), (0.30, 0.36), (0.70, 0.36), (0.88, 0.30)],                    # the dome's edge
    _ellipse(0.50, 0.21, 0.16, 0.08, 14),                                        # the eye
    [(0.30, 0.36), (0.35, 0.74)], [(0.70, 0.36), (0.65, 0.74)],                  # the ribs
    [(0.05, 0.80), (0.22, 0.80), (0.12, 0.92), (0.05, 0.80)],                    # the feet
    [(0.78, 0.80), (0.95, 0.80), (0.88, 0.92), (0.78, 0.80)],
    [(0.42, 1.00), (0.58, 1.00), (0.50, 0.89), (0.42, 1.00)],
]

DRAWINGS = {
    "9acbd20fd3c64963": (34, (4, 7, 45, 59), _probe()),
    # the Octopus icon's four animation frames: one drawing
    "1a5bec3b62e267fb": (165, (6, 0, 43, 57), _OCTOPUS),
    "f4b804c300467673": (165, (6, 0, 43, 57), _OCTOPUS),
    "df1cc63e62f30c9f": (165, (6, 0, 43, 57), _OCTOPUS),
    "709ce93b69ede6a7": (165, (6, 0, 43, 57), _OCTOPUS),
}


# The tiles of the outlined and drawn pictures: each picture's drawing goes
# in its first tile, its other tiles draw nothing (so every variant of the
# picture that starts with that tile gets the drawing).
PICTURE_TILES = {
    "c65f06013fd9d52f": (7, 0x0000, "8020 8020 02f4 0cbf 0cc0 0cc1 8020 0cc2 0cc3 0cc4 0cc5 0cc5 0cc5 0cc6 0cc5 0cc5 0cc5 0cc5 0cc7 0cc8 8020 0cc9 0cc9 0cc9 0cc9 0cca 0ccb 8020"),
    "b307dd0470bb4e03": (2, 0x0000, "034c 034d 034e 034f 0350 0351"),
    "d70c9fe5f2593827": (3, 0x0000, "0352 0353 0354 0355 0356 0357 0358 0359 035a 035b 035c 035d"),
    "c66f6484c5af843b": (10, 0x0000, "0d4f 8020 8020 8020 8020 8020 8020 8020 8020 8020 0d50 0d51 0d52 0d53 0d54 0d55 0d56 0d57 0d58 8020 0d59 0d5a 0d5b 0d5c 0d5d 0d5e 0d5f 0d60 0d61 0d62 8020 0d63 0d64 0d65 0d66 0d67 0d68 8020 8020 8020 8020 0d69 0d6a 0d6b 8020 8020 8020 8020 8020 8020"),
    "6f338b2312697bf9": (8, 0x0000, "0d6c 0d6d 0d6e 0d6f 0d70 0d71 0d72 0d73 0d74 0d75 0d76 0d77 0d78 0d79 0d7a 0d7b"),
    "8f39ffebba15e336": (2, 0x0000, "03c2 03c2"),
    "c87c60d34f8b3e26": (6, 0x0000, "8020 0366 0367 0368 0369 8020 036a 036b 036c 036d 036e 8020 036f 0370 8020 0371 0372 8020 0373 0374 8020 0375 0376 0377 0378 0379 8020 8020 037a 037b 037c 037d 037e 037f 0380 8020 8020 0381 0382 8020 8020 8020"),
    "9acbd20fd3c64963": (3, 0x0000, "0d98 0d99 0d9a 0da7 0da8 0da9 0db6 0db7 0db8 0dc5 0dc6 0dc7"),
    "1a5bec3b62e267fb": (3, 0x0000, "0d8c 0d8d 0d8e 0d9b 0d9c 0d9d 0daa 0dab 0dac 0db9 0dba 0dbb"),
    "f4b804c300467673": (3, 0x0000, "0d8f 0d90 0d91 0d9e 0d9f 0da0 0dad 0dae 0daf 0dbc 0dbd 0dbe"),
    "df1cc63e62f30c9f": (3, 0x0000, "0d92 0d93 0d94 0da1 0da2 0da3 0db0 0db1 0db2 0dbf 0dc0 0dc1"),
    "709ce93b69ede6a7": (3, 0x0000, "0d95 0d96 0d97 0da4 0da5 0da6 0db3 0db4 0db5 0dc2 0dc3 0dc4"),
}


def to_first_tile(work, key, polys, put):
    """A picture's drawing (sprite pixels) into its first tile, the rest empty."""
    cols, offset, words = PICTURE_TILES[key]
    words = [int(w, 16) for w in words.split()]
    slots = [(i, (w + offset) % 8192) for i, w in enumerate(words) if not w & 0x8000]
    if not slots:
        return 0
    (i0, first), rest = slots[0], slots[1:]
    ox, oy = (i0 % cols) * 16, (i0 // cols) * 16
    k = "%04x" % first
    if work["tiles"].get(k, {}).get("protected"):
        return 0
    work["tiles"][k] = {"polys": [{"pen": p["pen"], "points": [(round(x - ox, 2), round(y - oy, 2)) for x, y in p["points"]]}
                                  for p in polys]}
    for _, t in rest:
        if t != first:
            work["tiles"]["%04x" % t] = {"empty": True}
    work["sprites"].pop(key, None)
    return 1


# Gauges: a sprite whose first tile is in lo..hi is an arc of segments, each
# drawn where the original picture is filled at its middle. The shield: its
# fill picture's C, fitted (tools: center, y scale, radii, the opening).
GAUGES = [
    {"lo": "1b40", "hi": "1c7f", "pen": 235, "segments": 24, "cx": 40, "cy": 56, "yscale": 1.28,
     "r0": 24.2, "r1": 39.0, "a0": -3.0, "a1": 269.0},
]


# Pictures of text, drawn as text by the game: the sprite's first tile, its
# pen, then per line: left, right, baseline, cap height (sprite pixels), text.
LABELS = {
    "0a70": {"pen": 236, "lines": [[6, 40, 18, 8, "RED EYE"], [6, 87, 29, 8, "THE MECHANIZED PLANET"]]},
    "0a79": {"pen": 236, "lines": [[5, 75, 18, 8, "MOTHER PLANET"], [5, 64, 29, 8, "OUR LIVING PLANET"]]},
    # the briefing, Red Eye and Mother Planet side by side
    "0a83": {"pen": 236, "lines": [[3, 37, 8, 8, "RED EYE"],
                                   [3, 191, 19, 8, "THE EMPIRE'S MECHANIZED PLANET SIZE ATTACK WEAPON."],
                                   [3, 190, 30, 8, "APPROACHING THE 2ND DEFENSE LINE OF MOTHER PLANET."]]},
    "0a9b": {"pen": 236, "lines": [[1, 71, 8, 8, "MOTHER PLANET"],
                                   [1, 164, 19, 8, "HEADQUARTERS OF UGSF AND OUR LIVING PLANET."],
                                   [1, 136, 30, 8, "WE ARE NOW UNDER ATTACK BY RED EYE."]]},
    "0ab1": {"pen": 236, "lines": [[2, 134, 15, 10, "MOTHER PLANET IS EXPECTED BE WITHIN"],
                                   [22, 115, 31, 10, "FIRING RANGE OF RED EYE'S"],
                                   [36, 141, 47, 10, "WAVE CANNON IN 35.24 HOURS."]]},
    # the Red Eye page
    "0c62": {"pen": 64, "lines": [[7, 249, 22, 18, 'MECHANIZED PLANET "RED EYE"'],
                                  [68, 188, 35, 8, "@236 IDENT FRIEND OR FOE:   FOE FOE FOE"],
                                  [60, 196, 46, 8, "@236 CATEGORY:   FOE ATTACK DEVICE CLASS Q"]]},
    "0b74": {"pen": 236, "lines": [[1, 73, 9, 8, "FOE HARDWARE SPEC:"],
                                   [12, 48, 23, 8, "DIAMETER:"], [79, 103, 23, 8, ">780KM"],
                                   [12, 67, 37, 8, "CRUSING SPEED:"],
                                   [56, 103, 51, 8, ">18.74KM/SEC"],
                                   [12, 84, 65, 8, "STRUCTURAL ANALYSIS:"],
                                   [26, 37, 78, 8, "FE:"], [80, 103, 78, 8, ">28.9%"],
                                   [26, 36, 90, 8, "SI:"], [82, 103, 90, 8, ">19.1%"],
                                   [26, 34, 102, 8, "C:"], [81, 103, 102, 8, ">12.7%"],
                                   [26, 36, 114, 8, "AL:"], [81, 103, 114, 8, ">11.4%"],
                                   [26, 36, 126, 8, "BI:"], [85, 103, 126, 8, ">7.3%"],
                                   [26, 36, 138, 8, "TI:"], [84, 103, 138, 8, ">4.0%"],
                                   [26, 60, 150, 8, "UNKNOWN:"], [81, 103, 150, 8, ">16.6%"],
                                   [1, 27, 164, 8, "NOTICE:"],
                                   [12, 65, 178, 8, "DEFENCE LEVEL:"], [98, 103, 178, 8, ">A"],
                                   [12, 55, 192, 8, "HIGH ENERGY"],
                                   [40, 105, 206, 8, ">DEFENSIVE BARRIER."],
                                   [13, 104, 220, 8, ">LIMITED ACCESS PASSAGES."]]},
    "0c05": {"pen": 236, "lines": [[7, 24, 50, 22, "@29 +"],          # the red cross
                                   [1, 54, 69, 8, "POWER REACTER"],
                                   [19, 103, 83, 8, ">OCTPUS ATTACK VECTERS."],
                                   [10, 42, 99, 8, "POSITION:"], [61, 65, 99, 8, "E"], [74, 103, 99, 8, ">135.43'"],
                                   [61, 66, 113, 8, "N"], [79, 103, 113, 8, ">43.12'"],
                                   [8, 103, 132, 8, ">SEE NEXT FEDCON SCREEN,"],
                                   [22, 56, 147, 8, "STANDBY,"],
                                   [44, 103, 161, 8, ">NOW PROCESSING."]]},
    "0c94": {"pen": 236, "lines": [[39, 152, 10, 8, "FEDERATION FEDCON DATA BASE"],
                                   [34, 157, 24, 8, "FEDCON FILE NO. VPD-10963-Q1"]]},
    # the Octopus page
    "0c3b": {"pen": 64, "lines": [[22, 234, 22, 18, 'POWER REACTOR "OCTOPUS"'],
                                  [47, 208, 36, 8, "@236 CATEGORY:  FOE PLANET ATTACK DEVICE CLASS Q"]]},
    "0ac8": {"pen": 236, "lines": [[1, 48, 9, 8, "FOE POSITION:"],
                                   [11, 83, 23, 8, "E135.43' N43.12'"],
                                   [1, 76, 39, 8, "FOE HARDWARE SPEC:"],
                                   [10, 46, 53, 8, "DIAMETER:"], [54, 97, 53, 8, ">MAX. 3200M"],
                                   [56, 97, 67, 8, ">MIN. 1200M"],
                                   [10, 37, 81, 8, "HEIGHT:"], [74, 97, 81, 8, ">1170M"],
                                   [10, 108, 95, 8, "MAXIMUM OUTPUT: 45200TW"],
                                   [61, 108, 110, 8, ">TERRA WATTS"],
                                   [10, 62, 123, 8, "POWER SOURCE:"],
                                   [17, 109, 137, 8, ">NUCLEAR FUSION MAGNETRON"],
                                   [1, 28, 153, 8, "NOTICE:"],
                                   [10, 61, 167, 8, "DEFENCE LEVEL:"], [86, 97, 167, 8, ">A+"],
                                   [17, 91, 181, 8, "FOE CONTROL IN CENTER"],
                                   [32, 109, 194, 8, ">OF MECHANIZED PLANET."],
                                   [17, 68, 208, 8, "ACCESS DENIED"],
                                   [49, 109, 221, 8, ">BY FOE DEFENSES."]]},
    "0ace": {"pen": 236, "lines": [[1, 98, 9, 8, "THERE IS AN ACCESS PASSAGE"],
                                   [47, 105, 22, 8, ">TO ACCESS INSIDE."],
                                   [1, 96, 37, 8, "SEE NEXT FEDCON SCREEN."],
                                   [14, 109, 50, 8, ">STANDBY. NOW PROCESSING."],
                                   [12, 29, 82, 22, "@29 +"],
                                   [36, 78, 68, 8, '"THE MOUTH'],
                                   [61, 106, 81, 8, '>OF OCTOPUS"'],
                                   [10, 73, 99, 8, "THIS IS THE ACCESS"],
                                   [19, 102, 112, 8, ">PASSAGE TO THE INTERIOR"],
                                   [67, 109, 125, 8, ">OF OCTOPUS."],
                                   [10, 102, 139, 8, "THE ENTARNCE IS DEFENDED"],
                                   [26, 109, 152, 8, ">WITH REINFORCED PORTAL."],
                                   [10, 42, 167, 8, "POSITION:"], [69, 103, 167, 8, ">X:1600M"],
                                   [69, 103, 181, 8, ">Y:  720M"],
                                   [10, 86, 195, 8, "SIZE OF THE ENTARNCE:"],
                                   [49, 76, 209, 8, "HEIGHT:"], [87, 103, 209, 8, ">64M"],
                                   [51, 76, 223, 8, "WIDTH:"], [87, 103, 223, 8, ">74M"]]},
    "0b7a": {"pen": 236, "lines": [[8, 25, 48, 22, "@29 +"],
                                   [33, 101, 40, 8, '"POWER CHAMBER"'],
                                   [4, 98, 72, 8, "THIS CHAMBER HAS A POWER"],
                                   [13, 82, 85, 8, "HAS A POWER STONE,"],
                                   [32, 110, 98, 8, ">THE HEART OF OCTOPUS."],
                                   [5, 73, 118, 8, "ALL RED EYE ENERGY"],
                                   [54, 110, 131, 8, ">CONVERGES HERE."],
                                   [5, 58, 148, 8, "THIS IS THE ONLY"],
                                   [28, 110, 161, 8, ">WEAK POINT OF RED EYE."],
                                   [5, 37, 184, 8, "POSITION:"], [69, 103, 184, 8, ">X:1600M"],
                                   [69, 103, 198, 8, ">Y:  590M"]]},
    # the tunnel scene (preflight)
    "0c8c": {"pen": 236, "lines": [[66, 189, 44, 8, "FX-01 GEOSWORD READY FOR START."]]},
    "0ceb": {"pen": 236, "lines": [[1, 52, 11, 8, "ENGINE START..."],
                                   [1, 82, 23, 8, "FLIGHT CONTROL SYSTEMS"], [87, 98, 23, 8, "--"],
                                   [1, 62, 35, 8, "PHOTON LASER UNIT"], [67, 98, 35, 8, "--"],
                                   [1, 63, 47, 8, "ATTACK COMPUTER"], [67, 98, 47, 8, "--"],
                                   [1, 86, 59, 8, "COMMUNICATION SYSTEMS"], [91, 98, 59, 8, "--"]]},
    "c1b71cebe4f90641": {"pen": 56, "lines": [[8, 22, 13, 8, "NO.1"]]},
    "c1b725ebe4f9158c": {"pen": 56, "lines": [[8, 24, 13, 8, "NO.2"]]},
    "c1b726ebe4f9173f": {"pen": 56, "lines": [[8, 24, 13, 8, "NO.3"]]},
    "0d08": {"pen": 236, "lines": [[7, 105, 12, 8, "NOW VERIFYING TARGET DATA..."]]},
    "0d3a": {"pen": 236, "lines": [[8, 57, 12, 8, "ENGINE POWER"]]},
    "0d0f": {"pen": 236, "lines": [[19, 83, 10, 8, "TARGET - RED EYE"],
                                   [4, 41, 28, 8, "SPEC DATA"], [46, 79, 28, 8, "--"],
                                   [4, 52, 40, 8, "POSITION DATA"], [57, 79, 40, 8, "--"],
                                   [4, 44, 52, 8, "OTHER DATA"], [49, 79, 52, 8, "--"]]},
    "0d23": {"pen": 236, "lines": [[47, 112, 12, 8, "TARGET - OCTOPUS"],
                                   [1, 38, 30, 8, "SPEC DATA"], [43, 110, 30, 8, "--"],
                                   [1, 49, 42, 8, "POSITION DATA"], [54, 110, 42, 8, "--"],
                                   [1, 97, 54, 8, "ENTARNCE AND INTERIOR DATA"], [102, 110, 54, 8, "--"]]},
    "0d04": {"pen": 236, "lines": [[1, 63, 11, 8, "CATAPULT RELEASE."]]},
    "0cac": {"pen": 236, "lines": [[49, 143, 12, 8, "ALL SECTIONS ARE OPERATIVE."],
                                   [0, 192, 31, 9, "@66 PREFLIGHT CHECKLIST COMPLETE"]]},
    # the tunnel scene: SCRAMBLE in a frame, the engine power box (labels instead of the pictures)
    "24ec13c0021898b9": {"pen": 29, "lines": [[3, 124, 31, 31, "[]"], [10, 117, 28, 24, "SCRAMBLE"]]},
    "201a1f53e4b0a796": {"pen": 53, "lines": [[0, 95, 63, 63, "[]"], [0, 95, 50, 0, "--"],
                                              [8, 16, 61, 8, "M1"], [23, 32, 61, 8, "M2"], [37, 46, 61, 8, "M3"],
                                              [51, 58, 61, 8, "F1"], [66, 74, 61, 8, "F2"], [82, 86, 61, 8, "L"]]},
    # the HUD
    "038f": {"pen": 226, "lines": [[0, 45, 12, 12, "SCORE"]]},
    "0392": {"pen": 226, "lines": [[0, 47, 12, 12, "SHIELD"]]},
    "0395": {"pen": 226, "lines": [[0, 64, 12, 12, "DISTANCE"]]},
    "0388": {"pen": 50, "lines": [[6, 58, 13, 10, "TO RED EYE"]]},
    "03be": {"pen": 50, "lines": [[2, 14, 13, 7, "KM"]]},     # the distance unit ("skm" on the raster screen)
    # the continuation screen's COUNT DOWN and its number: a big serif font,
    # a character a 2x2 sprite (by its first tile); digits all one width
    "0111": {"pen": 8, "lines": [[5, 27, 28, 23, "C"]]},
    "0113": {"pen": 8, "lines": [[4, 28, 28, 23, "D"]]},
    "013b": {"pen": 8, "lines": [[4, 27, 28, 23, "N"]]},
    "013d": {"pen": 8, "lines": [[4, 28, 28, 23, "O"]]},
    "0150": {"pen": 8, "lines": [[4, 27, 28, 23, "T"]]},
    "0152": {"pen": 8, "lines": [[4, 28, 28, 23, "U"]]},
    "0156": {"pen": 8, "lines": [[0, 32, 28, 23, "W"]]},
    "0168": {"pen": 8, "lines": [[6, 25, 28, 23, "0"]]},
    "016a": {"pen": 8, "lines": [[6, 25, 28, 23, "1"]]},
    "016c": {"pen": 8, "lines": [[6, 25, 28, 23, "2"]]},
    "016e": {"pen": 8, "lines": [[6, 25, 28, 23, "3"]]},
    "017c": {"pen": 8, "lines": [[6, 25, 28, 23, "4"]]},
    "017e": {"pen": 8, "lines": [[6, 25, 28, 23, "5"]]},
    "0180": {"pen": 8, "lines": [[6, 25, 28, 23, "6"]]},
    "0182": {"pen": 8, "lines": [[6, 25, 28, 23, "7"]]},
    "0184": {"pen": 8, "lines": [[6, 25, 28, 23, "8"]]},
    "0186": {"pen": 8, "lines": [[6, 25, 28, 23, "9"]]},
    "c87c60d34f8b3e26": {"pen": 224, "lines": [[64, 85, 68, 7, "@229 FULL"], [44, 74, 91, 7, "@29 EMPTY"]]},
    # its footer starts like the Red Eye page's (0c94): matched by sprite key
    "e471a13015cf66f9": {"pen": 236, "lines": [[39, 152, 10, 8, "FEDERATION FEDCON DATA BASE"],
                                               [33, 158, 24, 8, "FEDCON FILE NO. VPD-10963-Q2"]]},
}


def bbox_pixels(tiles, t):
    px = tiles[t * 256:(t + 1) * 256]
    xs = [i % 16 for i in range(256) if px[i] not in TRANSPARENT]
    ys = [i // 16 for i in range(256) if px[i] not in TRANSPARENT]
    if not xs:
        return None, 0
    counts = {}
    for p in px:
        if p not in TRANSPARENT: counts[p] = counts.get(p, 0) + 1
    return (min(xs), min(ys), max(xs) + 1, max(ys) + 1), max(counts, key=counts.get)


def fitted(main_polys, src_box, dst_box, pen):
    sx0, sy0, sx1, sy1 = src_box
    dx0, dy0, dx1, dy1 = dst_box
    kx = (dx1 - dx0) / max(1e-6, sx1 - sx0)
    ky = (dy1 - dy0) / max(1e-6, sy1 - sy0)
    return [{"pen": pen, "points": [(round(dx0 + (x - sx0) * kx, 2), round(dy0 + (y - sy0) * ky, 2)) for x, y in p["points"]]}
            for p in main_polys]


def main():
    zip_path, editor = [a for a in sys.argv[1:] if not a.startswith("--")][:2]
    tiles = tiles_from_zip(zip_path)
    work_path = os.path.join(editor, "out", "starblade_vectors.json")
    work = {"sprites": {}, "tiles": {}}
    if os.path.exists(work_path):
        work = json.load(open(work_path))
        work.setdefault("sprites", {}); work.setdefault("tiles", {})
    glyphs, added, kept = {}, 0, 0

    def put(t, polys):
        nonlocal added, kept
        glyphs[t] = polys
        key = "%04x" % t
        if key in work["tiles"]:
            kept += 1
            return
        work["tiles"][key] = {"empty": True} if not polys else {"polys": polys}
        added += 1

    fonts = "--fonts" in sys.argv
    main_font = {}
    for t in range(FIRST, LAST + 1):
        main_font[t] = glyph(tiles, t)[0]
        if fonts:
            put(t, main_font[t])
    for t in OUTLINE_TILES:
        put(t, glyph(tiles, t)[0])

    def alias(t, ch):
        box, pen = bbox_pixels(tiles, t)
        src = main_font.get(ord(ch))
        if not box or not src:
            return
        sbox, _ = bbox_pixels(tiles, ord(ch))
        put(t, fitted(src, sbox, box, pen))

    for base, symbols in (ALPHA_FONTS if fonts else []):
        for i, ch in enumerate("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789" + symbols):
            alias(base + i, ch)
    for base in (DIGIT_FONTS if fonts else []):
        for i, ch in enumerate("0123456789"):
            alias(base + i, ch)

    for key, entry in SPRITE_OUTLINES.items():
        cols, rows, offset, words = entry[:4]
        close = len(entry) > 4 and entry[4] == "dithered"
        clean = len(entry) > 4 and entry[4] == "clean"
        mode = entry[4] if len(entry) > 4 and entry[4] in ("thin", "dots") else None
        pens = entry[5] if len(entry) > 5 else None
        if work["sprites"].get(key, {}).get("protected"):
            continue
        px, W, H = sprite_image(tiles, cols, rows, offset, [int(w, 16) for w in words.split()])
        polys = outline_image(px, W, H, close, clean, mode, pens)
        if key in PICTURE_TILES:
            added += to_first_tile(work, key, polys, put)
        else:
            work["sprites"][key] = {"polys": polys}
            added += 1
    for key, (pen, (x0, y0, x1, y1), polys) in DRAWINGS.items():
        if key in work["sprites"] and work["sprites"][key].get("protected"):
            continue
        polys = [{"pen": pen, "points": [(round(x0 + u * (x1 - x0), 2), round(y0 + v * (y1 - y0), 2)) for u, v in pl]}
                 for pl in polys]
        if key in PICTURE_TILES:
            added += to_first_tile(work, key, polys, put)
        else:
            work["sprites"][key] = {"polys": polys}
            added += 1
    for key in ("24ec13c0021898b9", "201a1f53e4b0a796"):     # now labels
        if key in work["sprites"] and not work["sprites"][key].get("protected"):
            del work["sprites"][key]
    for row in ROW_PICTURES:
        W, H = 16 * len(row), 16
        px = [0xFF] * (W * H)
        for i, t in enumerate(row):
            for y in range(16):
                px[y * W + i * 16:y * W + i * 16 + 16] = tiles[t * 256 + y * 16:t * 256 + y * 16 + 16]
        k = "%04x" % row[0]
        if not work["tiles"].get(k, {}).get("protected"):
            mask = [p not in TRANSPARENT for p in px]
            counts = {}
            for p in px:
                if p not in TRANSPARENT: counts[p] = counts.get(p, 0) + 1
            pen = max(counts, key=counts.get)
            work["tiles"][k] = {"polys": [{"pen": pen, "points": [(float(x), float(y)) for x, y in bevel(l)]}
                                          for l in loops(mask, W, H)]}
            for t in row[1:]:
                work["tiles"]["%04x" % t] = {"empty": True}
            added += 1
    for t, (pen, polys) in TILE_DRAWINGS.items():
        k = "%04x" % t
        if not work["tiles"].get(k, {}).get("protected"):
            work["tiles"][k] = {"polys": [{"pen": pen, "points": list(pl)} for pl in polys]}
            added += 1
    for t in EMPTY_TILES + EMPTY_STAR_FIELDS:
        put(t, None)
    for t, mode in TILE_MODES.items():
        px = list(tiles[t * 256:(t + 1) * 256])
        put(t, outline_image(px, 16, 16, clean=(mode == "clean"), mode=mode if mode != "clean" else None,
                             pens=BRIGHT_STAR_PENS if mode == "dots" else None,
                             min_thick=3 if mode == "clean" else 0))
    work["gauges"] = GAUGES
    labels = work.setdefault("labels", {})
    for k, lb in LABELS.items():
        labels.setdefault(k, lb)
    json.dump(work, open(work_path, "w"), indent=1)
    vec = export_text(work)
    open(os.path.join(editor, "out", "sprites.vec"), "w").write(vec)
    print("tiles: %d written, %d already traced (kept)" % (added, kept))
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) > 2:
        preview(tiles, glyphs, args[2])


main()
