// soundboard.cpp: the sound board (soundboard.h).
#include "soundboard.h"

#include "machine.h"
#include "roms.h"
#include "state.h"

namespace {

const double CPU_CLOCK  = 49152000.0 / 24;      // the 6809: 2.048 MHz
const double YM_CLOCK   = 3579545.0;
const double YM_RATE    = YM_CLOCK / 64;        // the YM2151's samples: 55.9 kHz, the stream's rate
const double C140_RATE  = 49152000.0 / 384 / 6; // the C140's: 21.3 kHz
const float  C140_GAIN  = 0.50f;                // MAME's mix
const float  YM_GAIN    = 0.30f;
const double QUEUE_MAX  = 0.12;                 // seconds of sound waiting at most (more: dropped)
const double QUEUE_START = 0.05;                // silence written first: a cushion against a late frame

} // namespace

Sound::Sound() = default;

Sound::~Sound() { close(); }

void Sound::open(vp_audio *audio, const Roms &roms, uint8_t *dpram)
{
    roms_ = &roms;
    dpram_ = dpram;
    c140_ = std::make_unique<C140>(roms.voices);
    ym_.reset();
    audio_ = audio;
    if (audio_) {
        voice_ = vp_stream_play(audio_, (int)YM_RATE, 1.0f);
        std::vector<float> silence((size_t)(YM_RATE * QUEUE_START) * 2, 0.0f);
        if (voice_) vp_stream_write(audio_, voice_, silence.data(), (int)(silence.size() / 2));
    }
}

void Sound::close()
{
    if (audio_ && voice_) vp_voice_stop(audio_, voice_, 0);
    voice_ = 0;
    audio_ = nullptr;
}

void Sound::reset(bool held)
{
    if (held) {
        held_ = true;
        bank_ = 0;
        return;
    }
    if (!held_ || !roms_) return;
    held_ = false;
    cpu_.reset();                       // from its reset vector, in the fixed ROM at C000h
}

// ---- the 6809's map ---------------------------------------------------------------

uint8_t Sound::read(uint16_t a)
{
    if (a < 0x4000) return roms_->sound[(size_t)bank_ * 0x4000 + a];
    if (a < 0x5000) return a < 0x4002 ? ym_.read(a & 1) : 0;
    if (a < 0x6000) return c140_->read(a & 0x1ff);
    if (a < 0x7000) return 0;
    if (a < 0x8000) return dpram_[a & 0x7ff];
    if (a < 0xa000) return ram_[a & 0x1fff];
    if (a < 0xc000) return 0;
    return roms_->sound[a & 0x3fff];
}

void Sound::write(uint16_t a, uint8_t d)
{
    if (a >= 0x4000 && a < 0x4002) { ym_.write(a & 1, d); return; }
    if (a >= 0x5000 && a < 0x6000) {
        c140_->write(a & 0x1ff, d);
        cpu_.setFirq(c140_->irq());     // taking the interrupt clears it at once
        return;
    }
    if (a >= 0x7000 && a < 0x8000) { dpram_[a & 0x7ff] = d; return; }
    if (a >= 0x8000 && a < 0xa000) { ram_[a & 0x1fff] = d; return; }
    if (a == 0xc001) bank_ = (d >> 4) % (int)(roms_->sound.size() / 0x4000);   // the ROM bank at 0000h
    // the rest: the amplifier, the watchdog, nothing
}

// ---- the YM2151's timers ------------------------------------------------------------

void Sound::ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks)
{
    if (tnum < 2) ymTimer_[tnum] = duration_in_clocks;
}

// ---- running -----------------------------------------------------------------------

void Sound::runFrame()
{
    if (!c140_) return;
    samplesDue_ += YM_RATE / Machine::kFrameRate;
    const int n = (int)samplesDue_;
    samplesDue_ -= n;
    out_.resize((size_t)n * 2);
    for (int i = 0; i < n; i++) {
        // The 6809, for the time of one YM2151 sample.
        if (!held_) {
            cpuDue_ += CPU_CLOCK / YM_RATE;
            const int cycles = (int)cpuDue_;
            cpuDue_ -= cycles;
            cpu_.setFirq(c140_->irq());
            cpu_.run(cycles);
        }
        // The C140 at its own rate, its samples joined by a straight line.
        c140Due_ += C140_RATE / YM_RATE;
        while (c140Due_ >= 1.0) {
            c140Due_ -= 1.0;
            c140Prev_[0] = c140Now_[0];
            c140Prev_[1] = c140Now_[1];
            c140_->tick(c140Now_[0], c140Now_[1]);
            cpu_.setFirq(c140_->irq());
        }
        const float t = (float)c140Due_;
        const float pl = c140Prev_[0] + (c140Now_[0] - c140Prev_[0]) * t;
        const float pr = c140Prev_[1] + (c140Now_[1] - c140Prev_[1]) * t;

        // The YM2151: its timers (64 clocks a sample), then a sample.
        for (uint32_t k = 0; k < 2; k++) {
            if (ymTimer_[k] < 0) continue;
            ymTimer_[k] -= 64;
            if (ymTimer_[k] <= 0) {
                ymTimer_[k] = -1;
                m_engine->engine_timer_expired(k);      // may set it again
            }
        }
        ymfm::ym2151::output_data fm;
        ym_.generate(&fm);

        out_[(size_t)i * 2]     = pl * C140_GAIN + fm.data[0] / 32768.0f * YM_GAIN;
        out_[(size_t)i * 2 + 1] = pr * C140_GAIN + fm.data[1] / 32768.0f * YM_GAIN;
    }
    if (audio_ && voice_ && n > 0 && vp_stream_queued(audio_, voice_) < (int)(YM_RATE * QUEUE_MAX))
        vp_stream_write(audio_, voice_, out_.data(), n);
}

void Sound::state(State &s)
{
    cpu_.state(s);
    s.bytes(ram_, sizeof ram_);
    s.pod(bank_);
    s.pod(held_);
    s.bytes(ymTimer_, sizeof ymTimer_);
    s.pod(cpuDue_); s.pod(c140Due_); s.pod(samplesDue_);
    s.bytes(c140Prev_, sizeof c140Prev_);
    s.bytes(c140Now_, sizeof c140Now_);
    if (c140_) c140_->state(s);
    // The YM2151: ymfm's own save state, as a block.
    std::vector<uint8_t> ym;
    if (s.saving()) {
        ymfm::ymfm_saved_state y(ym, true);
        ym_.save_restore(y);
    }
    s.vec(ym, true);
    if (!s.saving() && s.ok()) {
        ymfm::ymfm_saved_state y(ym, false);
        ym_.save_restore(y);
    }
}

void Sound::list(FILE *out) const
{
    fprintf(out, "sound board: %s, 6809 at %04X, ROM bank %d, C140 voices playing %d, C140 interrupt %s\n",
            held_ ? "held in reset" : "running", cpu_.pc(), bank_, c140_ ? c140_->keyed() : 0,
            c140_ && c140_->irq() ? "pending" : "clear");
    if (!dpram_) return;
    static const struct { int base; const char *name; } AREAS[] = { { 0x100, "FM" }, { 0x200, "PCM" }, { 0x400, "music" } };
    for (const auto &area : AREAS) {
        fprintf(out, "  %-5s", area.name);
        for (int slot = 0; slot < 8; slot++)
            fprintf(out, " %02X:%02X", dpram_[area.base + slot * 2], dpram_[area.base + slot * 2 + 1]);
        fprintf(out, "\n");
    }
}
