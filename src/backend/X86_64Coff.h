#pragma once

// **The Windows target in the GNU spelling, for clang's COFF assembler** - the road cpp11 took on
// 2026-09-10 and D4 of the review of 2026-10-12. The same instruction stream as the MASM emitter,
// the same Microsoft ABI, rendered by GnuSpelling; what differs is what goes round the
// instructions: COFF sections, `.seh_*` unwind data the assembler turns into .pdata and .xdata, the
// `.cv_*` line entries, and CodeView after the code. Chosen by `-g` on x86_64-windows, since
// MASM has no words for a line table; without `-g` the MASM emitter is what it always was.

#include "X86_64.h"

namespace shalimar {

class X86_64CoffEmitter : public X86_64Emitter {
public:
    X86_64CoffEmitter() : X86_64Emitter(spelling_impl_, microsoftAbi()) {}

    std::string symbol(const std::string &name) const override { return name; }

    void beginModule(const std::string &sourceName) override;
    void endModule() override;
    void beginFunction(const std::string &name) override;
    void endFunction(int slots) override;
    void label(int id) override;

    void sourceLine(int unit, int line) override;
    void describeFunction(const DebugFunction &function) override;
    void describeGlobals(const std::vector<DebugGlobal> &globals) override { debugGlobals_ = globals; }

private:
    bool writesCfi() const override { return false; }
    std::string afterPush() const override;
    std::string afterSetFrame() const override;
    std::string afterAlloc(int bytes) const override;
    std::string endPrologue() const override;
    std::string localLabel(const std::string &name) const override;

    std::string labelName(int id) const override { return ".Lshm" + std::to_string(id); }
    std::string bytesLabel(int id) const override { return ".Lshmb" + std::to_string(id); }
    void openConstSection() override;
    void closeConstSection() override;
    void emitGlobalBlock(int slots) override;
    std::string globalsLabel() const override { return "shm_globals"; }

    GnuSpelling spelling_impl_;
    std::string sourceName_;
    int functions_ = 0;        // .cv_func_id numbers, in the order the functions are described
    int lastUnit_ = -1;        // the last .cv_loc, so a line is entered once per run of it
    int lastLine_ = -1;
};

}
