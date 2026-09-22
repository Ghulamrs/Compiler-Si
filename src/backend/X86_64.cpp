#include "X86_64.h"

#include <algorithm>

namespace shalimar {
namespace {

const Reg systemVIntArgs[] = {Reg::Di, Reg::Si, Reg::Dx, Reg::Cx, Reg::R8, Reg::R9};
const Reg microsoftIntArgs[] = {Reg::Cx, Reg::Dx, Reg::R8, Reg::R9};

}

const Abi &systemVAbi() {
    static const Abi abi = {systemVIntArgs, 6, 8, false, 0};
    return abi;
}

const Abi &microsoftAbi() {
    static const Abi abi = {microsoftIntArgs, 4, 4, true, 32};
    return abi;
}

Reg X86_64Emitter::registerFor(Slot kind) {
    return kind == Slot::Real ? realAccumulator : accumulator;
}

const char *X86_64Emitter::moveFor(Slot kind) {
    return kind == Slot::Real ? "movsd" : "mov";
}

int X86_64Emitter::widthFor(Slot kind) {
    return kind == Slot::Int ? 4 : 8;
}

Reg X86_64Emitter::sseArg(int index) {
    static const Reg sse[] = {Reg::Xmm0, Reg::Xmm1, Reg::Xmm2, Reg::Xmm3,
                              Reg::Xmm4, Reg::Xmm5, Reg::Xmm6, Reg::Xmm7};
    return sse[index];
}

Reg X86_64Emitter::argRegister(Slot kind, int index) const {
    return kind == Slot::Real ? sseArg(index) : abi_.intArgs[index];
}

int X86_64Emitter::frameBytesFor(int slots) const {
    return (slots * 8 + abi_.shadowBytes + 15) & ~15;
}

Operand X86_64Emitter::slotOperand(int slot, int width) const {
    return frameOp(abi_.shadowBytes + 8 * slot, width);
}

std::string X86_64Emitter::prologue(int slots) {
    const int bytes = frameBytesFor(slots);
    std::string out;
    // The prologue is returned as text rather than emitted, because the
    // frame size is not known until the body has been walked; it is built
    // from the same records all the same.
    out += "\t" + spelling_.render(ins1("push", 8, regOp(Reg::Bp, 8))) + "\n";
    out += "\t" + spelling_.render(ins2("mov", 8, regOp(Reg::Sp, 8),
                                         regOp(Reg::Bp, 8))) + "\n";
    if (bytes > 0) {
        out += "\t" + spelling_.render(ins2("sub", 8, immOp(bytes),
                                             regOp(Reg::Sp, 8))) + "\n";
    }
    return out;
}

void X86_64Emitter::emitEpilogue(int slots) {
    const int bytes = frameBytesFor(slots);
    if (bytes > 0) {
        emit(ins2("add", 8, immOp(bytes),
                                     regOp(Reg::Sp, 8)));
    }
    emit(ins1("pop", 8, regOp(Reg::Bp, 8)));
    emit(ins0("ret"));
}

void X86_64Emitter::loadIntConstant(int32_t value) {
    emit(ins2("mov", 4, immOp(value), regOp(accumulator, 4)));
}

void X86_64Emitter::loadRealConstant(double value) {
    emit(ins2("mov", 8, wideOp(bitsOf(value)),
                                 regOp(accumulator, 8)));

    emit(ins2("movq", 0, regOp(accumulator, 8),
                                 regOp(realAccumulator, 8)));
}

void X86_64Emitter::storeSlot(Slot kind, int slot) {
    const int width = widthFor(kind);
    emit(ins2(moveFor(kind), kind == Slot::Real ? 0 : width,
                                 regOp(registerFor(kind), width),
                                 slotOperand(slot, width)));
}

void X86_64Emitter::loadSlot(Slot kind, int slot) {
    const int width = widthFor(kind);
    emit(ins2(moveFor(kind), kind == Slot::Real ? 0 : width,
                                 slotOperand(slot, width),
                                 regOp(registerFor(kind), width)));
}

void X86_64Emitter::setArg(Slot kind, int index) {
    const Reg target = argRegister(kind, index);
    if (target == registerFor(kind)) return;
    const int width = widthFor(kind);

    const char *mnemonic = kind == Slot::Real ? "movapd" : "mov";
    emit(ins2(mnemonic, kind == Slot::Real ? 0 : width,
                                 regOp(registerFor(kind), width),
                                 regOp(target, width)));
}

void X86_64Emitter::loadSlotIntoArg(Slot kind, int slot, int index) {
    const int width = widthFor(kind);
    emit(ins2(moveFor(kind), kind == Slot::Real ? 0 : width,
                                 slotOperand(slot, width),
                                 regOp(argRegister(kind, index), width)));
}

void X86_64Emitter::setOverflowBlock(int slot, const std::vector<Slot> &) {
    emitLea(slotOperand(slot, 8),
                                      regOp(overflowPointer, 8));
}

void X86_64Emitter::spillOverflowArgument(Slot kind, int index, int slot) {
    const int width = widthFor(kind);
    const Reg via = kind == Slot::Real ? Reg::Xmm8 : Reg::R11;
    emit(ins2(moveFor(kind), kind == Slot::Real ? 0 : width,
                                 offsetOp(overflowPointer, 8 * index, width),
                                 regOp(via, width)));
    emit(ins2(moveFor(kind), kind == Slot::Real ? 0 : width,
                                 regOp(via, width), slotOperand(slot, width)));
}

void X86_64Emitter::spillArgument(Slot kind, int registerIndex, int slot) {
    const int width = widthFor(kind);
    emit(ins2(moveFor(kind), kind == Slot::Real ? 0 : width,
                                 regOp(argRegister(kind, registerIndex), width),
                                 slotOperand(slot, width)));
}

void X86_64Emitter::loadSlotAddress(int slot) {
    emitLea(slotOperand(slot, 8), regOp(accumulator, 8));
}

void X86_64Emitter::storeThroughPointer(Slot kind, int pointerSlot) {
    emit(ins2("mov", 8, slotOperand(pointerSlot, 8),
                                 regOp(Reg::R10, 8)));
    const int width = widthFor(kind);
    emit(ins2(moveFor(kind), kind == Slot::Real ? 0 : width,
                                 regOp(registerFor(kind), width),
                                 indirectOp(Reg::R10, width)));
}

void X86_64Emitter::loadThroughPointer(Slot kind, int pointerSlot) {
    emit(ins2("mov", 8, slotOperand(pointerSlot, 8),
                                 regOp(Reg::R10, 8)));
    const int width = widthFor(kind);
    emit(ins2(moveFor(kind), kind == Slot::Real ? 0 : width,
                                 indirectOp(Reg::R10, width),
                                 regOp(registerFor(kind), width)));
}

void X86_64Emitter::defineGlobals(int slots) {
    if (slots > 0) emitGlobalBlock(slots);
}

void X86_64Emitter::loadGlobal(Slot kind, int index) {
    emitLea(dataOp(globalsLabel()),
                                      regOp(Reg::R11, 8));
    const int width = widthFor(kind);
    emit(ins2(moveFor(kind), kind == Slot::Real ? 0 : width,
                                 offsetOp(Reg::R11, 8 * index, width),
                                 regOp(registerFor(kind), width)));
}

void X86_64Emitter::storeGlobal(Slot kind, int index) {
    emitLea(dataOp(globalsLabel()),
                                      regOp(Reg::R11, 8));
    const int width = widthFor(kind);
    emit(ins2(moveFor(kind), kind == Slot::Real ? 0 : width,
                                 regOp(registerFor(kind), width),
                                 offsetOp(Reg::R11, 8 * index, width)));
}

void X86_64Emitter::defineBytes(int id, const std::string &bytes) {
    std::string line = bytesLabel(id) + spelling_.byteArrayHead();
    bool opened = false;
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (opened) line += ", ";
        line += std::to_string(static_cast<unsigned char>(bytes[i]));
        opened = true;
        if (i % 16 == 15 && i + 1 < bytes.size()) {
            data_ += line + "\n";
            line = spelling_.byteDirective();
            opened = false;
        }
    }
    data_ += line + "\n";
}

