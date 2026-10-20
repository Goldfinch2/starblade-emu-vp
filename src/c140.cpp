// c140.cpp: the Namco C140 (c140.h), from MAME's c140.cpp (BSD-3-Clause,
// copyright R. Belmont).
#include "c140.h"

#include <cstdlib>

#include "state.h"

C140::C140(const std::vector<uint8_t> &samples) : rom_(samples)
{
    // The compressed PCM table (MAME: verified from the Wii Virtual Console Starblade).
    for (int i = 0; i < 256; i++) {
        const int j = (int8_t)i;
        const int s1 = j & 7;
        const int s2 = std::abs(j >> 3) & 31;
        int v = (0x80 << s1) & 0xff00;
        v += s2 << (s1 ? s1 + 3 : 4);
        pcmtbl_[i] = (int16_t)(j < 0 ? -v : v);
    }
    reset();
}

void C140::reset()
{
    for (uint8_t &r : reg_) r = 0;
    for (Voice &v : voice_) v = Voice();
    irq_ = false;
    timer_ = -1;
}

// The sample ROMs on System 21: word addresses, bits 20-21 the ROM, the data
// in each word's high byte (the low byte unused).
uint16_t C140::sampleWord(int32_t address) const
{
    const size_t i = (size_t)((address >> 20) & 3) * 0x80000 + (size_t)(address & 0x7ffff);
    return i < rom_.size() ? (uint16_t)(rom_[i] << 8) : 0;
}

void C140::state(State &s)
{
    s.bytes(reg_, sizeof reg_);
    s.bytes(voice_, sizeof voice_);     // plain numbers
    s.pod(irq_);
    s.pod(timer_);
}

int C140::keyed() const
{
    int n = 0;
    for (const Voice &v : voice_) n += v.key ? 1 : 0;
    return n;
}

uint8_t C140::read(int offset)
{
    offset &= 0x1ff;
    uint8_t data = reg_[offset];
    if ((offset & 0xf) == 0x5 && offset < 0x180)
        data = (uint8_t)((voice_[offset >> 4].key ? 0x40 : 0x00) | (reg_[offset] & 0x3f));   // playing
    else if (offset == 0x1f8)
        data++;                         // timer reload value = written + 1
    return data;
}

void C140::write(int offset, uint8_t data)
{
    offset &= 0x1ff;
    reg_[offset] = data;
    if (offset < 0x180) {
        Voice &v = voice_[offset >> 4];
        if ((offset & 0xf) == 0x5) {
            if ((data & 0x80) || ((data & 0x40) && v.key)) {
                const uint8_t *r = &reg_[offset & 0x1f0];
                v.key = 1;
                v.ptoffset = v.pos = 0;
                v.lastdt = v.prevdt = v.dltdt = 0;
                v.bank = r[4];
                v.mode = data;
                v.start = r[6] << 8 | r[7];
                v.end = r[8] << 8 | r[9];
                v.loop = r[10] << 8 | r[11];
            } else {
                v.key = 0;
            }
        }
        return;
    }
    switch (offset) {
    case 0x1fa:                         // INT1 taken: the next in (1F8h + 1) x 2 ticks
        irq_ = false;
        if (reg_[0x1fe] & 1) timer_ = (reg_[0x1f8] + 1) * 2;
        break;
    case 0x1fe:                         // INT1 timer on / off
        if (data & 1) {
            if (timer_ < 0) irq_ = true;    // the first comes at once
        } else {
            irq_ = false;
            timer_ = -1;
        }
        break;
    default:
        break;
    }
}

void C140::tick(float &left, float &right)
{
    if (timer_ >= 0 && --timer_ <= 0) {
        timer_ = -1;
        irq_ = true;
    }

    // MAME's update at the chip's own rate: the step is the frequency x 2.
    int32_t lsum = 0, rsum = 0;
    for (int i = 0; i < 24; i++) {
        Voice &v = voice_[i];
        if (!v.key) continue;
        const uint8_t *r = &reg_[i * 16];
        const int frequency = r[2] << 8 | r[3];
        if (frequency == 0) continue;
        const int delta = frequency * 2;
        const int lvol = r[1] * 32 / 24;
        const int rvol = r[0] * 32 / 24;
        const int size = v.end - v.start;
        const int base = (v.bank << 16) + v.start;

        v.ptoffset += delta;
        const int cnt = (v.ptoffset >> 16) & 0x7fff;
        v.ptoffset &= 0xffff;
        v.pos += cnt;
        if (v.pos >= size) {
            if (v.mode & 0x10) {
                v.pos = v.loop - v.start;   // looped
            } else {
                v.key = 0;
                continue;
            }
        }
        if (cnt) {
            const uint16_t sample = sampleWord(base + v.pos) & 0xfff0;       // 12 bits
            v.prevdt = v.lastdt;
            v.lastdt = ((v.mode & 0x08) ? pcmtbl_[(sample >> 8) & 0xff] : (int16_t)sample) >> 4;
            v.dltdt = v.lastdt - v.prevdt;
        }
        const int32_t dt = ((v.dltdt * v.ptoffset) >> 16) + v.prevdt;
        lsum += (dt * lvol) >> (5 + 4);
        rsum += (dt * rvol) >> (5 + 4);
    }
    // MAME: clamped at 4096 for full scale.
    auto scale = [](int32_t s) {
        if (s > 4095) s = 4095;
        if (s < -4096) s = -4096;
        return s / 4096.0f;
    };
    left = scale(lsum);
    right = scale(rsum);
}
