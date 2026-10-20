# Star Blade: what the System 21 DSPs do

Star Blade (Namco, 1991, System 21 with the C67 DSP board: 1 master + 4 slave
TMS320C25 "C67"). This is what the DSP board does, worked out from MAME's
driver (`src/mame/namco/namcos21_*`), the DSP programs the game uploads
(disassembled: `master.asm`, `slave.asm`) and captures of the running game
(the tools in `../tools`). Everything below was checked against the captures:
the master's output bit for bit, the projection to about a pixel.

The point: a re-creation does not need to emulate the DSPs. It reads what the
68000s write, and draws.

## 1. The shared DSP RAM

64 KB, 0x8000 words, at 68000 `0x200000-0x20FFFF`; the master DSP sees word
`n` of it at its data address `0x8000 + n` (68000 address `0x200000 + 2n`).

| 68000 | What |
|---|---|
| `0x200000-0x2000FF` | boot self-test text ("ROM:OK" ...), checksums |
| `0x200024`, `0x200026` | read by the 68000 each frame: `0x4545`, `0x3532` |
| `0x200100-0x200126` | DSP program upload (sizes, source addresses): not needed |
| `0x200202` | status, read each frame: keep `0` |
| `0x200206` | page select: the 68000 reads and toggles it |
| `0x208000-0x2080FF` | camera block, page 0 |
| `0x208200-0x208FFF` | object list, page 0 |
| `0x20C000-0x20C0FF` | camera block, page 1 |
| `0x20C200-0x20CFFF` | object list, page 1 |

Double buffered: in frame `F` the 68000 writes page `F & 1` and the DSPs draw
it. That is all the 68000s read back (captured over boot and play): the DSP
RAM can stay plain memory holding those values.

### Boot handshake (captured, `../tools/sb_boot2.lua`)

The 68000 tests the RAM (frames ~940-990), writes the self-test labels itself
("ROM:", "RAM:", "PTR:" ... "CPU: BOOTING"), sets up the program upload, then
waits on the DSP. What the DSP answers:

1. The 68000 writes `0x200112 = 2` (with `0x200104 = 2`, `0x200102 = 1`,
   upload addresses at `0x200120-0x200126`): the DSP **clears `0x200112`**.
2. The DSP **sets `0x200110 = 1`**, its point ROM checksums
   `0x20010A = EE52h`, `0x20010C = D5DFh`, and the text "EE52" at
   `0x200024` (the "PTR:" line, read by the 68000 every frame).
3. The 68000 writes `0x20010E = 1` (upload): the DSP **clears it**.
4. The 68000 writes `0x200202 = FFFFh`: the DSP **sets it to 0** (running).
5. Then every frame the 68000 writes `0x200206` (0, 1, 0 ...): the page it
   fills NEXT, often starting at once (during the title, 480 words in the
   same frame). The other page is complete: that is the one to draw. Seen
   in `../tools/sb_order.lua`: objects of page P (the slave 68000 adds its
   own records to it too), camera of P (in some phases written just before
   the select, in others just after the previous select), then select = P ^ 1.

## 2. The camera block (16-bit words)

| Word | Example | What |
|---|---|---|
| 0 | 2 | flags (bit 0 cleared by the master) |
| 1 | 1 | camera mode |
| 2 | 8192 | zoom (clamped `0AAAh..5555h`) |
| 3-8 | sin,cos x3 | camera angles C1 (Q15 pairs) |
| 9, 10 | 4096 | x / y scale, 1.0 = `1000h` (clamped `800h..2000h`) |
| 11, 12 | 0 | screen offset (clamped +-`1000h`) |
| 13, 14 | 248, 242 | half the viewport, pixels |
| 15, 16 | 3, 4 | monitor aspect |
| 17, 18 | 142, 20 | ? |
| `20h-25h` | sin,cos x3 | second camera angles C2 |
| `26h` | 2 | C2's rotation order (0..5) |
| `27h-2Bh` | | written back by the master (5 words); not read by the 68000 |

