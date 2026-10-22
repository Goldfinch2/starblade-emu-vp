# Development notes

How Starblade Emulator is built, run and worked on. The project's overview is
the README at the top.

## The parts

Namco's Star Blade (1991, System 21) on a vector monitor. The game's own
68000 program runs (on Musashi); the rest of the board is replaced by what
it does:

- the DSP board's 3D: drawn as outlines with hidden lines removed;
- text (all of the game's fonts): the SDK's Asteroids font, fitted to the
  original letters;
- the rest of the sprite layer (shield, crosshair, logos): traced with the
  sprite editor (`C:\tmp\starblade-sprites`, saved as `sprites.vec`); a
  sprite not traced yet shows as a box with its ID;
- the sound board: its own program, on an emulated 6809, YM2151 and C140;
- the I/O MCU: the cabinet's controls and coins through the SDK.

How each part works: `dsp.md`, `hardware.md` (here, in `docs/`).

## Build (WSL2 Debian)

    cmake --preset linux && cmake --build --preset linux    # build-linux/starblade_emu, a WSLg window
    cmake --preset pc && cmake --build --preset pc          # build-pc/starblade_emu.exe
    cmake --build --preset pc --target windows              # C:\tmp\starblade-emu, ready to run
    cmake --preset pi && cmake --build --preset pi          # build-pi/starblade_emu
    cmake --build --preset pi --target vpk                  # the game package

## Run

It needs MAME's `starblad.zip` (Star Blade, ST2 World). It looks in, in order:
`--roms PATH`, `roms/` next to the executable, then the cabinet's ROM
folders (advmame.rc `dir_rom`).

    build-linux/starblade_emu --windowed --roms /mnt/c/tmp/mame/roms

On Windows: `C:\tmp\starblade-emu\Launch Starblade.cmd` (uses `C:\tmp\mame\roms`).
`--sprite-ids` shows the sprites not traced yet as boxes with their ID
(the sprite editor's Jump finds them); without it they are not drawn.

Controls: stick (or arrow keys, or mouse) aims, button 1 (or left mouse
button) gun, button 2 missile, coin, start; P pauses and resumes. Pausing saves the frame as
`save/capture_<frame>.png` (with the DSP RAM as `.bin` and the sprite list as `.txt`), to compare with the
original. The game's NVRAM (its settings) is kept in `save/starblad.nv`.

Development tools (`#define SB_DEV 1` in `src/main.cpp`; 0 as shipped:
then P only pauses): F5 saves the whole game as it is (`save/state.bin`: both 68000s, all memory,
the sound board), F7 loads it back, at any time, also right after starting:
to come back to a moment (a level, a scene) without playing up to it again.
A state is for this build: after a change to the game it may no longer load.
`--record SECONDS` loads that state at start, then writes a capture every
6 frames for SECONDS (`save/record_<frame>.png` at half size, and `.txt`, the
sprite list), then quits: a scene captured frame by frame, to look at after.

TAB, or START 1 and START 2 together, opens the CONTROLS page (the game
stands still meanwhile): GUN, MISSILE and the four aim keys can be laid out
on any key, button, hat or stick direction (START 1 on a row, then the
control; more on the same row add to it; START 2 none, DEL the standard, the
cabinet's). The stick, mouse and trackball aim as before. Kept in
`save/controls.ini`. AIM SPEED follows, Left/Right to change it (SLOW,
NORMAL, FAST, FASTER: the stick reaches the edge with less travel, the aim
keys and the mouse move faster), kept in `save/options.ini`. The stick has
no dead zone: it counts from its first movement, as on the arcade. Star Blade has no view change: the arcade's button 3 only
steps through its service menus.

## Sounds

The sound board runs its own program, as on the original (`src/soundboard.cpp`):
a 6809 (`src/m6809.cpp`) plays `st1-snd0.bin` from the player's
`starblad.zip`, driving a YM2151 (FM: [ymfm](https://github.com/aarongiles/ymfm),
BSD-3-Clause) and a C140 (PCM from `st1-voi0..3`; `src/c140.cpp`, after MAME's,
BSD-3-Clause). The game's sound commands reach it through the dual-port RAM
(`hardware.md`, section 4). Its sound goes to the SDK as one stream at the
YM2151's rate (55.9 kHz).

## Models

The point ROM's 3D models are read from the player's `starblad.zip` as the
game asks for them (`src/models.cpp`): nothing of Namco's is extracted or
shipped. One mesh a model (the ROM's parts merged; their depth biases, the
board's painter sort, dropped: the game hides lines by true depth). A face's
color is its color word looked up in the game's palette, every frame (fades,
flashes). The warp's streaks and the horizon band are drawn as lines (too many
outlines for a vector monitor).
