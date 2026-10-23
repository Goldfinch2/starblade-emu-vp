# System 21 for VectorPie

Namco's **System 21** arcade games running on a vector monitor, for the
[VectorPie](https://github.com/Goldfinch2/VectorPie) arcade cabinet: a
Raspberry Pi driving a vector monitor through a USB-DVG, or a window on a PC.

| Game | Package | MAME ROM set |
|---|---|---|
| **Star Blade** (1991) | Starblade Emulator (`starblade_emu`) | `starblad.zip` |
| **Cyber Sled** (1993) | Cyber Sled Emulator (`cybersled_emu`) | `cybsled.zip` |

The game's own program runs as it did on the arcade board, and what that
board drew is redrawn as vectors:

- **the two 68000 CPUs** run the original code (Musashi);
- **the 3D world**: the arcade's DSP board is replaced by our own code, the
  models read from the ROM set and drawn with hidden lines through the
  VectorPie SDK;
- **the sprites** (HUD, text, menus) are drawn as vector tracings
  (each game's `sprites.vec`) and the game's text in a vector font;
- **the sound board** runs its own program: a 6809, a YM2151 (FM) and a
  C140 (PCM), emulated;
- **the controls**: the cabinet's, and the player can lay them out in the
  game (TAB, or START 1 and START 2 together, on the title screen).

One program runs every game; each game is its own package, its files in
`games/<set>/`.

## You need the ROMs

**No ROM, sound, model or picture of the original games is included.**
Everything is read at run time from **your own** MAME ROM set. The game
looks for it in:

1. `--roms PATH` (a folder holding the zip, or the zip itself);
2. a `roms/` folder next to the executable;
3. the cabinet's ROM folders (`dir_rom` in AdvanceMAME's `advmame.rc`).

## Build

With the [VectorPie SDK](https://github.com/Goldfinch2/VectorPie) installed
(`~/VectorPie/dist/pc` and `~/VectorPie/dist/pi`), from WSL2 Debian or Linux:

    cmake --preset pc && cmake --build --preset pc               # build-pc/system21_emu.exe
    cmake --preset pi && cmake --build --preset pi --target vpk  # build-pi/<package>-<version>.vpk, every game's

One game's package: `--target vpk_starblad` or `--target vpk_cybsled`.

Run on a PC:

    build-pc/system21_emu.exe --game cybsled --windowed --roms /path/to/roms

## On the cabinet

Copy the `.vpk` to `/persistent/games/packages/` (or a USB drive's `games/`
folder) and install it from **Settings → GAMES → INSTALL GAMES**. Put the
game's ROM set with the cabinet's other MAME ROMs.

## Working on it

How it is built, run, debugged and compared with MAME: `docs/development.md`.

## Credits and licenses

- **Star Blade** © Namco 1991, **Cyber Sled** © Namco 1993. They are
  trademarks of Bandai Namco. This project is not affiliated with or endorsed
  by Bandai Namco.
- **Musashi** 68000 emulator: Karl Stenerud, MIT-style license
  (`third_party/Musashi/readme.txt`).
- **ymfm** YM2151 emulator: Aaron Giles, BSD-3-Clause (`third_party/ymfm/LICENSE`).
- **puff** inflate: Mark Adler, zlib license (`third_party/puff/puff.h`).
- The C140 is a port of MAME's (`src/c140.cpp`, BSD-3-Clause, R. Belmont).