**Camera matrix** (rows, Q15): `M = C1 x C2`, `C1 = rot(words 3-8, order 4)`,
`C2 = rot(words 20h-25h, order word 26h)`; each entry is
`(sum of 3 Q15 products << 1) >> 16` (`../tools/cam.py`). The camera is at
the origin: object positions are already relative to it.

## 3. The object list

Records until a word `FFFFh`. The first word's high byte picks the family, the
low byte the kind:

| First word | Words | Record |
|---|---|---|
| `0x0001`, `0x0002` | 13 | object: `type, number, 0, x, y, z, s1, c1, s2, c2, s3, c3, order`; type 2 is in camera space (the player's shots: no camera matrix) |
| `0x0000` | 6 | object without rotation: `0, number, 0002, x, y, z` |
| `0x0100` | 11 | ? (not seen in attract mode) |
| `0x0101` | 14 | ? (not seen in attract mode) |

Positions are 16-bit world units; angles are Q15 sin/cos pairs (`(0, 32767)`
is 0 degrees); `order` (0..5, else 0) picks the rotation order.

**Object matrix**: `rot(pairs, order)`. For order 4 (the most used, also the
camera's), Q15 products:

```
[ c2*c3 + s1*s2*s3   s1*s2*c3 - c2*s3   c1*s2 ]
[ c1*s3              c1*c3              -s1   ]
[ s1*c2*s3 - s2*c3   s1*c2*c3 + s2*s3   c1*c2 ]
```

The six orders are the master's routines `892Bh, 896Ah, 89A9h, 89E8h, 8A27h,
8A66h`; `../tools/rot.py` runs them exactly (verified on 677 objects). As
products of rotations X(angle 1), Y(angle 2), Z(angle 3), with
`X = [1 0 0; 0 c -s; 0 s c]`, `Y = [c 0 s; 0 1 0; -s 0 c]`, `Z = [c -s 0; s c 0; 0 0 1]`:

| Order | Matrix |
|---|---|
| 0 | Z Y X |
| 1 | Y Z X |
| 2 | Z X Y |
| 3 | X Z Y |
| 4 | Y X Z |
| 5 | X Y Z |

## 4. The models: the point ROMs

24-bit words (the region `point24`, 32-bit big-endian); only the low 16 bits
of data words matter. `rom[number + 1]` is the address of the object's list
of parts, terminated by `FFFFFFh`; each part:

```
length (words, excluding itself)
quad index, vertex count n, depth bias
n x (x, y, z)
quad count m
m x (v0, v1, v2, v3, color)
```

(`../tools/ptrom.py`; `modelview.py` renders them.)
The game reads them from the ROM itself (`src/models.cpp`), model
`number + 1` when it is first drawn: one mesh a model (the parts merged, their
bias dropped: the game hides lines by true depth), each face with its color.

## 5. Drawing

For each object part: `world = R x vertex + position`, `view = M x world`
(type 2 records: `view = world`, they are already in camera space; checked
while firing: the shots then land on the reticle),
then

```
screen x = 248 + 582 * view.x / view.z
screen y = 240 - 797 * view.y / view.z      (496 x 480 picture, y down)
depth    = view.z
```

The two focal lengths come from the zoom (8192) and the aspect words; fitted
on the slave DSP's own output (median error 1.5-2.5 px, center offset < 1 px).
`overlay-check.png`: this projection drawn over MAME's picture of the same
frames.

The master also culls objects off screen (`8CA3h`) and sends the slaves a
header with the camera and projection: a re-creation does its own clipping.

## 6. Not covered yet

- Records `0x0100` / `0x0101` (gameplay only?).
- The 2D layer (C355 sprites: score, HUD, text, explosions, stars) and the
  palette: drawn by the 68000s, not the DSPs (`hardware.md`).
- Quad colors: palette `3E00h | color low byte`, `+100h` when bit 9 of the
  color word is clear; MAME darkens it by depth in 16 steps of 200h.
- Depth bias (each part's third word): the board sorts polygons by average
  depth + bias (a painter sort). Exact hidden-line removal does not need it:
  checked on the title (letters in front of the bar), the planet and the
  background model 2 (behind the ships), `../tools/occlusion_check.py`.
- Exact projection constants from the zoom word (`83E1h`, a table at `8B02h`).
