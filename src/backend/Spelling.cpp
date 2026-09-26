#include "../Name.h"
#include "Spelling.h"

#include "Ins.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace shalimar {
namespace {

// **What MASM calls each instruction this compiler emits**, and the whole
// point of the table is the ones that are *not* the same word: the AT&T base
// the emitter passes is not always Microsoft's name for it.

// A mnemonic that is not here is refused by name rather than written out and left for the
// assembler, because the failure then names the compiler that wrote it and the instruction it
// meant. An optimizer added later will emit mnemonics the emitter never did, and this is what will catch them.
struct MasmName {
    const char *att;
    const char *masm;
};

const MasmName kMasmNames[] = {
    // The moves, integer and floating - `movq` between an xmm and a general
    // register is `movq` here too, and `movsd`/`movapd` keep their names.
    { "mov", "mov" },       { "movq", "movq" },
    { "movsd", "movsd" },   { "movss", "movss" },
    { "movapd", "movapd" }, { "movaps", "movaps" },
    // The sign and zero extensions, which are the ones that differ: AT&T's
    // movs/movz against Microsoft's movsx/movzx, and movslq against movsxd.
    { "movslq", "movsxd" }, { "movsbl", "movsx" }, { "movswl", "movsx" },
    { "movzbl", "movzx" },  { "movzwl", "movzx" },
    { "cltq", "cdqe" },     { "cqto", "cqo" },     { "cltd", "cdq" },
    // Arithmetic, logic and the stack.
    { "add", "add" },   { "sub", "sub" },   { "imul", "imul" },
    { "idiv", "idiv" }, { "div", "div" },   { "neg", "neg" },
    { "and", "and" },   { "or", "or" },     { "xor", "xor" },
    { "not", "not" },   { "shl", "shl" },   { "shr", "shr" },
    { "sar", "sar" },   { "inc", "inc" },   { "dec", "dec" },
    { "cmp", "cmp" },   { "test", "test" }, { "lea", "lea" },
    { "push", "push" }, { "pop", "pop" },   { "leave", "leave" },
    // Floating arithmetic and the comparisons that set the flags from it.
    { "addsd", "addsd" }, { "subsd", "subsd" },
    { "mulsd", "mulsd" }, { "divsd", "divsd" },
    { "sqrtsd", "sqrtsd" }, { "xorpd", "xorpd" }, { "xorps", "xorps" },
    { "ucomisd", "ucomisd" }, { "comisd", "comisd" }, { "pxor", "pxor" },
    { "cvtsi2sd", "cvtsi2sd" }, { "cvtsi2sdq", "cvtsi2sd" },
    { "cvttsd2si", "cvttsd2si" }, { "cvttsd2siq", "cvttsd2si" },
    { "cvtsd2ss", "cvtsd2ss" }, { "cvtss2sd", "cvtss2sd" },
    // The transfers of control that are written as instructions here.
    { "call", "call" }, { "ret", "ret" }, { "jmp", "jmp" },
    { "je", "je" },   { "jne", "jne" }, { "jl", "jl" },   { "jge", "jge" },
    { "jle", "jle" }, { "jg", "jg" },   { "jb", "jb" },   { "jbe", "jbe" },
    { "ja", "ja" },   { "jae", "jae" }, { "jp", "jp" },   { "jnp", "jnp" },
    { "jns", "jns" }, { "js", "js" },
    // The conditional sets, whose names are the same on both sides.
    { "sete", "sete" },   { "setne", "setne" }, { "setl", "setl" },
    { "setle", "setle" }, { "setg", "setg" },   { "setge", "setge" },
    { "seta", "seta" },   { "setae", "setae" }, { "setb", "setb" },
    { "setbe", "setbe" }, { "setp", "setp" },   { "setnp", "setnp" },
};

[[noreturn]] void giveUp(const char *mnemonic) {
    std::fprintf(stderr,
                 "%s: masm: an instruction this spelling does not know\n"
                 "  for: %s\n", program::kName, mnemonic);
    std::exit(1);
}

// The MASM name for an AT&T mnemonic, or a refusal naming the instruction.
const char *masmNameFor(const char *mnemonic) {
    for (const MasmName &e : kMasmNames)
        if (std::strcmp(mnemonic, e.att) == 0) return e.masm;
    giveUp(mnemonic);
}

struct RegNames {
    const char *wide;
    const char *dword;
    const char *byte;
};

const RegNames table[] = {
    {"rax", "eax",  "al"},    {"rcx", "ecx",  "cl"},
    {"rdx", "edx",  "dl"},    {"rbx", "ebx",  "bl"},
    {"rsi", "esi",  "sil"},   {"rdi", "edi",  "dil"},
    {"rbp", "ebp",  "bpl"},   {"rsp", "esp",  "spl"},
    {"r8",  "r8d",  "r8b"},   {"r9",  "r9d",  "r9b"},
    {"r10", "r10d", "r10b"},  {"r11", "r11d", "r11b"},
    {"r12", "r12d", "r12b"},  {"r13", "r13d", "r13b"},
    {"r14", "r14d", "r14b"},  {"r15", "r15d", "r15b"},
    {"xmm0", "xmm0", "xmm0"}, {"xmm1", "xmm1", "xmm1"},
    {"xmm2", "xmm2", "xmm2"}, {"xmm3", "xmm3", "xmm3"},
    {"xmm4", "xmm4", "xmm4"}, {"xmm5", "xmm5", "xmm5"},
    {"xmm6", "xmm6", "xmm6"}, {"xmm7", "xmm7", "xmm7"},
    {"xmm8", "xmm8", "xmm8"}
};

std::string hex(uint64_t value) {
    static const char *digits = "0123456789ABCDEF";
    std::string out;
    do {
        out.insert(out.begin(), digits[value & 0xFu]);
        value >>= 4;
    } while (value != 0);
    return out;
}

}

