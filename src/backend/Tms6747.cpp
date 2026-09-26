#include "Tms6747.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace shalimar {

static const int kSaveBytes = 40;   // under A15 whatever is saved, so a slot's address does not wait on the body

void Tms6747Emitter::beginModule(const std::string &sourceName) {
    raw("; " + sourceName);
    raw("\t.text");
}

void Tms6747Emitter::endModule() {
    if (globalSlots_ > 0) {
        blank();
        raw("\t.global\t" + symbol("shm_globals"));
        raw("\t.bss\t" + symbol("shm_globals") + ", " +
            std::to_string(globalSlots_ * 8) + ", 8");
    }
    if (!data_.empty()) {
        blank();
        raw("\t.sect\t\".const\"");
        text_ += data_;
    }
    for (const std::string &name : called_)
        if (!defined_.count(name)) raw("\t.ref\t" + symbol(name));
    // The personality routine the index entries name by number.
    if (!currentFunction_.empty()) {
        raw("\t.global\t__c6xabi_unwind_cpp_pr3");
        raw("\t.symdepend\t\"__c6xabi_unwind_cpp_pr3\", \".c6xabi.exidx:.text\"");
    }
    blank();
}

void Tms6747Emitter::constant(const std::string &reg, int32_t value) {
    instruction("MVKL\t" + std::to_string(value) + ", " + reg);
    instruction("MVKH\t" + std::to_string(value) + ", " + reg);
}

// The slots rise from the bottom of the frame, so a block of them reads as an array; how far
// below A15 that is - the save area and every slot - is known only when the function ends, and
// is a symbol the assembler resolves: slot k is at A15 - base + 8k.
void Tms6747Emitter::slotAddress(int slot, const std::string &reg) {
    const std::string off = std::to_string(8 * slot) + " - " + slotBase_;
    instruction("MVKL\t" + off + ", " + reg);
    instruction("MVKH\t" + off + ", " + reg);
    instruction("ADD\tA15, " + reg + ", " + reg);
}

std::string Tms6747Emitter::pairOf(const std::string &reg) {
    // "A4" -> "A5:A4"
    const int n = std::atoi(reg.c_str() + 1);
    return reg.substr(0, 1) + std::to_string(n + 1) + ":" + reg;
}

std::string Tms6747Emitter::argRegister(int index) {
    const int number = 4 + 2 * (index / 2);
    return std::string(index % 2 == 0 ? "A" : "B") + std::to_string(number);
}

void Tms6747Emitter::loadAt(Slot kind, const std::string &addrReg, const std::string &reg) {
    if (kind == Slot::Int) instruction("LDW\t*" + addrReg + ", " + reg);
    else                   instruction("LDDW\t*" + addrReg + ", " + pairOf(reg));
    instruction("NOP\t4");
}

void Tms6747Emitter::storeAt(Slot kind, const std::string &addrReg, const std::string &reg) {
    if (kind == Slot::Int) instruction("STW\t" + reg + ", *" + addrReg);
    else                   instruction("STDW\t" + pairOf(reg) + ", *" + addrReg);
}

std::string Tms6747Emitter::labelName(int id) { return "Lshm" + std::to_string(id); }
std::string Tms6747Emitter::bytesLabel(int id) { return "Lshmb" + std::to_string(id); }

void Tms6747Emitter::beginFunction(const std::string &name) {
    blank();
    raw("\t.global\t" + symbol(name));
    raw(symbol(name) + ":");
    defined_.insert(name);
    currentFunction_ = symbol(name);
    slotBase_ = "shmbase$" + symbol(name);
    usesSavedArgRegs_ = false;
    usesSavedPairRegs_ = false;
    outgoingBytes_ = 0;
    incomingBytes_ = 0;
    prologueMark_ = text_.size();
}

// The frame, the shape c90 and cpp11 keep: A15 points at the caller's B15 word and holds the caller's A15; below it, in
// the order TI's unwinder pops them, B12, B10, B3, A12, A10 when the body loads the argument registers and B3 alone
// otherwise, in 40 bytes whatever is saved; then the slots, eight bytes each; then the outgoing arguments past the registers, from B15 + 4, the word at B15 the callee's.

