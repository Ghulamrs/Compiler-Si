
#pragma once

#include "DebugInfo.h"

#include <cstdint>
#include <string>
#include <vector>

namespace shalimar {

enum class Slot { Int, Real, Wide };

class Emitter {
public:
    virtual ~Emitter() = default;

    const std::string &text() const { return text_; }

    virtual std::string symbol(const std::string &name) const = 0;

    // -O0, -O1 or -O2. A target with no optimizer ignores it.
    virtual void setOptimize(int) {}

    virtual void beginModule(const std::string &sourceName) = 0;
    virtual void endModule() = 0;

    virtual void beginFunction(const std::string &name) = 0;
    virtual void endFunction(int slots) = 0;

    virtual void loadIntConstant(int32_t value) = 0;
    virtual void loadRealConstant(double value) = 0;

    virtual void storeSlot(Slot kind, int slot) = 0;
    virtual void loadSlot(Slot kind, int slot) = 0;

    virtual void setArg(Slot kind, int index) = 0;
    virtual void loadSlotIntoArg(Slot kind, int slot, int index) = 0;

    virtual bool positionalArguments() const = 0;

    virtual int intArgCapacity() const = 0;
    virtual int realArgCapacity() const = 0;

    // The arguments past the registers, in the block of slots from `slot` on, one each in order,
    // with their kinds: a target that hands over the block's address ignores the kinds, one that
    // lays them out as its ABI does needs them.
    virtual void setOverflowBlock(int slot, const std::vector<Slot> &kinds) = 0;
    virtual void spillOverflowArgument(Slot kind, int index, int slot) = 0;

    virtual void spillArgument(Slot kind, int registerIndex, int slot) = 0;

    virtual void call(const std::string &name) = 0;

    virtual void loadSlotAddress(int slot) = 0;
    virtual void storeThroughPointer(Slot kind, int pointerSlot) = 0;
    virtual void loadThroughPointer(Slot kind, int pointerSlot) = 0;

    virtual void defineGlobals(int slots) = 0;
    virtual void loadGlobal(Slot kind, int index) = 0;
    virtual void storeGlobal(Slot kind, int index) = 0;

    virtual void defineBytes(int id, const std::string &bytes) = 0;
    virtual void loadBytesAddress(int id) = 0;

    virtual void widenAccumulator() = 0;

    virtual void label(int id) = 0;
    virtual void jump(int id) = 0;

    virtual void jumpIfZero(int id) = 0;

    // **-g: what a debugger is told, and it changes no instruction.** The source files by unit,
    // and the directory they were named from; until this is called nothing below emits a byte.
    void setDebugInfo(const std::vector<std::string> &files, const std::string &compDir) {
        debugInfo_ = true;
        files_ = files;
        compDir_ = compDir;
    }
    bool debugInfo() const { return debugInfo_; }
    // Inside a function describeFunction opened: the one whose frame the unwind data describes.
    bool describing() const { return describing_; }

    // The position of the statement about to be generated, before its first instruction.
    virtual void sourceLine(int, int) {}
    // After beginFunction: the names, types and slots a debugger sees in this function.
    virtual void describeFunction(const DebugFunction &) {}
    // Once, before the functions: the globals by name, type and slot.
    virtual void describeGlobals(const std::vector<DebugGlobal> &) {}
    // A nested block's first instruction, and the point after its last - the lexical blocks a name
    // hides inside. A label each, in the function being described, and nothing otherwise.
    void beginBlock(int scope) { blockLabel(scope, true); }
    void endBlock(int scope) { blockLabel(scope, false); }

    void setLine(int unit, int line) {
        loadIntConstant(line);
        setArg(Slot::Int, 1);
        loadIntConstant(unit);
        setArg(Slot::Int, 0);
        call("shm_line");
    }

protected:

    // **Virtual, so a target that buffers can write its buffer out first.** Everything that is not a buffered instruction goes through one of these.
    virtual void instruction(const std::string &line) { text_ += "\t" + line + "\n"; }

    virtual void raw(const std::string &line) { text_ += line + "\n"; }
    void blank() { text_ += "\n"; }

    virtual void noteExternal(const std::string &) {}

    // How this target spells a label of the file's own - `L` on Mach-O, `.L` on ELF, `$` for MASM.
    virtual std::string localLabel(const std::string &name) const { return "L" + name; }

    void blockLabel(int scope, bool begin) {
        if (!describing() || scope <= 0 || debugFunctions_.empty()) return;
        DebugFunction &d = debugFunctions_.back();
        if (static_cast<size_t>(scope) >= d.blocks.size()) return;
        const std::string label = localLabel("block." + d.symbol + "." + std::to_string(scope) +
                                             (begin ? ".b" : ".e"));
        (begin ? d.blocks[static_cast<size_t>(scope)].begin : d.blocks[static_cast<size_t>(scope)].end) = label;
        raw(label + ":");
    }

    static uint64_t bitsOf(double value);

    std::string data_;

    std::string text_;

    bool debugInfo_ = false;
    bool describing_ = false;
    std::vector<std::string> files_;
    std::string compDir_;
    std::vector<DebugFunction> debugFunctions_;
    std::vector<DebugGlobal> debugGlobals_;
};

}
