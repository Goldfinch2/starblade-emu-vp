// m6809.cpp: the 6809 interpreter (m6809.h).
#include "m6809.h"

#include "state.h"

void M6809::reset()
{
    dp_ = 0;
    cc_ |= CC_I | CC_F;
    cwai_ = sync_ = false;
    nmi_ = nmiArmed_ = false;             // NMI waits until S has been set
    pc_ = rd16(0xfffe);
    debt_ = 0;
}

void M6809::state(State &s)
{
    s.pod(a_); s.pod(b_); s.pod(dp_); s.pod(cc_);
    s.pod(x_); s.pod(y_); s.pod(u_); s.pod(s_); s.pod(pc_);
    s.pod(irq_); s.pod(firq_); s.pod(nmi_); s.pod(nmiArmed_); s.pod(cwai_); s.pod(sync_);
    s.pod(debt_);
}

int M6809::run(int cycles)
{
    int done = debt_;
    while (done < cycles) {
        cycles_ = 0;
        if (!interrupts()) {
            if (cwai_ || sync_) { done = cycles; break; }   // waiting for an interrupt
            step();
        }
        done += cycles_;
    }
    debt_ = done - cycles;
    return cycles;
}

// ---- interrupts --------------------------------------------------------------------

void M6809::pushAll()
{
    push16(s_, pc_);
    push16(s_, u_);
    push16(s_, y_);
    push16(s_, x_);
    push8(s_, dp_);
    push8(s_, b_);
    push8(s_, a_);
    push8(s_, cc_);
}

bool M6809::interrupts()
{
    if (nmi_ && nmiArmed_) {
        nmi_ = false;
        if (!cwai_) { cc_ |= CC_E; pushAll(); }
        cwai_ = sync_ = false;
        cc_ |= CC_I | CC_F;
        pc_ = rd16(0xfffc);
        cycles_ += 19;
        return true;
    }
    if (firq_ && !(cc_ & CC_F)) {
        if (!cwai_) { cc_ &= ~CC_E; push16(s_, pc_); push8(s_, cc_); }   // PC and CC only
        cwai_ = sync_ = false;
        cc_ |= CC_I | CC_F;
        pc_ = rd16(0xfff6);
        cycles_ += 10;
        return true;
    }
    if (irq_ && !(cc_ & CC_I)) {
        if (!cwai_) { cc_ |= CC_E; pushAll(); }
        cwai_ = sync_ = false;
        cc_ |= CC_I;
        pc_ = rd16(0xfff8);
        cycles_ += 19;
        return true;
    }
    if (sync_ && (irq_ || firq_ || nmi_)) sync_ = false;    // SYNC ends on a line even when masked
    return false;
}

// ---- operands ----------------------------------------------------------------------

uint16_t &M6809::indexReg(int code)
{
    switch (code & 3) {
    case 0: return x_;
    case 1: return y_;
    case 2: return u_;
    default: return s_;
    }
}

uint16_t M6809::indexed()
{
    const uint8_t pb = fetch();
    uint16_t &r = indexReg(pb >> 5);
    if (!(pb & 0x80)) {                 // 5-bit offset
        int off = pb & 0x1f;
        if (off & 0x10) off -= 0x20;
        cycles_ += 1;
        return (uint16_t)(r + off);
    }
    uint16_t ea;
    switch (pb & 0x0f) {
    case 0x0: ea = r; r++; cycles_ += 2; break;                                  // ,R+
    case 0x1: ea = r; r += 2; cycles_ += 3; break;                               // ,R++
    case 0x2: r--; ea = r; cycles_ += 2; break;                                  // ,-R
    case 0x3: r -= 2; ea = r; cycles_ += 3; break;                               // ,--R
    case 0x4: ea = r; break;                                                     // ,R
    case 0x5: ea = (uint16_t)(r + (int8_t)b_); cycles_ += 1; break;              // B,R
    case 0x6: ea = (uint16_t)(r + (int8_t)a_); cycles_ += 1; break;              // A,R
    case 0x8: { int8_t o = (int8_t)fetch(); ea = (uint16_t)(r + o); cycles_ += 1; break; }
    case 0x9: { uint16_t o = fetch16(); ea = (uint16_t)(r + o); cycles_ += 4; break; }
    case 0xb: ea = (uint16_t)(r + (uint16_t)(a_ << 8 | b_)); cycles_ += 4; break;   // D,R
    case 0xc: { int8_t o = (int8_t)fetch(); ea = (uint16_t)(pc_ + o); cycles_ += 1; break; }
    case 0xd: { uint16_t o = fetch16(); ea = (uint16_t)(pc_ + o); cycles_ += 5; break; }
    case 0xf: ea = fetch16(); cycles_ += 2; break;                               // [n16]
    default: ea = r; break;                                                      // undefined
    }
    if (pb & 0x10) { ea = rd16(ea); cycles_ += 3; }                              // indirect
    return ea;
}

