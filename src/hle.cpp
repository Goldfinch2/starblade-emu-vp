// hle.cpp: the I/O MCU and the DSP board, as the 68000s see them. The values
// come from captures of the original running in MAME (tools/sb_mcu.lua,
// sb_cont.lua, sb_boot2.lua). The sound board runs its own program:
// soundboard.cpp.
#include "hle.h"
#include "state.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>


// ---- the C68 I/O MCU --------------------------------------------------------------

void Mcu::reset(uint8_t *dp)
{
    // What the C68 writes when it comes out of reset.
    memset(dp, 0, 0x60);
    dp[0x06] = 0x01;
    dp[0x07] = 0x09;
    for (int i = 0x08; i <= 0x0f; i++) dp[i] = 0xff;
    dp[0x09] = 0x80;                // stick X, centered
    dp[0x0a] = 0x80;                // stick Y
    dp[0x11] = 0x01;
    dp[0x12] = 0x01;
    dp[0x13] = 0x20;
    dp[0x14] = 0x02;
    dp[0x1d] = 0x07;
    dp[0x1f] = 0x01;
    dp[0x3a] = 0xa6;                // boot handshake: "MCU here"
    dp[0x44] = 0x01;
    dp[0x56] = 0x01;
    dp[0x57] = 0x01;
    prevButtons_ = 0;
    coinPending_ = 0;
}

static uint8_t stick(float v)
{
    int x = 0x80 + (int)(v * 0x40 + (v < 0 ? -0.5f : 0.5f));
    return (uint8_t)(x < 0x3f ? 0x3f : (x > 0xbf ? 0xbf : x));
}

void Mcu::vblank(uint8_t *dp)
{
    uint8_t buttons = 0, coinsHeld = 0, coinsIn = 0;
    float x = 0, y = 0;
    if (in_) {
        if (vp_input_held(in_, "start1")) buttons |= 0x80;
        if (vp_input_held(in_, "gun")) buttons |= 0x20;     // gun
        if (vp_input_held(in_, "missile")) buttons |= 0x10;     // missile
        if (vp_input_held(in_, "coin1")) coinsHeld |= 0x10;
        if (vp_input_held(in_, "coin2")) coinsHeld |= 0x08;
        if (vp_input_pressed(in_, "coin1")) coinsIn |= 0x10;
        if (vp_input_pressed(in_, "coin2")) coinsIn |= 0x08;
        x = vp_input_axis(in_, "p1_stickx");
        y = -vp_input_axis(in_, "p1_sticky");       // up (the axis's negative side) aims up
        // The game's Y: the low value aims DOWN (a flight stick: pull back to
        // aim up). Here up aims up on everything: the stick's axis (up is its
        // negative side), the aim keys and the mouse are all turned.
        // Without a stick: the aim keys (aim_up ...: main.cpp) push a stick that springs back to
        // the center when released (as MAME does for analog controls), and
        // the mouse or trackball moves it. While the stick is out of its
        // center the keys are left out: a stick bound both ways (its axis,
        // and its directions to the aim keys, AdvanceMAME's default) would
        // have them fight.
        auto toward = [this](float v, float target) {
            const float rate = 0.06f * aimSpeed_;
            return v < target ? std::min(v + rate, target) : std::max(v - rate, target);
        };
        const float CENTERED = 0.1f;
        float kx = (vp_input_held(in_, "aim_right") ? 1.0f : 0.0f) - (vp_input_held(in_, "aim_left") ? 1.0f : 0.0f);
        float ky = (vp_input_held(in_, "aim_up") ? 1.0f : 0.0f) - (vp_input_held(in_, "aim_down") ? 1.0f : 0.0f);
        if (std::fabs(x) > CENTERED) kx = 0;
        if (std::fabs(y) > CENTERED) ky = 0;
        keyX_ = toward(keyX_, kx);
        keyY_ = toward(keyY_, ky);
        const float mouse = aimSpeed_ / 250.0f;
        mouseX_ = std::max(-1.0f, std::min(1.0f, mouseX_ + vp_input_delta(in_, "p1_trackballx") * mouse));
        mouseY_ = std::max(-1.0f, std::min(1.0f, mouseY_ - vp_input_delta(in_, "p1_trackbally") * mouse));
        // The aim speed: the stick reaches the edge with less travel (FAST) or more.
        x *= aimSpeed_;
        y *= aimSpeed_;
        x = std::max(-1.0f, std::min(1.0f, x + keyX_ + mouseX_));
        y = std::max(-1.0f, std::min(1.0f, y + keyY_ + mouseY_));
    }

    dp[0x00] = buttons;
    dp[0x03] = buttons & ~prevButtons_;
    prevButtons_ = buttons;

    // A coin: the switch (02), then the next frame "inserted" (05) and the count.
    dp[0x02] = coinsHeld;
    dp[0x05] = coinPending_;
    if (coinPending_ & 0x10) { if (coins_[0] < 99) coins_[0]++; meter_[0]++; }
    if (coinPending_ & 0x08) { if (coins_[1] < 99) coins_[1]++; meter_[1]++; }
    coinPending_ = coinsIn;

    // The game takes coins for a start or a continue by writing how many to 16/17.
    int take = dp[0x16] | dp[0x17] << 8;
    if (take) {
        int from1 = take < coins_[0] ? take : coins_[0];
        coins_[0] -= from1;
        take -= from1;
        coins_[1] -= take < coins_[1] ? take : coins_[1];
        dp[0x16] = dp[0x17] = 0;
    }
    dp[0x18] = 0;
    dp[0x19] = (uint8_t)coins_[0];
    dp[0x1a] = 0;
    dp[0x1b] = (uint8_t)coins_[1];
    dp[0x46] = meter_[0];
    dp[0x47] = meter_[1];

    dp[0x09] = stick(x);
    dp[0x0a] = stick(y);

    dp[0x1f]++;
    dp[0x21]++;
    dp[0x23]++;
    dp[0x06] = 0x01;
    dp[0x44] = 0x01;
    if (dp[0x35] == 0xff) dp[0x35] = 0x00;
    if (dp[0x3b] == 0xff) { dp[0x3c] = 0xa6; dp[0x3b] = 0x00; }   // boot handshake
}

