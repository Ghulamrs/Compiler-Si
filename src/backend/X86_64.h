
#pragma once

#include "Emitter.h"
#include "Ins.h"
#include "Spelling.h"

#include <string>
#include <vector>

namespace shalimar {

struct Abi {
    const Reg *intArgs;
    int intArgCount;
    int sseArgCount;

    bool positional;

    int shadowBytes;
};

const Abi &systemVAbi();
const Abi &microsoftAbi();

class X86_64Emitter : public Emitter {
public:
    X86_64Emitter(const Spelling &spelling, const Abi &abi)
        : spelling_(spelling), abi_(abi) {}

    void loadIntConstant(int32_t value) override;
    void loadRealConstant(double value) override;

    void storeSlot(Slot kind, int slot) override;
    void loadSlot(Slot kind, int slot) override;
    void setArg(Slot kind, int index) override;
    void loadSlotIntoArg(Slot kind, int slot, int index) override;

    bool positionalArguments() const override { return abi_.positional; }
    int intArgCapacity() const override { return abi_.intArgCount; }
    int realArgCapacity() const override { return abi_.sseArgCount; }
    void setOverflowBlock(int slot, const std::vector<Slot> &kinds) override;
    void spillOverflowArgument(Slot kind, int index, int slot) override;
    void spillArgument(Slot kind, int registerIndex, int slot) override;
    void call(const std::string &name) override;
    void widenAccumulator() override;

    void loadSlotAddress(int slot) override;
    void storeThroughPointer(Slot kind, int pointerSlot) override;
    void loadThroughPointer(Slot kind, int pointerSlot) override;

    void defineGlobals(int slots) override;
    void loadGlobal(Slot kind, int index) override;
    void storeGlobal(Slot kind, int index) override;

    void defineBytes(int id, const std::string &bytes) override;
    void loadBytesAddress(int id) override;

    void jump(int id) override;
    void jumpIfZero(int id) override;

protected:
    // **The structured funnel.** Every x86 instruction goes through here as a
    // record; the spelling turns it into text at the last moment, which is
    // where an optimizer for this compiler would sit.
    void emit(const Ins &i) { instruction(spelling_.render(i)); }

    // The three the spelling names itself, because each syntax writes them
    // its own way: a `lea` whose source is an address, the widening that is
    // `movsxd` on one side and `movslq` on the other, and a call.
    void emitLea(const Operand &from, const Operand &to) {
        instruction(spelling_.loadAddress(spelling_.render(from), spelling_.render(to)));
    }
    void emitWiden(const Operand &from, const Operand &to) {
        instruction(spelling_.widen32To64(spelling_.render(from), spelling_.render(to)));
    }
    void emitCall(const std::string &target) { instruction(spelling_.call(target)); }

    const Spelling &spelling_;
    const Abi &abi_;

    static const Reg accumulator = Reg::Ax;
    static const Reg realAccumulator = Reg::Xmm0;

    static const Reg overflowPointer = Reg::R10;

    virtual std::string labelName(int id) const = 0;
    virtual std::string bytesLabel(int id) const = 0;

    virtual void openConstSection() = 0;
    virtual void closeConstSection() = 0;

    virtual void emitGlobalBlock(int slots) = 0;
    virtual std::string globalsLabel() const = 0;

    Operand slotOperand(int slot, int width) const;

    std::string prologue(int slots);
    void emitEpilogue(int slots);
    int frameBytesFor(int slots) const;

    size_t prologueMark_ = 0;

    void noteExternal(const std::string &name) override;
    void noteDefined(const std::string &name);

    std::vector<std::string> externals() const;

private:
    std::vector<std::string> referenced_;
    std::vector<std::string> defined_;

    static Reg registerFor(Slot kind);
    static const char *moveFor(Slot kind);
    static int widthFor(Slot kind);
    static Reg sseArg(int index);
    Reg argRegister(Slot kind, int index) const;
};

}