// Modes from the opcode's bits 4-5: 0 immediate, 1 direct, 2 indexed, 3 extended.
uint8_t M6809::operand8(uint8_t op)
{
    switch ((op >> 4) & 3) {
    case 0: cycles_ += 2; return fetch();
    case 1: cycles_ += 4; return rd(direct());
    case 2: cycles_ += 4; return rd(indexed());
    default: cycles_ += 5; return rd(extended());
    }
}

uint16_t M6809::operand16Address(uint8_t op)
{
    switch ((op >> 4) & 3) {
    case 1: cycles_ += 4; return direct();
    case 2: cycles_ += 4; return indexed();
    default: cycles_ += 5; return extended();
    }
}

uint16_t M6809::operand16(uint8_t op)
{
    if (((op >> 4) & 3) == 0) { cycles_ += 3; return fetch16(); }
    uint16_t ea = operand16Address(op);
    cycles_ += 1;
    return rd16(ea);
}

// ---- arithmetic --------------------------------------------------------------------

uint8_t M6809::add8(uint8_t a, uint8_t b, int carry)
{
    const unsigned r = (unsigned)a + b + (carry ? 1 : 0);
    cc_ &= ~(CC_H | CC_V | CC_C);
    if ((a ^ b ^ r) & 0x10) cc_ |= CC_H;
    if ((a ^ r) & (b ^ r) & 0x80) cc_ |= CC_V;
    if (r & 0x100) cc_ |= CC_C;
    setNZ8((uint8_t)r);
    return (uint8_t)r;
}

uint8_t M6809::sub8(uint8_t a, uint8_t b, int carry)
{
    const unsigned r = (unsigned)a - b - (carry ? 1 : 0);
    cc_ &= ~(CC_V | CC_C);
    if ((a ^ b) & (a ^ r) & 0x80) cc_ |= CC_V;
    if (r & 0x100) cc_ |= CC_C;
    setNZ8((uint8_t)r);
    return (uint8_t)r;
}

uint16_t M6809::add16(uint16_t a, uint16_t b)
{
    const uint32_t r = (uint32_t)a + b;
    cc_ &= ~(CC_V | CC_C);
    if ((a ^ r) & (b ^ r) & 0x8000) cc_ |= CC_V;
    if (r & 0x10000) cc_ |= CC_C;
    setNZ16((uint16_t)r);
    return (uint16_t)r;
}

uint16_t M6809::sub16(uint16_t a, uint16_t b)
{
    const uint32_t r = (uint32_t)a - b;
    cc_ &= ~(CC_V | CC_C);
    if ((a ^ b) & (a ^ r) & 0x8000) cc_ |= CC_V;
    if (r & 0x10000) cc_ |= CC_C;
    setNZ16((uint16_t)r);
    return (uint16_t)r;
}