void X86_64Emitter::loadBytesAddress(int id) {
    emitLea(dataOp(bytesLabel(id)),
                                      regOp(accumulator, 8));
}

void X86_64Emitter::call(const std::string &name) {
    const std::string linkName = symbol(name);
    noteExternal(linkName);
    emitCall(linkName);
}

void X86_64Emitter::widenAccumulator() {
    emitWiden(regOp(accumulator, 4),
                                      regOp(accumulator, 8));
}

void X86_64Emitter::jump(int id) {
    instruction("jmp\t" + labelName(id));
}

void X86_64Emitter::jumpIfZero(int id) {
    emit(ins2("test", 4, regOp(accumulator, 4),
                                 regOp(accumulator, 4)));
    instruction("je\t" + labelName(id));
}

void X86_64Emitter::noteExternal(const std::string &name) {
    if (std::find(referenced_.begin(), referenced_.end(), name) == referenced_.end()) {
        referenced_.push_back(name);
    }
}

void X86_64Emitter::noteDefined(const std::string &name) {
    if (std::find(defined_.begin(), defined_.end(), name) == defined_.end()) {
        defined_.push_back(name);
    }
}

std::vector<std::string> X86_64Emitter::externals() const {
    std::vector<std::string> out;
    for (const std::string &name : referenced_) {
        if (std::find(defined_.begin(), defined_.end(), name) == defined_.end()) {
            out.push_back(name);
        }
    }
    return out;
}

}
