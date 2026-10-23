// profile.h: what is particular to one Namco System 21 (C67) game: its ROM set,
// its controls, the models drawn as lines, its fonts. The machine, the DSP
// board's stand-in, the sprites and the sound board are the same for all; a
// game is a profile. The package says which (game.ini: "[system21]",
// "game = starblad"), Star Blade when it does not.
#pragma once

#include <cstddef>
#include <cstdint>

// Where a ROM file goes (roms.h).
enum class Region { Main, Slave, Data, Sprites, Sound, Voices, PointH, PointU, PointL };

struct RomFile {
    const char *name;       // in the zip
    Region region;
    uint32_t offset;        // its first byte's place in the region
    uint32_t step;          // 1, or 2 / 4 for interleaved halves and quarters
    uint32_t size;          // its size, checked
};

// A function the player lays out (vp_controls.h), and the I/O board's button
// bit it sets (dual-port RAM byte 00h: docs/hardware.md, section 3).
struct GameFunction {
    const char *name;       // vp_input's: "gun"
    const char *label;      // on the CONTROLS page: "GUN"
    const char *standard;   // out of the box: "p1_button1"
    uint8_t bit;
};

// One of the game's fonts in the sprite ROM: its tiles from `base`, one per
// character of `chars`.
struct FontRange { int base; const char *chars; };
struct FontChar { int tile; char ch; };
struct FontWord { int tile; const char *text; };   // a tile that shows a short word

// Model codes drawn as lines (render3d.cpp, models.cpp), [first, last].
struct CodeRange { int first, last; };

struct Profile {
    const char *set;                    // MAME's set: "starblad" (starblad.zip)
    const char *title;                  // "Star Blade"
    const RomFile *roms; size_t nroms;
    const char *nvram;                  // the factory settings in the zip ("starblad.nv"), or nullptr
    const GameFunction *functions; size_t nfunctions;
    int stickX, stickY;                 // the dual-port RAM bytes the stick's channels are read from
    const CodeRange *streaks; size_t nstreaks;     // drawn as their long diagonals
    const CodeRange *backdrops; size_t nbackdrops; // pushed out behind everything, their stripes halved
    const FontRange *fonts; size_t nfonts;
    const FontChar *chars; size_t nchars;          // single characters outside the ranges
    const FontWord *words; size_t nwords;
};

// The game this package is: from game.ini's [system21] game, Star Blade if
// none. Chosen once, at start.
const Profile &profile();
// The profile of a set ("starblad"), or nullptr.
const Profile *profile_find(const char *set);
