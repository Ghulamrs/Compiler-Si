#include "X86_64Coff.h"

#include "CodeView.h"

namespace shalimar {

void X86_64CoffEmitter::beginModule(const std::string &sourceName) {
    sourceName_ = sourceName;
    raw("# " + sourceName);
    raw("\t.text");
    // The files by unit, numbered from 1; absolute, since cdb matches a breakpoint's file by its path.
    for (size_t i = 0; i < files_.size(); ++i)
        raw("\t.cv_file\t" + std::to_string(i + 1) + " \"" + codeViewPath(compDir_, files_[i]) + "\"");
}

void X86_64CoffEmitter::endModule() {
    if (!data_.empty()) {
        blank();
        openConstSection();
        text_ += data_;
        closeConstSection();
    }
    if (debugInfo()) {
        blank();
        writeCodeView(text_, codeViewPath(compDir_, sourceName_ + ".obj"), debugFunctions_,
                      debugGlobals_, globalsLabel());
    }
    blank();
}

void X86_64CoffEmitter::openConstSection() { raw("\t.section\t.rdata,\"dr\""); }
void X86_64CoffEmitter::closeConstSection() { raw("\t.text"); }

void X86_64CoffEmitter::emitGlobalBlock(int slots) {
    blank();
    raw("\t.bss");
    raw("\t.globl\t" + globalsLabel());
    raw("\t.p2align\t3");
    raw(globalsLabel() + ":");
    raw("\t.zero\t" + std::to_string(slots * 8));
    raw("\t.text");
}

void X86_64CoffEmitter::beginFunction(const std::string &name) {
    const std::string s = symbol(name);
    blank();
    raw("\t.globl\t" + s);
    raw("\t.p2align\t4");
    raw(s + ":");
    prologueMark_ = text_.size();
}

// **A line entry names its function, and is entered once per run of the line** - as cl writes
// them: cdb takes the first entry at an address and refuses `bp` on a line entered twice.
void X86_64CoffEmitter::sourceLine(int unit, int line) {
    if (!describing()) return;
    if (unit == lastUnit_ && line == lastLine_) return;
    lastUnit_ = unit;
    lastLine_ = line;
    raw("\t.cv_loc\t" + std::to_string(functions_ - 1) + " " + std::to_string(unit + 1) + " " +
        std::to_string(line) + " 0");
}

// The function's number, its own line, and `.seh_proc`, all before the prologue the mark will take.
void X86_64CoffEmitter::describeFunction(const DebugFunction &function) {
    if (!debugInfo()) return;
    DebugFunction d = function;
    d.begin = d.symbol;
    d.end = "Lfunc_end_" + d.symbol;
    d.prologEnd = "Lfunc_prolog_" + d.symbol;
    d.codeEnd = d.end;
    raw("\t.cv_func_id\t" + std::to_string(functions_++));
    lastUnit_ = -1;
    lastLine_ = -1;
    describing_ = true;
    debugFunctions_.push_back(d);
    if (d.line > 0) sourceLine(d.unit, d.line);
    raw("\t.seh_proc\t" + d.symbol);
    prologueMark_ = text_.size();
}

std::string X86_64CoffEmitter::afterPush() const { return describing() ? "\t.seh_pushreg\t%rbp\n" : ""; }
std::string X86_64CoffEmitter::afterSetFrame() const { return describing() ? "\t.seh_setframe\t%rbp, 0\n" : ""; }
std::string X86_64CoffEmitter::afterAlloc(int bytes) const {
    return describing() ? "\t.seh_stackalloc\t" + std::to_string(bytes) + "\n" : "";
}
std::string X86_64CoffEmitter::endPrologue() const {
    if (!describing() || debugFunctions_.empty()) return "";
    return "\t.seh_endprologue\n" + debugFunctions_.back().prologEnd + ":\n";
}

// A label of the file's own, spelled so that a CodeView record may name it: `.L` is a temporary
// the assembler keeps out of every table, and a dot is not an identifier's.
std::string X86_64CoffEmitter::localLabel(const std::string &name) const {
    std::string out = "L";
    for (size_t i = 0; i < name.size(); ++i) out += name[i] == '.' ? '_' : name[i];
    return out;
}

void X86_64CoffEmitter::endFunction(int slots) {
    emitEpilogue(slots);
    if (describing()) {
        // Slot k is at rsp + shadow + 8k, and rsp is rbp less the frame: rbp was taken before the allocation.
        DebugFunction &d = debugFunctions_.back();
        d.slotBias = abi_.shadowBytes - frameBytesFor(slots);
        d.frameBias = frameBytesFor(slots);
        raw(d.end + ":");
        raw("\t.seh_endproc");
    }
    text_.insert(prologueMark_, prologue(slots));
    describing_ = false;
}

void X86_64CoffEmitter::label(int id) {
    raw(labelName(id) + ":");
}

}
