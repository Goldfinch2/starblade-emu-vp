// soundboard.h: the game's sound board, running its own program from the
// player's ROM set: a 6809 (Star Blade: st1-snd0.bin), a YM2151 (FM: ymfm) and a
// C140 (PCM: st1-voi0..3), talking to the game through the dual-port RAM
// as on the real board (docs/hardware.md, section 4). What it plays goes to
// the SDK as one stream.
//
// The 6809's map (MAME's namcos21_c67.cpp):
//   0000-3FFF  program ROM, 16 KB banks (C001h: bank = data >> 4)
//   4000-4001  YM2151           5000-5FFF  C140 (registers mirrored every 200h)
//   7000-7FFF  dual-port RAM (800h, mirrored)   8000-9FFF  RAM
//   C000-FFFF  program ROM, its first 16 KB
// Its only interrupt is the C140's timer, on FIRQ.
#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#include <vectorpie/vp_audio.h>

#include "c140.h"
#include "m6809.h"
#include "ymfm_opm.h"

struct Roms;
class State;

class Sound : private M6809::Bus, private ymfm::ymfm_interface {
public:
    Sound();
    ~Sound() override;
    // After the ROMs are loaded: `dpram` is the machine's dual-port RAM.
    void open(vp_audio *audio, const Roms &roms, uint8_t *dpram);
    void close();
    // The master 68000 holds the board in reset (bit 0 of 1C22000h clear) or
    // lets it run (the 6809 starts from its reset vector).
    void reset(bool held);
    // One video frame of the board, its sound sent to the SDK.
    void runFrame();
    // What it is doing, as text: for the pause capture.
    void list(FILE *out) const;
    // Save or load everything it is: the 6809, its RAM, both chips, the timing.
    void state(State &s);

private:
    uint8_t read(uint16_t address) override;
    void write(uint16_t address, uint8_t data) override;
    void ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks) override;

    const Roms *roms_ = nullptr;
    uint8_t *dpram_ = nullptr;
    M6809 cpu_{ *this };
    ymfm::ym2151 ym_{ *this };
    std::unique_ptr<C140> c140_;
    uint8_t ram_[0x2000] = {};
    int bank_ = 0;
    bool held_ = true;
    int32_t ymTimer_[2] = { -1, -1 };   // YM2151 clocks until timer A / B, -1: off

    double cpuDue_ = 0, c140Due_ = 0, samplesDue_ = 0;
    float c140Prev_[2] = {}, c140Now_[2] = {};

    vp_audio *audio_ = nullptr;
    vp_voice voice_ = 0;
    std::vector<float> out_;
};
