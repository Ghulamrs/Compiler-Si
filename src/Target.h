
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace shalimar {

class Emitter;

class Target {
public:
    virtual ~Target() = default;

    static std::unique_ptr<Target> forName(const std::string &name);

    static std::string hostName();
    static std::vector<std::string> names();

    virtual std::string name() const = 0;
    virtual std::unique_ptr<Emitter> newEmitter() const = 0;
    // The emitter for -g, where a target has one that differs: x86_64-windows writes the GNU
    // spelling for clang's COFF assembler then, MASM carrying no line table. The rest answer newEmitter.
    virtual std::unique_ptr<Emitter> newDebugEmitter() const { return newEmitter(); }
    // And the extension of what that emitter writes.
    virtual std::string debugAssemblyExtension() const { return assemblyExtension(); }

    virtual std::string assemblyExtension() const { return ".s"; }

    bool isHost() const { return name() == hostName(); }
};

}
