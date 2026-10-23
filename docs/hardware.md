# Star Blade: the chips around the 68000s

What the re-creation does in place of each chip of Namco System 21 (C67),
worked out from MAME's driver (`src/mame/namco/namcos21_c67.cpp` and the
devices it uses) and captures of the original running in MAME (the Lua
scripts in `../tools`). The DSP board is in `dsp.md`.

The two 68000s run the game's own code (Musashi). Everything else is ours.

## 1. Memory maps (both 68000s)

| Address | What | Here |
|---|---|---|
| `000000` | program ROM (master 1 MB, slave 512 KB) | |
| `100000` | work RAM (master 64 KB, slave 256 KB) | |
| `180000-183FFF` | NVRAM, low byte of each word (master) | saved as `save/starblad.nv` |
| `1C0000-1FFFFF` | C148, one per CPU (section 2) | |
| `200000-20FFFF` | DSP RAM (`dsp.md`) | |
| `400000`, `440000` | point RAM index reset, data (quad lists for the DSPs) | kept, unused |
| `480000-4807FF` | depth cue tables | kept, unused |
| `700000-71FFFF` | C355 sprite RAM (section 5) | |
| `720000-720007` | C355 scroll | |
| `740000`, `750000` | palette: R<<8\|G words, B words | |
| `760000` | video enable (bit 6) | |
| `800000-8FFFFF` | data ROM | |
| `900000-90FFFF` | RAM shared by the 68000s | |
| `A00000-A00FFF` | dual-port RAM, low byte of each word (sections 3, 4) | |
| `B00000`, `B80000` | C139 serial RAM, status (reads 4) | |
| `C00000-DFFFFF` | expansion data, not fitted (reads FFFF) | |

## 2. C148 interrupt controllers

Per CPU, at `1C0000` + (byte offsets, each block 2000h long):

| Offset | Read | Write |
|---|---|---|
| `04000` | bus register | bus register |
| `06000`-`0E000` | level of: CPU, EX, POS, SCI, VBLANK interrupts | set level (clears that line first) |
| `10000` | | interrupt the OTHER CPU (at its CPU level) |
| `16000`-`1E000` | acknowledge (clear) CPU, EX, POS, SCI, VBLANK | the same |
| `20000` | 7 (EEPROM ready) | |
| `22000` | | master: sound CPU reset, bit 0 = run |
| `24000` | | master: slave 68000 + I/O MCU + DSPs reset, bit 0 = run |

VBLANK is raised on both CPUs at the start of vertical blank (line 480 of
525, 59.94 Hz) and held until taken; the others stay until acknowledged.
Interrupts are autovectored. At power-on the slave, the MCU and the sound CPU
are held in reset until the master lets them go.

## 3. The C68 I/O MCU (dual-port RAM 000h-0FFh)

Offsets are dual-port RAM bytes (68000 address `A00000 + 2 x offset`).

| Offset | What |
|---|---|
| `00` | buttons held: `80` start, `40` button 3 (view), `20` button 2 (gun), `10` button 1 (gun 2: the game fires the same for both) |
| `03` | the same bits, pressed this frame |
| `02` | coin switches held: `10` coin 1, `08` coin 2 (`20` test, `40` service) |
| `05` | coins inserted this frame (same bits) |
| `08-0F` | analog channels: `09` stick X, `0A` stick Y (`3F..BF`, center `80`), others `FF` |
| `13` | `20` (switches) |
| `16`, `17` | written by the game: coins to take (2 for a start; 4 seen at a continue) |
| `19`, `1B` | coins counted on coin 1 / coin 2: the game shows CREDIT n/2 |
| `46`, `47` | coin meters |
| `1F`, `21`, `23` | frame counters |
| `3A` | `A6` after reset; the game writes `3B` = `FF`, the MCU answers `3C` = `A6`, `3B` = `00` |
| `06`=`01`, `07`=`09`, `11`, `12`=`01`, `14`=`02`, `1D`=`07`, `44`=`01`, `56`, `57`=`01` | set at reset (`35` = `FF` from the game is answered `00`) |

A coin: `02` while the switch is closed, the next frame `05` and the count
in `19` goes up. The MCU writes its counts every frame; when the game writes
a number to `16`, the MCU subtracts it.

## 4. The sound board (dual-port RAM 100h-4FFh, 7FEh-7FFh)

Three areas of 8 slots, each slot two bytes (cue number, flag):
`100h` FM, `200h` PCM (effects, speech), `400h` sequences (music).

The game writes the cue number, then `40h`. The sound CPU answers `80h`
(playing), then `00h` when the cue is over. Writing `40h` again restarts it.
`7FFh` is the sound CPU's heartbeat (counts up twice a frame); `7FEh` is 0.

Seen in attract mode: music `400h`: 21h, 01h; PCM: 05, 06, 1A, 1C, 1E, 35,
46, 47, 48, 56, 57.

The re-creation runs the board's own program (`src/soundboard.cpp`): a
6809 at 2.048 MHz (bank select at C001h), the YM2151 at 3.58 MHz, the C140
at 21.3 kHz, whose timer is the 6809's only interrupt (FIRQ).

## 5. The C355 sprites

Sprite RAM (words from `700000`): attributes at `0000h` (8 words a sprite:
format link, tile offset, x, y, width + flip, height + flip, palette/clip/
priority), the list at `2000h/2` (sprite numbers, bit 8 = last), clip
windows at `2400h/2`, formats at `4000h/2` (first tile, columns/rows,
origin x, y), tile numbers at `8000h/2` (bit 15 = blank). The game writes
the second copies at `10000h` and `14000h`, mirrored into the first.
Tiles are 16x16, one byte a pixel, `FFh` transparent; pixel colors from
palette `1000h + 256 x (palette & 15) + pixel`. Scroll: X = `720002`
(10 bits), Y = `720000` (9 bits) + 20h.

Text: the tiles are fonts. 20h-7Fh is ASCII (60h-7Fh repeat 20h-3Fh;
5Bh-5Fh are box pieces), five more fonts are laid out A-Z, 0-9 and then a few
symbols (A at 194h, 1BBh, 1E2h: `-&/`; 209h: `-&`; 22Fh: `.!`; 255h: `.`),
and three digit sets start at 399h (score), 3A3h and 3ADh (distance). The
re-creation draws every font tile as that character in the SDK's Asteroids
font, fitted to the original glyph's box.

The re-creation draws the other sprites from vector traces made with the sprite
editor (`C:\tmp\starblade-sprites`, catalog from `tools/sb_sprcat.lua`),
saved as `sprites.vec`: a sprite image traced as a whole (keyed by a hash of
its size, tile offset and tile list), or tile by tile. A sprite not traced
yet is drawn as its box with its ID (`src/sprites.cpp`).
