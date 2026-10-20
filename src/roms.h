// roms.h: the Star Blade ROM set (MAME's starblad.zip), read straight from the zip.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct Roms {
    std::vector<uint8_t> main;      // master 68000 program, 1 MB, as the CPU sees it (big-endian)
    std::vector<uint8_t> slave;     // slave 68000 program, 512 KB
    std::vector<uint8_t> data;      // data ROM at 0x800000, 1 MB (256 KB loaded)
    std::vector<uint8_t> sprites;   // C355 sprite tiles, 2 MB: 16x16 pixels, one byte each
    std::vector<uint8_t> nvram;     // the default settings, 8 KB
    std::vector<uint8_t> sound;     // the sound board's 6809 program, 128 KB (st1-snd0.bin)
    std::vector<uint8_t> voices;    // the C140's samples, st1-voi0..3, 512 KB each, one after the other
    std::vector<int32_t> points;    // the point ROM: the 3D models, 1M 24-bit words, signed (models.cpp)
};

// Finds starblad.zip: `path` if not empty (a zip, or a folder holding it), else
// the package's roms folder, then advmame.rc's dir_rom folders.
// Returns false with `error` set when it is missing or incomplete.
bool roms_load(const std::string &path, Roms &roms, std::string &error);
