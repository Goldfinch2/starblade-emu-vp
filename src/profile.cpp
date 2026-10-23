// profile.cpp: the games (profile.h). Star Blade's values are the ones the
// emulator was built on; another game's come from MAME's namcos21_c67.cpp and
// from the game itself.
#include "profile.h"

#include <cstdio>
#include <cstring>
#include <string>

#include <vectorpie/vp_paths.h>

namespace {

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

// ---- Star Blade (starblad: ST2, World) -------------------------------------------

const RomFile STARBLAD_ROMS[] = {
    { "st2_mpu.mpru",   Region::Main,    0, 2, 0x80000 },
    { "st2_mpl.mprl",   Region::Main,    1, 2, 0x80000 },
    { "st1-sp-u.bin",   Region::Slave,   0, 2, 0x40000 },
    { "st1-sp-l.bin",   Region::Slave,   1, 2, 0x40000 },
    { "st1-data-u.bin", Region::Data,    0, 2, 0x20000 },
    { "st1-data-l.bin", Region::Data,    1, 2, 0x20000 },
    { "st1-obj0.bin",   Region::Sprites, 0, 4, 0x80000 },
    { "st1-obj1.bin",   Region::Sprites, 1, 4, 0x80000 },
    { "st1-obj2.bin",   Region::Sprites, 2, 4, 0x80000 },
    { "st1-obj3.bin",   Region::Sprites, 3, 4, 0x80000 },
    { "st1-snd0.bin",   Region::Sound,   0, 1, 0x20000 },
    { "st1-voi0.bin",   Region::Voices,  0x000000, 1, 0x80000 },
    { "st1-voi1.bin",   Region::Voices,  0x080000, 1, 0x80000 },
    { "st1-voi2.bin",   Region::Voices,  0x100000, 1, 0x80000 },
    { "st1-voi3.bin",   Region::Voices,  0x180000, 1, 0x80000 },
    // the point ROM: each 24-bit word's high, middle and low byte, two banks of 512K
    { "st1-pt0-h.bin",  Region::PointH,  0x00000, 1, 0x80000 },
    { "st1-pt0-u.bin",  Region::PointU,  0x00000, 1, 0x80000 },
    { "st1-pt0-l.bin",  Region::PointL,  0x00000, 1, 0x80000 },
    { "st1-pt1-h.bin",  Region::PointH,  0x80000, 1, 0x80000 },
    { "st1-pt1-u.bin",  Region::PointU,  0x80000, 1, 0x80000 },
    { "st1-pt1-l.bin",  Region::PointL,  0x80000, 1, 0x80000 },
};

// The stick and the trackball aim as they are; the aim keys push the sight
// like a stick (hle.cpp): no button bit.
const GameFunction STARBLAD_FUNCTIONS[] = {
    { "gun",       "GUN",        "p1_button1", 0x20 },
    { "missile",   "GUN 2",      "p1_button2", 0x10 },   // the same trigger, a second button
    { "aim_up",    "AIM UP",     "p1_up",      0 },
    { "aim_down",  "AIM DOWN",   "p1_down",    0 },
    { "aim_left",  "AIM LEFT",   "p1_left",    0 },
    { "aim_right", "AIM RIGHT",  "p1_right",   0 },
};

// The warp: 24 lengths of one streak (and again); the horizon band.
const CodeRange STARBLAD_STREAKS[] = { { 1297, 1320 }, { 4743, 4766 } };
const CodeRange STARBLAD_BACKDROPS[] = { { 2, 2 }, { 3448, 3448 } };

// The ASCII font at 20h-7Fh is every game's (sprites.cpp); these are Star
// Blade's five other fonts, its three digit sets, and their extras.
const FontRange STARBLAD_FONTS[] = {
    { 0x194, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-&/" },
    { 0x1bb, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-&/" },
    { 0x1e2, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-&/" },
    { 0x209, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-&" },
    { 0x22f, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.!" },
    { 0x255, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789." },
    { 0x399, "0123456789" },
    { 0x3a3, "0123456789" },
    { 0x3ad, "0123456789" },
};
// The coordinates' letters after the digit sets (the Red Eye briefing's
// X+ 2913.64 ...): X Y Z, the same slanted, a minus and a plus; the point.
const FontChar STARBLAD_CHARS[] = {
    { 0x3bd, '.' },
    { 0x3b7, 'X' }, { 0x3b8, 'Y' }, { 0x3b9, 'Z' },
    { 0x3ba, 'X' }, { 0x3bb, 'Y' }, { 0x3bc, 'Z' },
    { 0x3bf, '-' }, { 0x3c0, '+' },
};
const FontWord STARBLAD_WORDS[] = {
    { 0x3c1, "OK" },            // the preflight checklist's green OK
};

const Profile PROFILES[] = {
    {
        "starblad", "Star Blade",
        STARBLAD_ROMS, COUNT(STARBLAD_ROMS), "starblad.nv",
        STARBLAD_FUNCTIONS, COUNT(STARBLAD_FUNCTIONS),
        0x09, 0x0a,
        STARBLAD_STREAKS, COUNT(STARBLAD_STREAKS),
        STARBLAD_BACKDROPS, COUNT(STARBLAD_BACKDROPS),
        STARBLAD_FONTS, COUNT(STARBLAD_FONTS),
        STARBLAD_CHARS, COUNT(STARBLAD_CHARS),
        STARBLAD_WORDS, COUNT(STARBLAD_WORDS),
    },
};

// game.ini's "[system21]" section, its "game = <set>" line; empty if none.
std::string manifestGame()
{
    char path[1024];
    FILE *f = fopen(vp_asset_path("game.ini", path, sizeof path), "r");
    if (!f) return std::string();
    char line[256], set[64];
    bool in = false;
    std::string game;
    while (fgets(line, sizeof line, f)) {
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') { in = !strncmp(p, "[system21]", 10); continue; }
        if (in && sscanf(p, "game = %63[a-z0-9_]", set) == 1) game = set;
    }
    fclose(f);
    return game;
}

} // namespace

const Profile *profile_find(const char *set)
{
    for (const Profile &p : PROFILES)
        if (set && !strcmp(p.set, set)) return &p;
    return nullptr;
}

const Profile &profile()
{
    static const Profile *chosen = nullptr;
    if (!chosen) {
        const std::string game = manifestGame();
        chosen = profile_find(game.c_str());
        if (!chosen) {
            if (!game.empty()) fprintf(stderr, "starblade: game.ini names %s, a game not known: Star Blade\n", game.c_str());
            chosen = &PROFILES[0];
        }
    }
    return *chosen;
}