uint8_t M6809::rmw(int op, uint8_t m)
{
    switch (op) {
    case 0x0: return sub8(0, m, 0);                                              // NEG
    case 0x3: m = (uint8_t)~m; setNZ0V8(m); cc_ |= CC_C; return m;               // COM
    case 0x4: cc_ = (uint8_t)((cc_ & ~CC_C) | (m & 1)); m >>= 1; setNZ8(m); return m;   // LSR
    case 0x6: {                                                                  // ROR
        const uint8_t c = cc_ & CC_C;
        cc_ = (uint8_t)((cc_ & ~CC_C) | (m & 1));
        m = (uint8_t)(m >> 1 | c << 7);
        setNZ8(m);
        return m;
    }
    case 0x7: cc_ = (uint8_t)((cc_ & ~CC_C) | (m & 1)); m = (uint8_t)((m & 0x80) | m >> 1); setNZ8(m); return m;   // ASR
    case 0x8:                                                                    // ASL
    case 0x9: {                                                                  // ROL
        const uint8_t r = (uint8_t)(m << 1 | (op == 0x9 ? (cc_ & CC_C) : 0));
        cc_ &= ~(CC_V | CC_C);
        if (m & 0x80) cc_ |= CC_C;
        if ((m ^ (m << 1)) & 0x80) cc_ |= CC_V;
        setNZ8(r);
        return r;
    }
    case 0xa: { const uint8_t r = (uint8_t)(m - 1); cc_ &= ~CC_V; if (m == 0x80) cc_ |= CC_V; setNZ8(r); return r; }   // DEC
    case 0xc: { const uint8_t r = (uint8_t)(m + 1); cc_ &= ~CC_V; if (m == 0x7f) cc_ |= CC_V; setNZ8(r); return r; }   // INC
    case 0xd: setNZ0V8(m); return m;                                             // TST
    case 0xf: cc_ = (uint8_t)((cc_ & ~(CC_N | CC_V | CC_C)) | CC_Z); return 0;  // CLR
    default: return m;
    }
}

bool M6809::branch(int cond)
{
    const bool c = cc_ & CC_C, z = cc_ & CC_Z, n = cc_ & CC_N, v = cc_ & CC_V;
    switch (cond & 15) {
    case 0x0: return true;              // BRA
    case 0x1: return false;             // BRN
    case 0x2: return !(c || z);         // BHI
    case 0x3: return c || z;            // BLS
    case 0x4: return !c;                // BCC
    case 0x5: return c;                 // BCS
    case 0x6: return !z;                // BNE
    case 0x7: return z;                 // BEQ
    case 0x8: return !v;                // BVC
    case 0x9: return v;                 // BVS
    case 0xa: return !n;                // BPL
    case 0xb: return n;                 // BMI
    case 0xc: return n == v;            // BGE
    case 0xd: return n != v;            // BLT
    case 0xe: return !z && n == v;      // BGT
    default: return z || n != v;        // BLE
    }
}

// TFR / EXG register codes.
uint16_t M6809::getReg(int code)
{
    switch (code & 15) {
    case 0x0: return (uint16_t)(a_ << 8 | b_);
    case 0x1: return x_;
    case 0x2: return y_;
    case 0x3: return u_;
    case 0x4: return s_;
    case 0x5: return pc_;
    case 0x8: return (uint16_t)(0xff00 | a_);
    case 0x9: return (uint16_t)(0xff00 | b_);
    case 0xa: return (uint16_t)(0xff00 | cc_);
    case 0xb: return (uint16_t)(0xff00 | dp_);
    default: return 0xffff;
    }
}

void M6809::setReg(int code, uint16_t v)
{
    switch (code & 15) {
    case 0x0: a_ = (uint8_t)(v >> 8); b_ = (uint8_t)v; break;
    case 0x1: x_ = v; break;
    case 0x2: y_ = v; break;
    case 0x3: u_ = v; break;
    case 0x4: s_ = v; nmiArmed_ = true; break;
    case 0x5: pc_ = v; break;
    case 0x8: a_ = (uint8_t)v; break;
    case 0x9: b_ = (uint8_t)v; break;
    case 0xa: cc_ = (uint8_t)v; break;
    case 0xb: dp_ = (uint8_t)v; break;
    default: break;
    }
}

void M6809::pushRegs(uint16_t &sp, uint16_t &other, uint8_t mask)
{
    if (mask & 0x80) { push16(sp, pc_); cycles_ += 2; }
    if (mask & 0x40) { push16(sp, other); cycles_ += 2; }
    if (mask & 0x20) { push16(sp, y_); cycles_ += 2; }
    if (mask & 0x10) { push16(sp, x_); cycles_ += 2; }
    if (mask & 0x08) { push8(sp, dp_); cycles_ += 1; }
    if (mask & 0x04) { push8(sp, b_); cycles_ += 1; }
    if (mask & 0x02) { push8(sp, a_); cycles_ += 1; }
    if (mask & 0x01) { push8(sp, cc_); cycles_ += 1; }
}