void Mcu::state(State &s)
{
    s.pod(prevButtons_);
    s.pod(coinPending_);
    s.bytes(coins_, sizeof coins_);
    s.bytes(meter_, sizeof meter_);
    s.pod(keyX_); s.pod(keyY_);
    s.pod(mouseX_); s.pod(mouseY_);
}

void Mcu::write(uint8_t *, int, uint8_t)
{
    // Everything the game asks of the MCU is answered at the next vblank.
}

// ---- the DSP board ---------------------------------------------------------------------

void Dsp::state(State &s)
{
    s.pod(frame_.valid);
    s.bytes(frame_.camera, sizeof frame_.camera);
    s.vec(frame_.list, true);
    s.pod(running_);
}

void Dsp::reset()
{
    frame_ = Frame();
    running_ = false;
}

void Dsp::write(uint16_t *ram, int word)
{
    if (word != 0x103) return;
    // The value written is the page the 68000 fills NEXT (it often starts
    // at once); the other one is finished: keep a copy for the renderer, as
    // the DSPs would start drawing it now.
    const uint16_t *page = ram + 0x4000 + ((ram[0x103] & 1) ^ 1) * 0x2000;
    memcpy(frame_.camera, page, sizeof frame_.camera);
    // The whole list area: FFFFh ends the list only where a record would
    // start (it is also -1 inside records); the renderer walks the records.
    frame_.list.assign(page + 0x100, page + 0x2000);
    frame_.valid = true;
}

void Dsp::vblank(uint16_t *ram)
{
    // The boot handshake (docs/dsp.md): what the master DSP answers.
    if (ram[0x89] != 0) {               // 0x200112: are you there?
        ram[0x89] = 0;
        ram[0x85] = 0xee52;             // 0x20010A/C: the point ROM checksums
        ram[0x86] = 0xd5df;
        ram[0x12] = 0x4545;             // 0x200024: "EE52", shown on the PTR: line
        ram[0x13] = 0x3532;
        ram[0x88] = 1;                  // 0x200110: ready
    }
    if (ram[0x87] != 0) ram[0x87] = 0;              // 0x20010E: program upload done
    if (ram[0x101] == 0xffff) {                     // 0x200202: running
        ram[0x101] = 0;
        running_ = true;
    }
}
