// c140.h: the Namco C140, 24 voices of PCM from the sample ROMs, and the timer
// that interrupts the sound CPU (its FIRQ). Ported from MAME's c140.cpp
// (BSD-3-Clause, copyright R. Belmont; after the AMUSE sources and
// QuattroPlay by superctr): its C140 only.
#pragma once

#include <cstdint>
#include <vector>

class State;

class C140 {
public:
    // `samples`: the sample ROMs, st1-voi0..3, 512 KB each, one after the other.
    explicit C140(const std::vector<uint8_t> &samples);
    void reset();
    uint8_t read(int offset);
    void write(int offset, uint8_t data);
    // One sample (at the chip's own rate): left and right, about -1..1.
    void tick(float &left, float &right);
    bool irq() const { return irq_; }   // INT1: the sound CPU's FIRQ
    int keyed() const;                  // voices playing (for the pause capture)
    void state(State &s);               // save or load its registers and voices

private:
    struct Voice {
        int32_t ptoffset = 0, pos = 0, key = 0;
        int32_t lastdt = 0, prevdt = 0, dltdt = 0;
        int32_t bank = 0, mode = 0, start = 0, end = 0, loop = 0;
    };
    uint16_t sampleWord(int32_t address) const;

    const std::vector<uint8_t> &rom_;
    uint8_t reg_[0x200] = {};
    int16_t pcmtbl_[256] = {};
    Voice voice_[24];
    bool irq_ = false;
    int32_t timer_ = -1;                // ticks until INT1, -1: stopped
};
