// roms.cpp: reading a game's <set>.zip (stored or deflated entries, through
// zlib's puff) and laying the files out the way MAME's ROM_LOAD macros do.
#include "roms.h"

#include "profile.h"

#include <cstdio>
#include <cstring>
#include <map>

#include <vectorpie/vp_paths.h>
#include <vectorpie/vp_rc.h>

extern "C" {
#include "puff.h"
}

namespace {

uint32_t le16(const uint8_t *p) { return p[0] | p[1] << 8; }
uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

bool readFile(const std::string &path, std::vector<uint8_t> &out)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(size > 0 ? (size_t)size : 0);
    bool ok = size > 0 && fread(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

// Every file of a zip, by name.
bool unzip(const std::vector<uint8_t> &zip, std::map<std::string, std::vector<uint8_t>> &files,
           std::string &error)
{
    // The end of central directory record: 22 bytes, plus a comment of up to 64 KB.
    long end = -1;
    for (long i = (long)zip.size() - 22; i >= 0 && i >= (long)zip.size() - 22 - 65535; i--) {
        if (le32(&zip[i]) == 0x06054b50) { end = i; break; }
    }
    if (end < 0) { error = "not a zip file"; return false; }
    uint32_t count = le16(&zip[end + 10]);
    uint32_t pos = le32(&zip[end + 16]);
    for (uint32_t n = 0; n < count; n++) {
        if (pos + 46 > zip.size() || le32(&zip[pos]) != 0x02014b50) { error = "damaged zip directory"; return false; }
        uint32_t method = le16(&zip[pos + 10]);
        uint32_t packed = le32(&zip[pos + 20]);
        uint32_t size = le32(&zip[pos + 24]);
        uint32_t nameLen = le16(&zip[pos + 28]);
        uint32_t extraLen = le16(&zip[pos + 30]);
        uint32_t commentLen = le16(&zip[pos + 32]);
        uint32_t local = le32(&zip[pos + 42]);
        std::string name((const char *)&zip[pos + 46], nameLen);
        pos += 46 + nameLen + extraLen + commentLen;

        if (local + 30 > zip.size() || le32(&zip[local]) != 0x04034b50) { error = "damaged zip entry " + name; return false; }
        uint32_t start = local + 30 + le16(&zip[local + 26]) + le16(&zip[local + 28]);
        if (start + packed > zip.size()) { error = "truncated zip entry " + name; return false; }
        std::vector<uint8_t> data(size);
        if (method == 0) {
            if (packed != size) { error = "damaged zip entry " + name; return false; }
            memcpy(data.data(), &zip[start], size);
        } else if (method == 8) {
            unsigned long outLen = size, inLen = packed;
            if (puff(data.data(), &outLen, &zip[start], &inLen) != 0 || outLen != size) {
                error = "cannot inflate " + name;
                return false;
            }
        } else {
            error = "unsupported compression in " + name;
            return false;
        }
        size_t slash = name.find_last_of('/');
        files[slash == std::string::npos ? name : name.substr(slash + 1)] = std::move(data);
    }
    return true;
}

// The places <set>.zip (`zip`) may be.
std::vector<std::string> candidates(const std::string &path, const std::string &zip)
{
    std::vector<std::string> out;
    auto addDir = [&](std::string dir) {
        while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
        if (!dir.empty()) out.push_back(dir + "/" + zip);
    };
    if (!path.empty()) {
        if (path.size() > 4 && path.compare(path.size() - 4, 4, ".zip") == 0) out.push_back(path);
        else addDir(path);
        return out;
    }
    char buf[1024];
    out.push_back(vp_asset_path(("roms/" + zip).c_str(), buf, sizeof buf));
    vp_rc *rc = nullptr;
    if (vp_rc_load(nullptr, &rc) == VP_OK) {
        const char *dirs = vp_rc_get(rc, "dir_rom");
        if (dirs) {
            std::string all(dirs), dir;
#ifdef _WIN32
            const char sep = ';';
#else
            const char sep = ':';
#endif
            for (char c : all) {
                if (c == sep) { addDir(dir); dir.clear(); }
                else dir += c;
            }
            addDir(dir);
        }
        vp_rc_free(rc);
    }
#ifndef _WIN32
    addDir("/persistent/games/rom");
    addDir("/home/pi/.advance/rom");
    addDir("/usr/local/share/advance/rom");
#endif
    return out;
}

// ROM_LOAD16_BYTE / ROM_LOAD32_BYTE: every `step` bytes from `offset`.
bool place(std::map<std::string, std::vector<uint8_t>> &files, const std::string &zip, const RomFile &rf,
           std::vector<uint8_t> &region, std::string &error)
{
    auto it = files.find(rf.name);
    if (it == files.end() || it->second.size() != rf.size) {
        error = zip + ": " + rf.name + (it == files.end() ? " is missing" : " has the wrong size");
        return false;
    }
    if (rf.offset + (size_t)(rf.size - 1) * rf.step >= region.size()) {
        error = zip + ": " + rf.name + " does not fit its region";
        return false;
    }
    for (size_t i = 0; i < rf.size; i++) region[rf.offset + i * rf.step] = it->second[i];
    return true;
}

} // namespace

bool roms_load(const std::string &path, const Profile &game, Roms &roms, std::string &error)
{
    const std::string zipName = std::string(game.set) + ".zip";
    std::vector<uint8_t> zip;
    std::string found;
    for (const std::string &c : candidates(path, zipName)) {
        if (readFile(c, zip)) { found = c; break; }
    }
    if (found.empty()) {
        error = zipName + " (MAME's " + game.title + " ROM set) not found";
        if (!path.empty()) error += " at " + path;
        return false;
    }
    std::map<std::string, std::vector<uint8_t>> files;
    if (!unzip(zip, files, error)) { error = found + ": " + error; return false; }

    // The sprites' region is as big as the game's ROMs fill (2 MB, 4 MB): the
    // C355 wraps tile numbers at its size.
    size_t spriteBytes = 0x200000;
    for (size_t k = 0; k < game.nroms; k++) {
        const RomFile &rf = game.roms[k];
        if (rf.region != Region::Sprites) continue;
        while (spriteBytes < rf.offset + (size_t)(rf.size - 1) * rf.step + 1) spriteBytes *= 2;
    }
    roms.main.assign(0x100000, 0);
    roms.slave.assign(0x100000, 0);
    roms.data.assign(0x100000, 0);
    roms.edata.assign(0x100000, 0xff);
    roms.sprites.assign(spriteBytes, 0);
    roms.sound.assign(0x20000, 0);
    roms.voices.assign(0x200000, 0);
    // The point ROM: each 24-bit word from three files (high, middle, low
    // byte); 1M words.
    std::vector<uint8_t> pointH(0x100000, 0), pointU(0x100000, 0), pointL(0x100000, 0);
    for (size_t k = 0; k < game.nroms; k++) {
        const RomFile &rf = game.roms[k];
        std::vector<uint8_t> *region = nullptr;
        switch (rf.region) {
        case Region::Main:    region = &roms.main; break;
        case Region::Slave:   region = &roms.slave; break;
        case Region::Data:    region = &roms.data; break;
        case Region::EData:   region = &roms.edata; break;
        case Region::Sprites: region = &roms.sprites; break;
        case Region::Sound:   region = &roms.sound; break;
        case Region::Voices:  region = &roms.voices; break;
        case Region::PointH:  region = &pointH; break;
        case Region::PointU:  region = &pointU; break;
        case Region::PointL:  region = &pointL; break;
        }
        if (!place(files, zipName, rf, *region, error)) return false;
    }
    roms.points.assign(0x100000, 0);
    for (size_t i = 0; i < roms.points.size(); i++) {
        int32_t v = pointH[i] << 16 | pointU[i] << 8 | pointL[i];
        roms.points[i] = v & 0x800000 ? v - 0x1000000 : v;
    }

    roms.nvram.assign(0x2000, 0);
    if (game.nvram) {
        auto nv = files.find(game.nvram);
        if (nv != files.end() && nv->second.size() == 0x2000) roms.nvram = nv->second;
    }
    return true;
}