static std::vector<std::string> savedRegs(bool argRegs, bool pairRegs) {
    std::vector<std::string> r;
    r.push_back("A15");
    if (pairRegs) r.push_back("B13");
    if (argRegs) r.push_back("B12");
    if (pairRegs) r.push_back("B11");
    if (argRegs) r.push_back("B10");
    r.push_back("B3");
    if (pairRegs) r.push_back("A13");
    if (argRegs) r.push_back("A12");
    if (pairRegs) r.push_back("A11");
    if (argRegs) r.push_back("A10");
    return r;
}
void Tms6747Emitter::endFunction(int slots) {
    const std::vector<std::string> saved = savedRegs(usesSavedArgRegs_, usesSavedPairRegs_);
    const int outgoing = (4 + outgoingBytes_ + 7) / 8 * 8;   // the callee's word, then the arguments
    const int frame = kSaveBytes + 8 * slots + outgoing;
    std::string prologue;
    prologue += slotBase_ + "\t.set\t" + std::to_string(kSaveBytes + 8 * slots) + "\n";   // before its uses
    prologue += "\tSTW\tA15, *B15\n";
    prologue += "\tMV\tB15, A15\n";
    for (size_t k = 1; k < saved.size(); k++)
        prologue += "\tSTW\t" + saved[k] + ", *-A15(" + std::to_string(4 * k) + ")\n";
    prologue += "\tMVKL\t" + std::to_string(frame) + ", A3\n";
    prologue += "\tMVKH\t" + std::to_string(frame) + ", A3\n";
    prologue += "\tSUB\tB15, A3, B15\n";
    text_.insert(prologueMark_, prologue);

    for (size_t k = 1; k < saved.size(); k++)
        instruction("LDW\t*-A15(" + std::to_string(4 * k) + "), " + saved[k]);
    instruction("MV\tA15, B15");
    instruction("LDW\t*A15, A15");
    instruction("NOP\t4");
    instruction("B\tB3");
    instruction("NOP\t5");
    indexEntry(usesSavedArgRegs_, usesSavedPairRegs_);
}

// TI's exception index entry for the function just ended, in the compact
// form (lib/src/tdeh_pr_c6000.cpp): personality 3, SP restored from A15
// (0x7f), the bitmask of the registers saved, B3 the return register.
void Tms6747Emitter::indexEntry(bool argRegs, bool pairRegs) {
    static const struct { const char *reg; int bit; } bits[] = {
        { "A15", 12 }, { "B13", 9 }, { "B12", 8 }, { "B11", 7 }, { "B10", 6 }, { "B3", 5 },
        { "A13", 3 }, { "A12", 2 }, { "A11", 1 }, { "A10", 0 } };
    unsigned mask = 0;
    for (const std::string &r : savedRegs(argRegs, pairRegs))
        for (size_t k = 0; k < sizeof bits / sizeof bits[0]; k++)
            if (r == bits[k].reg) mask |= 1u << bits[k].bit;
    char word[16];
    std::snprintf(word, sizeof word, "0x%08x", 0x83000000u | 0x7fu << 17 | mask << 4 | 7u);
    raw("\t.sect\t\".c6xabi.exidx:.text\"");
    raw("\t.align\t4");
    raw("\t.ulong\t$EXIDX_FUNC(" + currentFunction_ + ")");
    raw("\t.ulong\t" + std::string(word));
    raw("\t.text");
}

void Tms6747Emitter::loadIntConstant(int32_t value) {
    constant("A4", value);
}

void Tms6747Emitter::loadRealConstant(double value) {
    const uint64_t bits = bitsOf(value);
    constant("A4", static_cast<int32_t>(static_cast<uint32_t>(bits & 0xFFFFFFFFu)));
    constant("A5", static_cast<int32_t>(static_cast<uint32_t>(bits >> 32)));
}

void Tms6747Emitter::storeSlot(Slot kind, int slot) {
    slotAddress(slot, "A3");
    storeAt(kind, "A3", "A4");
}

void Tms6747Emitter::loadSlot(Slot kind, int slot) {
    slotAddress(slot, "A3");
    loadAt(kind, "A3", "A4");
}

void Tms6747Emitter::setArg(Slot kind, int index) {
    if (index == 0) return;
    if (index >= 6) usesSavedArgRegs_ = true;
    if (index >= 6 && kind != Slot::Int) usesSavedPairRegs_ = true;   // the pair's partner is callee-saved too
    const std::string reg = argRegister(index);
    instruction("MV\tA4, " + reg);
    if (kind != Slot::Int) instruction("MV\tA5, " + pairOf(reg).substr(0, pairOf(reg).find(':')));
}

void Tms6747Emitter::loadSlotIntoArg(Slot kind, int slot, int index) {
    if (index >= 6) usesSavedArgRegs_ = true;
    if (index >= 6 && kind != Slot::Int) usesSavedPairRegs_ = true;
    slotAddress(slot, "A3");
    loadAt(kind, "A3", argRegister(index));
}

void Tms6747Emitter::spillArgument(Slot kind, int registerIndex, int slot) {
    slotAddress(slot, "A3");
    storeAt(kind, "A3", argRegister(registerIndex));
}

