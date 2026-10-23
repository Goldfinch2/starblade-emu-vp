# Development notes

How the System 21 emulator is built, run and worked on. The project's
overview is the README at the top.

## The parts

Namco's System 21 games (Star Blade 1991, Cyber Sled 1993) on a vector
monitor. The game's own 68000 program runs (on Musashi); the rest of the
board is replaced by what it does:

- the DSP board's 3D: drawn as outlines with hidden lines removed;
- text (all of the game's fonts): the SDK's Asteroids font, fitted to the
  original letters;
- the rest of the sprite layer (HUD, logos, frames): vector traces in the
  game's `sprites.vec`; a sprite not traced yet is not drawn;
- the sound board: its own program, on an emulated 6809, YM2151 and C140;
- the I/O MCU: the cabinet's controls and coins through the SDK.

How each part works: `dsp.md`, `hardware.md` (here, in `docs/`). What is one
game's (its ROM set, controls, fonts...) is its profile (`src/profile.cpp`),
below.

## Build (WSL2 Debian)

    cmake --preset linux && cmake --build --preset linux    # build-linux/system21_emu, a WSLg window
    cmake --preset pc && cmake --build --preset pc          # build-pc/system21_emu.exe
    cmake --build --preset pc --target windows              # C:\tmp\system21-emu, ready to run
    cmake --preset pi && cmake --build --preset pi          # build-pi/system21_emu
    cmake --build --preset pi --target vpk                  # every game's package

## Run

It needs MAME's ROM set of the game (`starblad.zip`, `cybsled.zip`). It looks
in, in order: `--roms PATH`, `roms/` next to the executable, then the
cabinet's ROM folders (advmame.rc `dir_rom`).

    build-linux/system21_emu --game cybsled --windowed --roms /mnt/c/tmp/mame/roms

On Windows: `C:\tmp\system21-emu\Launch <game>.cmd`, one for each game (uses
`C:\tmp\mame\roms`). Without `--game`, the game is the one whose files were
copied next to the program (`-DS21_GAME=cybsled`; Star Blade by default).

Controls: the game's own, laid out on its CONTROLS page (below); coin,
start; P pauses and resumes. Pausing saves the frame as
`save/capture_<frame>.png` (with the DSP RAM as `.bin` and the sprite list as
`.txt`), to compare with the original. The game's NVRAM (its settings) is
kept in `save/<set>.nv`.

Development tools (`#define S21_DEV 1` in `src/main.cpp`; 0 as shipped:
then P only pauses): F5 saves the whole game as it is (`save/state.bin`: both 68000s, all memory,
the sound board), F7 loads it back, at any time, also right after starting:
to come back to a moment (a level, a scene) without playing up to it again.
A state is for this build: after a change to the game it may no longer load.
`--record SECONDS` loads that state at start, then writes a capture every
6 frames for SECONDS (`save/record_<frame>.png` at half size, and `.txt`, the
sprite list), then quits: a scene captured frame by frame, to look at after.

TAB, or START 1 and START 2 together, opens the CONTROLS page (the game
stands still meanwhile): GUN, GUN 2 and the four aim keys can be laid out
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

## Other System 21 games: profiles

The emulator is a Namco System 21 (C67 DSP board) one: the machine, the DSP
board's stand-in, the sprites and the sound board are every game's. What is
one game's is its profile (`src/profile.cpp`): its ROM set (MAME's zip, each
file and where it goes), its factory settings, its functions and their I/O
board button bits, the stick's channels, the models drawn as lines, and its
fonts' tiles. The package says which game it is, in `game.ini`:

    [system21]
    game = starblad

The profiles so far: Star Blade (`starblad`) and Cyber Sled (`cybsled`, being
started: its ROM set, controls and sticks; no traces, no models drawn as lines,
its DSP boot checksums not captured yet). The others on the same board (MAME's
`namcos21_c67.cpp`): Solvalou (`solvalou`) and Air Combat (`aircomb`).

Each game is its own package, its files in `games/<set>/`: `game.ini` (the
package id, the executable's name, `[system21] game`), `leds.cfg`,
`sprites.vec`, `guide/*.wav` and the marquee. The program is the same for
all, copied under each package's executable name:

    cmake --build --preset pi --target vpk            # every game's package
    cmake --build --preset pi --target vpk_cybsled    # one game's

From a build folder, `--game cybsled` runs another game than the one whose
files were copied next to the program (`-DS21_GAME=cybsled` copies Cyber
Sled's).
