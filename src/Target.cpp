#include "Target.h"

#include "backend/Arm64Darwin.h"
#include "backend/X86_64Linux.h"
#include "backend/X86_64Windows.h"
#include "backend/Tms6747.h"

namespace shalimar {
namespace {

class Arm64DarwinTarget : public Target {
public:
    std::string name() const override { return "arm64-darwin"; }
    std::unique_ptr<Emitter> newEmitter() const override {
        return std::unique_ptr<Emitter>(new Arm64DarwinEmitter());
    }
};

class X86_64LinuxTarget : public Target {
public:
    std::string name() const override { return "x86_64-linux"; }
    std::unique_ptr<Emitter> newEmitter() const override {
        return std::unique_ptr<Emitter>(new X86_64LinuxEmitter());
    }
};

class X86_64WindowsTarget : public Target {
public:
    std::string name() const override { return "x86_64-windows"; }
    std::string assemblyExtension() const override { return ".asm"; }
    std::unique_ptr<Emitter> newEmitter() const override {
        return std::unique_ptr<Emitter>(new X86_64WindowsEmitter());
    }
};

// The fourth target: nothing here assembles it, so shci stops at the text
// (-S) and the VM6747 emulator runs it with the runtime compiled by cxx1i.
class Tms6747Target : public Target {
public:
    std::string name() const override { return "tms6747"; }
    std::unique_ptr<Emitter> newEmitter() const override {
        return std::unique_ptr<Emitter>(new Tms6747Emitter());
    }
};

}

std::vector<std::string> Target::names() {
    return {"arm64-darwin", "x86_64-linux", "x86_64-windows", "tms6747"};
}

std::string Target::hostName() {
#if defined(_WIN32)
    return "x86_64-windows";
#elif defined(__APPLE__)
    return "arm64-darwin";
#else
    return "x86_64-linux";
#endif
}

std::unique_ptr<Target> Target::forName(const std::string &name) {
    if (name == "arm64-darwin")   return std::unique_ptr<Target>(new Arm64DarwinTarget());
    if (name == "x86_64-linux")   return std::unique_ptr<Target>(new X86_64LinuxTarget());
    if (name == "x86_64-windows") return std::unique_ptr<Target>(new X86_64WindowsTarget());
    if (name == "tms6747")        return std::unique_ptr<Target>(new Tms6747Target());
    return nullptr;
}

}
