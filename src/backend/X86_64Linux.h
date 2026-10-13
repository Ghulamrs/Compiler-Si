
#pragma once

#include "X86_64.h"

namespace shalimar {

class X86_64LinuxEmitter : public X86_64Emitter {
public:
    X86_64LinuxEmitter() : X86_64Emitter(spelling_impl_, systemVAbi()) {}

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
    bool writesCfi() const override { return describing(); }
    std::string localLabel(const std::string &name) const override { return ".L" + name; }

    std::string labelName(int id) const override { return ".Lshm" + std::to_string(id); }
    std::string bytesLabel(int id) const override { return ".Lshmb" + std::to_string(id); }
    void openConstSection() override;
    void closeConstSection() override;
    void emitGlobalBlock(int slots) override;
    std::string globalsLabel() const override { return "shm_globals"; }

    GnuSpelling spelling_impl_;
};

}
