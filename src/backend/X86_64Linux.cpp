#include "X86_64Linux.h"

#include "Dwarf.h"

namespace shalimar {

void X86_64LinuxEmitter::beginModule(const std::string &sourceName) {
    raw("# " + sourceName);
    raw("\t.text");
    // -g: the files by unit, numbered from 1 as .loc wants them; the line program is the assembler's.
    for (size_t i = 0; i < files_.size(); ++i)
        raw("\t.file\t" + std::to_string(i + 1) + " \"" + files_[i] + "\"");
}

void X86_64LinuxEmitter::sourceLine(int unit, int line) {
    if (!debugInfo()) return;
    raw("\t.loc\t" + std::to_string(unit + 1) + " " + std::to_string(line) + " 0");
}

// The function's first address and its own line go before the prologue, which is inserted at the
// mark when the frame is known; .cfi_startproc opens the unwind data the prologue then describes.
void X86_64LinuxEmitter::describeFunction(const DebugFunction &function) {
    if (!debugInfo()) return;
    DebugFunction d = function;
    d.begin = ".Lfunc.begin." + d.symbol;
    d.end = ".Lfunc.end." + d.symbol;
    raw(d.begin + ":");
    if (d.line > 0) raw("\t.loc\t" + std::to_string(d.unit + 1) + " " + std::to_string(d.line) + " 0");
    raw("\t.cfi_startproc");
    prologueMark_ = text_.size();
    debugFunctions_.push_back(d);
    describing_ = true;
}

void X86_64LinuxEmitter::openConstSection() {
    raw("\t.section\t.rodata");
}

void X86_64LinuxEmitter::closeConstSection() {
    raw("\t.text");
}

void X86_64LinuxEmitter::emitGlobalBlock(int slots) {
    blank();
    raw("\t.bss");
    raw("\t.globl\t" + globalsLabel());
    raw("\t.align\t8");
    raw(globalsLabel() + ":");
    raw("\t.zero\t" + std::to_string(slots * 8));
    raw("\t.text");
}

void X86_64LinuxEmitter::endModule() {
    if (!data_.empty()) {
        blank();
        openConstSection();
        text_ += data_;
        closeConstSection();
    }
    if (debugInfo()) {
        blank();
        writeDwarf(text_, kElfDwarf, files_.empty() ? std::string() : files_[0], compDir_,
                   debugFunctions_, debugGlobals_, globalsLabel());
    }

    blank();
    raw("\t.section\t.note.GNU-stack,\"\",@progbits");
}

void X86_64LinuxEmitter::beginFunction(const std::string &name) {
    const std::string s = symbol(name);
    blank();
    raw("\t.globl\t" + s);
    raw("\t.type\t" + s + ", @function");
    raw(s + ":");
    prologueMark_ = text_.size();
}

void X86_64LinuxEmitter::endFunction(int slots) {
    emitEpilogue(slots);
    if (describing()) {
        // Slot k is at rsp + shadow + 8k, and rsp is rbp less the frame: the frame base is rbp.
        DebugFunction &d = debugFunctions_.back();
        d.slotBias = abi_.shadowBytes - frameBytesFor(slots);
        raw(d.end + ":");
        raw("\t.cfi_endproc");
    }
    text_.insert(prologueMark_, prologue(slots));
    describing_ = false;
}

void X86_64LinuxEmitter::label(int id) {
    raw(labelName(id) + ":");
}

}
