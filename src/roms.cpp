// roms.cpp: reading starblad.zip (stored or deflated entries, through zlib's puff)
// and laying the files out the way MAME's ROM_LOAD macros do.
#include "roms.h"

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

// The places starblad.zip may be.
std::vector<std::string> candidates(const std::string &path)
{
    std::vector<std::string> out;
    auto addDir = [&](std::string dir) {
        while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
        if (!dir.empty()) out.push_back(dir + "/starblad.zip");
    };
    if (!path.empty()) {
        if (path.size() > 4 && path.compare(path.size() - 4, 4, ".zip") == 0) out.push_back(path);
        else addDir(path);
        return out;
    }
    char buf[1024];
    out.push_back(vp_asset_path("roms/starblad.zip", buf, sizeof buf));
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
bool place(std::map<std::string, std::vector<uint8_t>> &files, const char *name,
           std::vector<uint8_t> &region, size_t offset, size_t step, size_t size, std::string &error)
{
    auto it = files.find(name);
    if (it == files.end() || it->second.size() != size) {
        error = std::string("starblad.zip: ") + name + (it == files.end() ? " is missing" : " has the wrong size");
        return false;
    }
    for (size_t i = 0; i < size; i++) region[offset + i * step] = it->second[i];
    return true;
}

} // namespace

bool roms_load(const std::string &path, Roms &roms, std::string &error)
{
    std::vector<uint8_t> zip;
    std::string found;
    for (const std::string &c : candidates(path)) {
        if (readFile(c, zip)) { found = c; break; }
    }
    if (found.empty()) {
        error = "starblad.zip (MAME's Star Blade ROM set) not found";
        if (!path.empty()) error += " at " + path;
        return false;
    }
    std::map<std::string, std::vector<uint8_t>> files;
    if (!unzip(zip, files, error)) { error = found + ": " + error; return false; }

    roms.main.assign(0x100000, 0);
    roms.slave.assign(0x80000, 0);
    roms.data.assign(0x100000, 0);
    roms.sprites.assign(0x200000, 0);
    bool ok =
        place(files, "st2_mpu.mpru", roms.main, 0, 2, 0x80000, error) &&
        place(files, "st2_mpl.mprl", roms.main, 1, 2, 0x80000, error) &&
        place(files, "st1-sp-u.bin", roms.slave, 0, 2, 0x40000, error) &&
        place(files, "st1-sp-l.bin", roms.slave, 1, 2, 0x40000, error) &&
        place(files, "st1-data-u.bin", roms.data, 0, 2, 0x20000, error) &&
        place(files, "st1-data-l.bin", roms.data, 1, 2, 0x20000, error) &&
        place(files, "st1-obj0.bin", roms.sprites, 0, 4, 0x80000, error) &&
        place(files, "st1-obj1.bin", roms.sprites, 1, 4, 0x80000, error) &&
        place(files, "st1-obj2.bin", roms.sprites, 2, 4, 0x80000, error) &&
        place(files, "st1-obj3.bin", roms.sprites, 3, 4, 0x80000, error);
    if (!ok) return false;

    // The sound board: its program, and the C140's samples.
    roms.sound.assign(0x20000, 0);
    roms.voices.assign(0x200000, 0);
    ok = place(files, "st1-snd0.bin", roms.sound, 0, 1, 0x20000, error) &&
         place(files, "st1-voi0.bin", roms.voices, 0x000000, 1, 0x80000, error) &&
         place(files, "st1-voi1.bin", roms.voices, 0x080000, 1, 0x80000, error) &&
         place(files, "st1-voi2.bin", roms.voices, 0x100000, 1, 0x80000, error) &&
         place(files, "st1-voi3.bin", roms.voices, 0x180000, 1, 0x80000, error);
    if (!ok) return false;

    // The point ROM: each 24-bit word from three files (high, middle, low
    // byte), two banks of 512K words.
    roms.points.assign(0x100000, 0);
    static const char *const BANKS[2][3] = {
        { "st1-pt0-h.bin", "st1-pt0-u.bin", "st1-pt0-l.bin" },
        { "st1-pt1-h.bin", "st1-pt1-u.bin", "st1-pt1-l.bin" },
    };
    for (int bank = 0; bank < 2; bank++) {
        const std::vector<uint8_t> *part[3];
        for (int k = 0; k < 3; k++) {
            auto it = files.find(BANKS[bank][k]);
            if (it == files.end() || it->second.size() != 0x80000) {
                error = std::string("starblad.zip: ") + BANKS[bank][k] +
                        (it == files.end() ? " is missing" : " has the wrong size");
                return false;
            }
            part[k] = &it->second;
        }
        for (size_t i = 0; i < 0x80000; i++) {
            int32_t v = (*part[0])[i] << 16 | (*part[1])[i] << 8 | (*part[2])[i];
            roms.points[bank * 0x80000 + i] = v & 0x800000 ? v - 0x1000000 : v;
        }
    }

    auto nv = files.find("starblad.nv");
    roms.nvram.assign(0x2000, 0);
    if (nv != files.end() && nv->second.size() == 0x2000) roms.nvram = nv->second;
    return true;
}