// The arguments past the ten registers go where the ABI puts them: from
// the caller's B15 + 4 upward, a word for an int or a pointer, a real in
// eight bytes at an 8-aligned offset. Both sides count the same way.
static int argumentOffset(int &bytes, Slot kind) {
    int at = 4 + bytes;
    if (kind == Slot::Real) at = (at + 7) / 8 * 8;     // the offset from B15 is what aligns
    bytes = at - 4 + (kind == Slot::Real ? 8 : 4);
    return at;
}
// Through A0-A3 only: the argument registers are already loaded.
void Tms6747Emitter::setOverflowBlock(int slot, const std::vector<Slot> &kinds) {
    int bytes = 0;
    for (size_t i = 0; i < kinds.size(); i++) {
        const int at = argumentOffset(bytes, kinds[i]);
        slotAddress(slot + static_cast<int>(i), "A3");
        loadAt(kinds[i], "A3", "A0");
        constant("A2", at);
        instruction("ADD\tB15, A2, A2");
        if (kinds[i] == Slot::Real) instruction("STDW\tA1:A0, *A2");
        else                        instruction("STW\tA0, *A2");
    }
    if (bytes > outgoingBytes_) outgoingBytes_ = bytes;
}

void Tms6747Emitter::spillOverflowArgument(Slot kind, int index, int slot) {
    (void)index;                                   // they arrive in order
    const int at = argumentOffset(incomingBytes_, kind);
    constant("A6", at);
    instruction("ADD\tA15, A6, A6");
    if (kind == Slot::Real) instruction("LDDW\t*A6, A9:A8");
    else                    instruction("LDW\t*A6, A8");
    instruction("NOP\t4");
    if (kind == Slot::Wide) instruction("ZERO\tA9");
    slotAddress(slot, "A3");
    storeAt(kind, "A3", "A8");
}

void Tms6747Emitter::call(const std::string &name) {
    const std::string back = "Lshmr" + std::to_string(returns_++);
    instruction("MVKL\t" + back + ", B3");
    instruction("MVKH\t" + back + ", B3");
    instruction("B\t" + symbol(name));
    instruction("NOP\t5");
    raw(back + ":");
    called_.insert(name);
}

void Tms6747Emitter::loadSlotAddress(int slot) {
    slotAddress(slot, "A4");
}

void Tms6747Emitter::storeThroughPointer(Slot kind, int pointerSlot) {
    slotAddress(pointerSlot, "A3");
    instruction("LDW\t*A3, A3");
    instruction("NOP\t4");
    storeAt(kind, "A3", "A4");
}

void Tms6747Emitter::loadThroughPointer(Slot kind, int pointerSlot) {
    slotAddress(pointerSlot, "A3");
    instruction("LDW\t*A3, A3");
    instruction("NOP\t4");
    loadAt(kind, "A3", "A4");
}

void Tms6747Emitter::defineGlobals(int slots) {
    globalSlots_ = slots;
}

void Tms6747Emitter::loadGlobal(Slot kind, int index) {
    instruction("MVKL\t" + symbol("shm_globals") + ", A3");
    instruction("MVKH\t" + symbol("shm_globals") + ", A3");
    constant("A6", 8 * index);
    instruction("ADD\tA3, A6, A3");
    loadAt(kind, "A3", "A4");
}

void Tms6747Emitter::storeGlobal(Slot kind, int index) {
    instruction("MVKL\t" + symbol("shm_globals") + ", A3");
    instruction("MVKH\t" + symbol("shm_globals") + ", A3");
    constant("A6", 8 * index);
    instruction("ADD\tA3, A6, A3");
    storeAt(kind, "A3", "A4");
}

void Tms6747Emitter::defineBytes(int id, const std::string &bytes) {
    data_ += bytesLabel(id) + ":\n";
    std::string line;
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (line.empty()) line = "\t.byte\t";
        else line += ", ";
        line += std::to_string(static_cast<unsigned char>(bytes[i]));
        if (i % 16 == 15) { data_ += line + "\n"; line.clear(); }
    }
    if (!line.empty()) data_ += line + "\n";
}

void Tms6747Emitter::loadBytesAddress(int id) {
    instruction("MVKL\t" + bytesLabel(id) + ", A4");
    instruction("MVKH\t" + bytesLabel(id) + ", A4");
}

// An int made wide: the sign copied into the high word of the pair.
void Tms6747Emitter::widenAccumulator() {
    instruction("SHR\tA4, 31, A5");
}

void Tms6747Emitter::label(int id) {
    raw(labelName(id) + ":");
}

void Tms6747Emitter::jump(int id) {
    instruction("B\t" + labelName(id));
    instruction("NOP\t5");
}

void Tms6747Emitter::jumpIfZero(int id) {
    instruction("MV\tA4, A1");
    instruction("[!A1]\tB\t" + labelName(id));
    instruction("NOP\t5");
}

}
