// m6809.h: a Motorola 6809 interpreter, for the sound board's CPU (soundboard.cpp).
// The whole instruction set (pages 1-3), the IRQ / FIRQ / NMI inputs, CWAI and
// SYNC. Cycle counts are the data sheet's, close enough for a CPU paced by
// its sound chip's timer.
#pragma once

#include <cstdint>

class State;

class M6809 {
public:
    struct Bus {
        virtual uint8_t read(uint16_t address) = 0;
        virtual void write(uint16_t address, uint8_t data) = 0;
        virtual ~Bus() = default;
    };

    explicit M6809(Bus &bus) : bus_(bus) {}
    void reset();
    void setIrq(bool on) { irq_ = on; }
    void setFirq(bool on) { firq_ = on; }
    void nmi() { nmi_ = true; }
    // Run for `cycles` (the overshoot of the last instruction is taken from the
    // next run). Returns the cycles used.
    int run(int cycles);

    uint16_t pc() const { return pc_; }
    void state(State &s);               // save or load its registers and lines

private:
    enum : uint8_t { CC_C = 0x01, CC_V = 0x02, CC_Z = 0x04, CC_N = 0x08, CC_I = 0x10, CC_H = 0x20, CC_F = 0x40, CC_E = 0x80 };

    uint8_t rd(uint16_t a) { return bus_.read(a); }
    void wr(uint16_t a, uint8_t d) { bus_.write(a, d); }
    uint16_t rd16(uint16_t a) { return (uint16_t)(rd(a) << 8 | rd((uint16_t)(a + 1))); }
    void wr16(uint16_t a, uint16_t d) { wr(a, (uint8_t)(d >> 8)); wr((uint16_t)(a + 1), (uint8_t)d); }
    uint8_t fetch() { return rd(pc_++); }
    uint16_t fetch16() { uint16_t v = rd16(pc_); pc_ += 2; return v; }

    void push8(uint16_t &sp, uint8_t v) { wr(--sp, v); }
    void push16(uint16_t &sp, uint16_t v) { wr(--sp, (uint8_t)v); wr(--sp, (uint8_t)(v >> 8)); }
    uint8_t pull8(uint16_t &sp) { return rd(sp++); }
    uint16_t pull16(uint16_t &sp) { uint16_t h = rd(sp++); return (uint16_t)(h << 8 | rd(sp++)); }
    void pushAll();                     // the entire state on S (E set by the caller)
    void pushRegs(uint16_t &sp, uint16_t &other, uint8_t mask);
    void pullRegs(uint16_t &sp, uint16_t &other, uint8_t mask);
    bool interrupts();                  // take a pending interrupt; true if one was taken

    uint16_t direct() { return (uint16_t)(dp_ << 8 | fetch()); }
    uint16_t extended() { return fetch16(); }
    uint16_t indexed();                 // the effective address (cycles added)
    uint16_t &indexReg(int code);

    void setNZ8(uint8_t r) { cc_ = (uint8_t)((cc_ & ~(CC_N | CC_Z)) | (r & 0x80 ? CC_N : 0) | (r ? 0 : CC_Z)); }
    void setNZ16(uint16_t r) { cc_ = (uint8_t)((cc_ & ~(CC_N | CC_Z)) | (r & 0x8000 ? CC_N : 0) | (r ? 0 : CC_Z)); }
    void setNZ0V8(uint8_t r) { setNZ8(r); cc_ &= ~CC_V; }
    void setNZ0V16(uint16_t r) { setNZ16(r); cc_ &= ~CC_V; }
    uint8_t add8(uint8_t a, uint8_t b, int carry);
    uint8_t sub8(uint8_t a, uint8_t b, int carry);
    uint16_t add16(uint16_t a, uint16_t b);
    uint16_t sub16(uint16_t a, uint16_t b);
    uint8_t rmw(int op, uint8_t m);     // NEG COM LSR ROR ASR ASL ROL DEC INC TST CLR (op: low nibble)
    bool branch(int cond);
    uint16_t getReg(int code);
    void setReg(int code, uint16_t v);

    void step();
    void page2(uint8_t op);
    void page3(uint8_t op);
    uint16_t operand16Address(uint8_t op);     // modes 9x / Ax / Bx of a 16-bit op
    uint8_t operand8(uint8_t op);              // modes 8x .. Bx of an 8-bit op
    uint16_t operand16(uint8_t op);

    Bus &bus_;
    uint8_t a_ = 0, b_ = 0, dp_ = 0, cc_ = CC_I | CC_F;
    uint16_t x_ = 0, y_ = 0, u_ = 0, s_ = 0, pc_ = 0;
    bool irq_ = false, firq_ = false, nmi_ = false, nmiArmed_ = false;
    bool cwai_ = false, sync_ = false;
    int cycles_ = 0;                    // spent by the instruction being run
    int debt_ = 0;                      // overshoot carried into the next run
};