void M6809::pullRegs(uint16_t &sp, uint16_t &other, uint8_t mask)
{
    if (mask & 0x01) { cc_ = pull8(sp); cycles_ += 1; }
    if (mask & 0x02) { a_ = pull8(sp); cycles_ += 1; }
    if (mask & 0x04) { b_ = pull8(sp); cycles_ += 1; }
    if (mask & 0x08) { dp_ = pull8(sp); cycles_ += 1; }
    if (mask & 0x10) { x_ = pull16(sp); cycles_ += 2; }
    if (mask & 0x20) { y_ = pull16(sp); cycles_ += 2; }
    if (mask & 0x40) { other = pull16(sp); cycles_ += 2; if (&other == &s_) nmiArmed_ = true; }
    if (mask & 0x80) { pc_ = pull16(sp); cycles_ += 2; }
}

// ---- instructions ------------------------------------------------------------------

void M6809::step()
{
    const uint8_t op = fetch();
    const int lo = op & 15;
    switch (op >> 4) {
    case 0x0: case 0x6: case 0x7: {     // memory: NEG COM LSR ROR ASR ASL ROL DEC INC TST JMP CLR
        uint16_t ea;
        if (op < 0x10)      { ea = direct();   cycles_ += 6; }
        else if (op < 0x70) { ea = indexed();  cycles_ += 6; }
        else                { ea = extended(); cycles_ += 7; }
        if (lo == 0xe) { pc_ = ea; cycles_ -= 3; return; }              // JMP
        if (lo == 0x1 || lo == 0x2 || lo == 0x5 || lo == 0xb) return;   // undefined
        const uint8_t r = rmw(lo, rd(ea));
        if (lo != 0xd) wr(ea, r);                                       // TST only reads
        return;
    }
    case 0x4: case 0x5: {               // the same on A, B
        cycles_ += 2;
        if (lo == 0x1 || lo == 0x2 || lo == 0x5 || lo == 0xb || lo == 0xe) return;
        uint8_t &r = op < 0x50 ? a_ : b_;
        r = rmw(lo, r);
        return;
    }
    case 0x1:
        switch (op) {
        case 0x10: page2(fetch()); return;
        case 0x11: page3(fetch()); return;
        case 0x12: cycles_ += 2; return;                                // NOP
        case 0x13: sync_ = true; cycles_ += 2; return;                  // SYNC
        case 0x16: { uint16_t o = fetch16(); pc_ = (uint16_t)(pc_ + o); cycles_ += 5; return; }   // LBRA
        case 0x17: { uint16_t o = fetch16(); push16(s_, pc_); pc_ = (uint16_t)(pc_ + o); cycles_ += 9; return; }   // LBSR
        case 0x19: {                                                    // DAA
            const uint8_t lsn = a_ & 0x0f, msn = a_ & 0xf0;
            uint8_t cf = 0;
            if ((cc_ & CC_H) || lsn > 9) cf |= 0x06;
            if ((cc_ & CC_C) || msn > 0x90 || (msn > 0x80 && lsn > 9)) cf |= 0x60;
            const unsigned r = (unsigned)a_ + cf;
            a_ = (uint8_t)r;
            if (r & 0x100) cc_ |= CC_C;
            setNZ0V8(a_);
            cycles_ += 2;
            return;
        }
        case 0x1a: cc_ |= fetch(); cycles_ += 3; return;                // ORCC
        case 0x1c: cc_ &= fetch(); cycles_ += 3; return;                // ANDCC
        case 0x1d: a_ = (b_ & 0x80) ? 0xff : 0; setNZ0V16((uint16_t)(a_ << 8 | b_)); cycles_ += 2; return;   // SEX
        case 0x1e: {                                                    // EXG
            const uint8_t pb = fetch();
            const uint16_t v1 = getReg(pb >> 4), v2 = getReg(pb & 15);
            setReg(pb >> 4, v2);
            setReg(pb & 15, v1);
            cycles_ += 8;
            return;
        }
        case 0x1f: { const uint8_t pb = fetch(); setReg(pb & 15, getReg(pb >> 4)); cycles_ += 6; return; }   // TFR
        default: cycles_ += 2; return;
        }
    case 0x2: {                         // short branches
        const int8_t o = (int8_t)fetch();
        if (branch(lo)) pc_ = (uint16_t)(pc_ + o);
        cycles_ += 3;
        return;
    }
    case 0x3:
        switch (op) {
        case 0x30: x_ = indexed(); cc_ = (uint8_t)((cc_ & ~CC_Z) | (x_ ? 0 : CC_Z)); cycles_ += 4; return;   // LEAX
        case 0x31: y_ = indexed(); cc_ = (uint8_t)((cc_ & ~CC_Z) | (y_ ? 0 : CC_Z)); cycles_ += 4; return;   // LEAY
        case 0x32: s_ = indexed(); nmiArmed_ = true; cycles_ += 4; return;     // LEAS
        case 0x33: u_ = indexed(); cycles_ += 4; return;                       // LEAU
        case 0x34: pushRegs(s_, u_, fetch()); cycles_ += 5; return;            // PSHS
        case 0x35: pullRegs(s_, u_, fetch()); cycles_ += 5; return;            // PULS
        case 0x36: pushRegs(u_, s_, fetch()); cycles_ += 5; return;            // PSHU
        case 0x37: pullRegs(u_, s_, fetch()); cycles_ += 5; return;            // PULU
        case 0x39: pc_ = pull16(s_); cycles_ += 5; return;                     // RTS
        case 0x3a: x_ = (uint16_t)(x_ + b_); cycles_ += 3; return;             // ABX
        case 0x3b:                                                             // RTI
            cc_ = pull8(s_);
            if (cc_ & CC_E) {
                a_ = pull8(s_);
                b_ = pull8(s_);
                dp_ = pull8(s_);
                x_ = pull16(s_);
                y_ = pull16(s_);
                u_ = pull16(s_);
                cycles_ += 15;
            } else {
                cycles_ += 6;
            }
            pc_ = pull16(s_);
            return;
        case 0x3c: cc_ &= fetch(); cc_ |= CC_E; pushAll(); cwai_ = true; cycles_ += 20; return;   // CWAI
        case 0x3d: {                                                           // MUL
            const uint16_t r = (uint16_t)(a_ * b_);
            a_ = (uint8_t)(r >> 8);
            b_ = (uint8_t)r;
            cc_ &= ~(CC_Z | CC_C);
            if (!r) cc_ |= CC_Z;
            if (r & 0x80) cc_ |= CC_C;
            cycles_ += 11;
            return;
        }
        case 0x3f: cc_ |= CC_E; pushAll(); cc_ |= CC_I | CC_F; pc_ = rd16(0xfffa); cycles_ += 19; return;   // SWI
        default: cycles_ += 2; return;
        }
    default: {                          // 80-FF: A (80-BF) and B (C0-FF), immediate / direct / indexed / extended
        const bool isB = op >= 0xc0;
        const int mode = (op >> 4) & 3;
        uint8_t &r = isB ? b_ : a_;
        switch (lo) {
        case 0x0: r = sub8(r, operand8(op), 0); return;                        // SUB
        case 0x1: sub8(r, operand8(op), 0); return;                            // CMP
        case 0x2: r = sub8(r, operand8(op), cc_ & CC_C); return;               // SBC
        case 0x3: {                                                            // SUBD / ADDD
            const uint16_t m = operand16(op), d = (uint16_t)(a_ << 8 | b_);
            const uint16_t v = isB ? add16(d, m) : sub16(d, m);
            a_ = (uint8_t)(v >> 8);
            b_ = (uint8_t)v;
            cycles_ += 1;
            return;
        }
        case 0x4: r &= operand8(op); setNZ0V8(r); return;                      // AND
        case 0x5: setNZ0V8(r & operand8(op)); return;                          // BIT
        case 0x6: r = operand8(op); setNZ0V8(r); return;                       // LD
        case 0x7:                                                              // ST
            if (mode == 0) { cycles_ += 2; return; }
            wr(operand16Address(op), r);
            setNZ0V8(r);
            return;
        case 0x8: r ^= operand8(op); setNZ0V8(r); return;                      // EOR
        case 0x9: r = add8(r, operand8(op), cc_ & CC_C); return;               // ADC
        case 0xa: r |= operand8(op); setNZ0V8(r); return;                      // OR
        case 0xb: r = add8(r, operand8(op), 0); return;                        // ADD
        case 0xc:
            if (!isB) { sub16(x_, operand16(op)); cycles_ += 1; return; }      // CMPX
            {                                                                  // LDD
                const uint16_t m = operand16(op);
                a_ = (uint8_t)(m >> 8);
                b_ = (uint8_t)m;
                setNZ0V16(m);
            }
            return;
        case 0xd:
            if (!isB) {
                if (mode == 0) {                                               // BSR
                    const int8_t o = (int8_t)fetch();
                    push16(s_, pc_);
                    pc_ = (uint16_t)(pc_ + o);
                    cycles_ += 7;
                    return;
                }
                const uint16_t ea = operand16Address(op);                      // JSR
                push16(s_, pc_);
                pc_ = ea;
                cycles_ += 3;
                return;
            }
            if (mode == 0) { cycles_ += 2; return; }
            {                                                                  // STD
                const uint16_t d = (uint16_t)(a_ << 8 | b_);
                wr16(operand16Address(op), d);
                setNZ0V16(d);
                cycles_ += 1;
            }
            return;
        case 0xe: {                                                            // LDX / LDU
            const uint16_t m = operand16(op);
            if (isB) u_ = m; else x_ = m;
            setNZ0V16(m);
            return;
        }
        default:                                                               // STX / STU
            if (mode == 0) { cycles_ += 2; return; }
            {
                const uint16_t v = isB ? u_ : x_;
                wr16(operand16Address(op), v);
                setNZ0V16(v);
                cycles_ += 1;
            }
            return;
        }
    }
    }
}

