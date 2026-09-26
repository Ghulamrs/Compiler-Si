#pragma once

#include "Spelling.h"

#include <cstdint>
#include <string>

namespace shalimar {

// **An instruction as a record rather than as text.** The emitter used to hand the spelling
// finished strings, which is a shape nothing can optimize: a pass that wants to know what a
// register holds cannot read it out of "mov\trax, QWORD PTR [rsp+8]".

// So an operand says what it *is* and the spelling renders it at the end,
// which is where cc1 and cxx1 put the same seam - and is what an optimizer
// for this compiler would interpose on.
struct Operand {
    enum Kind {
        None,
        Register,     // a register at `width`
        Immediate,    // a signed constant
        Wide,         // a 64-bit pattern, written in hex
        Frame,        // `value` bytes up from the stack pointer, at `width`
        Indirect,     // through `reg`, at `width`
        Offset,       // `value` bytes from `reg`, at `width`
        Data,         // a label, by name
    };

    Kind kind = None;
    Reg reg = Reg::Ax;
    int width = 0;
    int64_t value = 0;
    uint64_t bits = 0;
    std::string text;
};

inline Operand regOp(Reg r, int width) {
    Operand o; o.kind = Operand::Register; o.reg = r; o.width = width; return o;
}
inline Operand immOp(int64_t v) {
    Operand o; o.kind = Operand::Immediate; o.value = v; return o;
}
inline Operand wideOp(uint64_t bits) {
    Operand o; o.kind = Operand::Wide; o.bits = bits; return o;
}
inline Operand frameOp(int64_t bytes, int width) {
    Operand o; o.kind = Operand::Frame; o.value = bytes; o.width = width; return o;
}
inline Operand indirectOp(Reg base, int width) {
    Operand o; o.kind = Operand::Indirect; o.reg = base; o.width = width; return o;
}
inline Operand offsetOp(Reg base, int64_t bytes, int width) {
    Operand o; o.kind = Operand::Offset; o.reg = base; o.value = bytes; o.width = width; return o;
}
inline Operand dataOp(const std::string &label) {
    Operand o; o.kind = Operand::Data; o.text = label; return o;
}

// The operands are in AT&T order - `a` the source, `b` the destination - and
// each spelling puts them the way its own syntax wants. `width` is what the
// GNU spelling turns into a suffix and MASM reads off the operand instead.
struct Ins {
    const char *mnemonic = nullptr;
    int width = 0;
    int operands = 0;
    Operand a, b;
};

inline Ins ins0(const char *m) {
    Ins i; i.mnemonic = m; i.operands = 0; return i;
}
inline Ins ins1(const char *m, int width, const Operand &a) {
    Ins i; i.mnemonic = m; i.width = width; i.operands = 1; i.a = a; return i;
}
inline Ins ins2(const char *m, int width, const Operand &a, const Operand &b) {
    Ins i; i.mnemonic = m; i.width = width; i.operands = 2; i.a = a; i.b = b; return i;
}

} // namespace shalimar