const char *Spelling::name(Reg r, int width) {
    const RegNames &n = table[static_cast<int>(r)];
    if (width == 8) return n.wide;
    if (width == 1) return n.byte;
    return n.dword;
}

char GnuSpelling::suffix(int width) {
    if (width == 8) return 'q';
    if (width == 1) return 'b';
    return 'l';
}

std::string GnuSpelling::reg(Reg r, int width) const {
    return std::string("%") + name(r, width);
}

std::string GnuSpelling::imm(int64_t value) const {
    return "$" + std::to_string(value);
}

std::string GnuSpelling::wideImm(uint64_t value) const {
    return "$0x" + hex(value);
}

std::string GnuSpelling::binary(const char *mnemonic, int width,
                                const std::string &src, const std::string &dst) const {
    std::string out = mnemonic;
    if (width > 0) out += suffix(width);
    return out + "\t" + src + ", " + dst;
}

std::string GnuSpelling::unary(const char *mnemonic, int width,
                               const std::string &operand) const {
    std::string out = mnemonic;
    if (width > 0) out += suffix(width);
    return out + "\t" + operand;
}

std::string GnuSpelling::call(const std::string &target) const {
    return "call\t" + target;
}

std::string GnuSpelling::ret() const { return "ret"; }

std::string GnuSpelling::frameSlot(int offset, int) const {
    return std::to_string(offset) + "(%rsp)";
}

std::string GnuSpelling::widen32To64(const std::string &src, const std::string &dst) const {
    return "movslq\t" + src + ", " + dst;
}

std::string GnuSpelling::loadAddress(const std::string &from, const std::string &dst) const {
    return "leaq\t" + from + ", " + dst;
}

std::string GnuSpelling::indirect(Reg base, int) const {
    return "(" + reg(base, 8) + ")";
}

std::string GnuSpelling::offsetFrom(Reg base, int offset, int) const {
    return std::to_string(offset) + "(" + reg(base, 8) + ")";
}

std::string GnuSpelling::byteArrayHead() const { return ":\n\t.byte\t"; }
std::string GnuSpelling::byteDirective() const { return "\t.byte\t"; }

std::string GnuSpelling::dataReference(const std::string &label) const {
    return label + "(%rip)";
}

std::string MasmSpelling::reg(Reg r, int width) const {
    return name(r, width);
}

std::string MasmSpelling::imm(int64_t value) const {
    return std::to_string(value);
}

std::string MasmSpelling::wideImm(uint64_t value) const {
    return "0" + hex(value) + "h";
}

std::string MasmSpelling::binary(const char *mnemonic, int,
                                 const std::string &src, const std::string &dst) const {
    return std::string(masmNameFor(mnemonic)) + "\t" + dst + ", " + src;
}

std::string MasmSpelling::unary(const char *mnemonic, int,
                                const std::string &operand) const {
    return std::string(masmNameFor(mnemonic)) + "\t" + operand;
}

std::string MasmSpelling::call(const std::string &target) const {
    return "call\t" + target;
}

std::string MasmSpelling::ret() const { return "ret"; }

std::string MasmSpelling::widen32To64(const std::string &src, const std::string &dst) const {
    return "movsxd\t" + dst + ", " + src;
}

std::string MasmSpelling::loadAddress(const std::string &from, const std::string &dst) const {
    return "lea\t" + dst + ", " + from;
}

std::string MasmSpelling::indirect(Reg base, int width) const {
    const char *size = width == 8 ? "QWORD" : (width == 1 ? "BYTE" : "DWORD");
    return std::string(size) + " PTR [" + reg(base, 8) + "]";
}

std::string MasmSpelling::offsetFrom(Reg base, int offset, int width) const {
    const char *size = width == 8 ? "QWORD" : (width == 1 ? "BYTE" : "DWORD");
    return std::string(size) + " PTR [" + reg(base, 8) + "+" + std::to_string(offset) + "]";
}

std::string MasmSpelling::byteArrayHead() const { return "\tDB\t"; }
std::string MasmSpelling::byteDirective() const { return "\tDB\t"; }

std::string MasmSpelling::dataReference(const std::string &label) const {
    return label;
}

std::string MasmSpelling::frameSlot(int offset, int width) const {
    const char *size = width == 8 ? "QWORD" : (width == 1 ? "BYTE" : "DWORD");
    return std::string(size) + " PTR [rsp+" + std::to_string(offset) + "]";
}
// **Rendering a structured instruction**, and it is deliberately not virtual:
// every spelling already answers what an operand looks like, so the shape of
// an instruction is the same question asked of different answers.
std::string Spelling::render(const Operand &o) const {
    switch (o.kind) {
    case Operand::Register:  return reg(o.reg, o.width);
    case Operand::Immediate: return imm(o.value);
    case Operand::Wide:      return wideImm(o.bits);
    case Operand::Frame:     return frameSlot(static_cast<int>(o.value), o.width);
    case Operand::Indirect:  return indirect(o.reg, o.width);
    case Operand::Offset:    return offsetFrom(o.reg, static_cast<int>(o.value), o.width);
    case Operand::Data:      return dataReference(o.text);
    case Operand::None:      break;
    }
    return std::string();
}

std::string Spelling::render(const Ins &i) const {
    if (i.operands == 0) return i.mnemonic;
    if (i.operands == 1) return unary(i.mnemonic, i.width, render(i.a));
    return binary(i.mnemonic, i.width, render(i.a), render(i.b));
}

}