void M6809::page2(uint8_t op)
{
    if (op >= 0x21 && op <= 0x2f) {     // long branches
        const uint16_t o = fetch16();
        cycles_ += 5;
        if (branch(op & 15)) { pc_ = (uint16_t)(pc_ + o); cycles_ += 1; }
        return;
    }
    if (op == 0x3f) { cc_ |= CC_E; pushAll(); pc_ = rd16(0xfff4); cycles_ += 20; return; }   // SWI2
    const int lo = op & 15, hi = op & 0xf0, mode = (op >> 4) & 3;
    if (hi < 0x80) { cycles_ += 2; return; }
    cycles_ += 1;
    switch (lo) {
    case 0x3: if (hi < 0xc0) { sub16((uint16_t)(a_ << 8 | b_), operand16(op)); cycles_ += 1; } return;   // CMPD
    case 0xc: if (hi < 0xc0) { sub16(y_, operand16(op)); cycles_ += 1; } return;                        // CMPY
    case 0xe: {                                                                                          // LDY / LDS
        const uint16_t m = operand16(op);
        if (hi < 0xc0) y_ = m; else { s_ = m; nmiArmed_ = true; }
        setNZ0V16(m);
        return;
    }
    case 0xf:                                                                                            // STY / STS
        if (mode == 0) return;
        {
            const uint16_t v = hi < 0xc0 ? y_ : s_;
            wr16(operand16Address(op), v);
            setNZ0V16(v);
            cycles_ += 1;
        }
        return;
    default: return;
    }
}

void M6809::page3(uint8_t op)
{
    if (op == 0x3f) { cc_ |= CC_E; pushAll(); pc_ = rd16(0xfff2); cycles_ += 20; return; }   // SWI3
    const int lo = op & 15, hi = op & 0xf0;
    if (hi < 0x80 || hi >= 0xc0) { cycles_ += 2; return; }
    cycles_ += 1;
    if (lo == 0x3) { sub16(u_, operand16(op)); cycles_ += 1; }        // CMPU
    else if (lo == 0xc) { sub16(s_, operand16(op)); cycles_ += 1; }   // CMPS
}
