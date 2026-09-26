#include "Optimize.h"

#include <cstring>

namespace shalimar {
namespace {

// **What each mnemonic this emitter writes does to its operands.** Anything not named here reads and writes everything, which stops a run rather than risking it.
enum class Shape { Move, ReadWrite, ReadRead, Push, Pop, Unknown };

Shape shapeOf(const char *m) {
    if (m == nullptr) return Shape::Unknown;
    if (std::strcmp(m, "mov") == 0 || std::strcmp(m, "movsd") == 0 ||
        std::strcmp(m, "movq") == 0 || std::strcmp(m, "movapd") == 0)
        return Shape::Move;
    if (std::strcmp(m, "add") == 0 || std::strcmp(m, "sub") == 0)
        return Shape::ReadWrite;
    if (std::strcmp(m, "test") == 0 || std::strcmp(m, "cmp") == 0)
        return Shape::ReadRead;
    if (std::strcmp(m, "push") == 0) return Shape::Push;
    if (std::strcmp(m, "pop") == 0) return Shape::Pop;
    return Shape::Unknown;
}

// An operand reads a register when it names one to be read, or addresses
// memory through one: a base register is read whichever side it is on.
bool reads(const Operand &o, Reg r, bool asValue) {
    switch (o.kind) {
    case Operand::Register: return asValue && o.reg == r;
    case Operand::Indirect:
    case Operand::Offset:   return o.reg == r;
    default:                return false;
    }
}

// Written whole, and nothing of the old value kept: only a register named
// as a destination is that. Memory is not a register, so it writes none.
bool writesWhole(const Operand &o, Reg r) {
    return o.kind == Operand::Register && o.reg == r;
}

// Whether `i` reads `r` at all - a value, a base, or an implicit use.
bool instructionReads(const Ins &i, Reg r) {
    switch (shapeOf(i.mnemonic)) {
    case Shape::Move:      return reads(i.a, r, true) || reads(i.b, r, false);
    case Shape::ReadWrite: return reads(i.a, r, true) || reads(i.b, r, true);
    case Shape::ReadRead:  return reads(i.a, r, true) || reads(i.b, r, true);
    case Shape::Push:      return reads(i.a, r, true);
    case Shape::Pop:       return false;
    case Shape::Unknown:   break;
    }
    return true;
}

bool instructionWrites(const Ins &i, Reg r) {
    switch (shapeOf(i.mnemonic)) {
    case Shape::Move: return writesWhole(i.b, r);
    case Shape::Pop:  return writesWhole(i.a, r);
    // add and sub read their destination as well, so they never make the old
    // value dead; test and cmp write no register at all.
    case Shape::ReadWrite:
    case Shape::ReadRead:
    case Shape::Push: return false;
    case Shape::Unknown: break;
    }
    return true;
}

// **Whether `r` is dead from `at` onwards**, proved rather than assumed: it must be written again
// inside this run before anything reads it. A run ends at a branch and the target may read the
// register, so running off the end proves nothing and is not treated as death.
bool deadFrom(const std::vector<Ins> &run, std::size_t at, Reg r) {
    for (std::size_t k = at; k < run.size(); k++) {
        if (instructionReads(run[k], r)) return false;
        if (instructionWrites(run[k], r)) return true;
    }
    return false;
}

bool sameRegister(const Operand &a, const Operand &b) {
    return a.kind == Operand::Register && b.kind == Operand::Register &&
           a.reg == b.reg && a.width == b.width;
}

} // namespace

void optimizeRun(std::vector<Ins> &run) {
    std::vector<Ins> kept;
    kept.reserve(run.size());

    for (std::size_t i = 0; i < run.size(); i++) {
        const Ins &x = run[i];

        // A move of a register onto itself is nothing at all.
        if (shapeOf(x.mnemonic) == Shape::Move && x.operands == 2 &&
            sameRegister(x.a, x.b)) {
            continue;
        }

        // **A value made in one register and at once moved to another.** The first move may name
        // the second's destination when what it wrote is dead after the copy - and the copy must
        // be a plain register move of the same width, or the two are not the same value.
        if (i + 1 < run.size() && shapeOf(x.mnemonic) == Shape::Move &&
            x.operands == 2 && x.b.kind == Operand::Register) {
            const Ins &c = run[i + 1];
            if (shapeOf(c.mnemonic) == Shape::Move && c.operands == 2 &&
                std::strcmp(x.mnemonic, c.mnemonic) == 0 &&
                c.a.kind == Operand::Register && c.b.kind == Operand::Register &&
                c.a.reg == x.b.reg && c.a.width == x.b.width &&
                c.b.width == x.b.width && c.b.reg != x.b.reg &&
                // The source must not be what the copy writes, or the value
                // would be read after being overwritten.
                !reads(x.a, c.b.reg, true) && !reads(x.a, c.b.reg, false) &&
                deadFrom(run, i + 2, x.b.reg)) {
                Ins folded = x;
                folded.b = c.b;
                kept.push_back(folded);
                i++;                      // the copy goes with it
                continue;
            }
        }

        kept.push_back(x);
    }

    run.swap(kept);
}

} // namespace shalimar
