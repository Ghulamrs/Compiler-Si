#include "CodeView.h"
#include "CodeViewTypes.h"

namespace shalimar {
namespace {

// Symbol record kinds (cvinfo.h) and the subsection that carries them.
const int kSObjName = 0x1101, kSCompile3 = 0x113c, kSFrameProc = 0x1012;
const int kSGProc32 = 0x1110, kSEnd = 0x0006, kSRegRel32 = 0x1111, kSBlock32 = 0x1103;
const int kSUdt = 0x1108, kSGData32 = 0x110d;
const int kDebugSSymbols = 0xf1, kCvSignatureC13 = 4, kMachineAmd64 = 0xd0, kCvAmd64Rbp = 334;
// S_COMPILE3's language: CV_CFL_C is 0, which is the expression reader `?? a->count` wants.
const int kLanguageC = 0;
// S_FRAMEPROC flags: locals and parameters both off RBP (encoded 2, bits 14-15 and 16-17).
const int kFrameFlags = (2 << 14) | (2 << 16);

void line(std::string &o, const std::string &text) { o += text; o += '\n'; }

void num(std::string &o, const char *dir, long long v) {
    o += dir;
    o += '\t';
    o += std::to_string(v);
    o += '\n';
}

// **One record, measured by its own labels.** A symbol record is its length (not counting the
// length field), its kind, its fields, and padding to four.
class Records {
public:
    Records(std::string &o, const char *prefix) : o_(o), prefix_(prefix) {}

    void begin(int kind) {
        const std::string n = std::to_string(++count_);
        end_ = prefix_ + "r" + n + ".e";
        const std::string b = prefix_ + "r" + n + ".b";
        line(o_, "\t.short\t" + end_ + "-" + b);
        line(o_, b + ":");
        num(o_, "\t.short", kind);
    }
    void end() {
        line(o_, "\t.p2align\t2");
        line(o_, end_ + ":");
    }

    void openSubsection() {
        const std::string n = std::to_string(++count_);
        subEnd_ = prefix_ + "s" + n + ".e";
        const std::string b = prefix_ + "s" + n + ".b";
        num(o_, "\t.long", kDebugSSymbols);
        line(o_, "\t.long\t" + subEnd_ + "-" + b);
        line(o_, b + ":");
    }
    void closeSubsection() {
        line(o_, subEnd_ + ":");
        line(o_, "\t.p2align\t2");
    }

    std::string &out() { return o_; }

private:
    std::string &o_;
    std::string prefix_;
    std::string end_;
    std::string subEnd_;
    int count_ = 0;
};

void name(std::string &o, const std::string &v) {
    o += "\t.asciz\t\"";
    for (std::size_t i = 0; i < v.size(); i++) {
        if (v[i] == '"' || v[i] == '\\') o += '\\';
        o += v[i];
    }
    o += "\"\n";
}

void writeCompileUnit(Records &r, const std::string &objectName) {
    std::string &o = r.out();
    r.openSubsection();
    r.begin(kSObjName);
    num(o, "\t.long", 0);
    name(o, objectName);
    r.end();

    r.begin(kSCompile3);
    num(o, "\t.long", kLanguageC);
    num(o, "\t.short", kMachineAmd64);
    for (int i = 0; i < 8; i++) num(o, "\t.short", i % 4 == 0 ? 1 : 0);
    name(o, "shalimar");
    r.end();
    r.closeSubsection();
}

unsigned typeOf(const DebugLocal &l, CodeViewTypes &types) {
    return l.indirect ? types.reference(l.type) : types.index(l.type);
}

// One object in a scope: a frame slot off RBP, at slotBias + 8*slot - negative, the frame
// pointer being taken before the allocation on this target.
void writeObject(Records &r, const DebugLocal &l, int slotBias, CodeViewTypes &types) {
    std::string &o = r.out();
    r.begin(kSRegRel32);
    num(o, "\t.long", slotBias + 8LL * l.slot);
    num(o, "\t.long", typeOf(l, types));
    num(o, "\t.short", kCvAmd64Rbp);
    name(o, l.name);
    r.end();
}

bool declaresAnything(const DebugFunction &f, int scope) {
    for (std::size_t i = 0; i < f.locals.size(); i++)
        if (f.locals[i].scope == scope) return true;
    for (std::size_t b = 1; b < f.blocks.size(); b++)
        if (f.blocks[b].parent == scope && declaresAnything(f, static_cast<int>(b))) return true;
    return false;
}

// **Parameters first and in order**: cdb counts off the procedure's arguments from the top.
void writeScope(Records &r, const DebugFunction &f, int scope, CodeViewTypes &types) {
    for (std::size_t i = 0; i < f.locals.size(); i++)
        if (f.locals[i].scope == scope && f.locals[i].isParam) writeObject(r, f.locals[i], f.slotBias, types);
    for (std::size_t i = 0; i < f.locals.size(); i++)
        if (f.locals[i].scope == scope && !f.locals[i].isParam) writeObject(r, f.locals[i], f.slotBias, types);
    std::string &o = r.out();
    for (std::size_t b = 1; b < f.blocks.size(); b++) {
        const DebugBlock &block = f.blocks[b];
        const int id = static_cast<int>(b);
        if (block.parent != scope || !declaresAnything(f, id)) continue;
        if (block.begin.empty() || block.end.empty()) {
            writeScope(r, f, id, types);
            continue;
        }
        r.begin(kSBlock32);
        line(o, "\t.long\t0, 0");
        line(o, "\t.long\t" + block.end + "-" + block.begin);
        line(o, "\t.secrel32\t" + block.begin);
        line(o, "\t.secidx\t" + block.begin);
        name(o, "");
        r.end();
        writeScope(r, f, id, types);
        r.begin(kSEnd);
        r.end();
    }
}

unsigned procedureType(const DebugFunction &f, CodeViewTypes &types) {
    std::vector<unsigned> params;
    for (std::size_t i = 0; i < f.locals.size(); i++)
        if (f.locals[i].isParam && f.locals[i].scope == 0) params.push_back(typeOf(f.locals[i], types));
    return types.procedure(f.returns, params);
}

void writeFunction(Records &r, const DebugFunction &f, int id, CodeViewTypes &types) {
    std::string &o = r.out();
    r.openSubsection();
    r.begin(kSGProc32);
    line(o, "\t.long\t0, 0, 0");
    line(o, "\t.long\t" + f.codeEnd + "-" + f.symbol);
    line(o, "\t.long\t" + f.prologEnd + "-" + f.symbol);
    line(o, "\t.long\t" + f.codeEnd + "-" + f.symbol);
    num(o, "\t.long", procedureType(f, types));
    line(o, "\t.secrel32\t" + f.symbol);
    line(o, "\t.secidx\t" + f.symbol);
    num(o, "\t.byte", 0);
    name(o, f.name);
    r.end();

    r.begin(kSFrameProc);
    num(o, "\t.long", f.frameBias);
    line(o, "\t.long\t0, 0, 0, 0");
    num(o, "\t.short", 0);
    num(o, "\t.long", kFrameFlags);
    r.end();

    writeScope(r, f, 0, types);

    r.begin(kSEnd);
    r.end();
    r.closeSubsection();
    line(o, "\t.cv_linetable\t" + std::to_string(id) + ", " + f.symbol + ", " + f.codeEnd);
}

// The globals, each at its slot in the one block, and an S_UDT for every structure named.
void writeGlobals(Records &r, const std::vector<DebugGlobal> &globals, CodeViewTypes &types,
                  const std::string &globalsSymbol) {
    // **No empty subsection**: link.exe then lost the file table after it, and no line had a file.
    if (globals.empty() && types.named().empty()) return;
    std::string &o = r.out();
    r.openSubsection();
    for (std::size_t i = 0; i < globals.size(); i++) {
        r.begin(kSGData32);
        num(o, "\t.long", types.index(globals[i].type));
        line(o, "\t.secrel32\t" + globalsSymbol + "+" + std::to_string(8 * globals[i].slot));
        line(o, "\t.secidx\t" + globalsSymbol);
        name(o, globals[i].name);
        r.end();
    }
    const std::vector<std::pair<std::string, unsigned> > &named = types.named();
    for (std::size_t i = 0; i < named.size(); i++) {
        r.begin(kSUdt);
        num(o, "\t.long", named[i].second);
        name(o, named[i].first);
        r.end();
    }
    r.closeSubsection();
}

}

std::string codeViewPath(const std::string &compDir, const std::string &name) {
    const bool absolute = (name.size() > 1 && name[1] == ':') ||
                          (!name.empty() && (name[0] == '/' || name[0] == '\\'));
    std::string full = absolute || compDir.empty() ? name : compDir + "\\" + name;
    std::string out;
    for (std::size_t i = 0; i < full.size(); i++) {
        char c = full[i];
        if (c == '/') c = '\\';
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    return out;
}

void writeCodeView(std::string &out, const std::string &objectName,
                   const std::vector<DebugFunction> &fns,
                   const std::vector<DebugGlobal> &globals, const std::string &globalsSymbol) {
    if (fns.empty()) return;
    CodeViewTypes types;
    Records r(out, ".Lcv.");
    line(out, "\t.section\t.debug$S,\"dr\"");
    line(out, "\t.p2align\t2");
    num(out, "\t.long", kCvSignatureC13);
    writeCompileUnit(r, objectName);
    for (std::size_t i = 0; i < fns.size(); i++) writeFunction(r, fns[i], static_cast<int>(i), types);
    // After every function, so the S_UDT list holds every structure they named.
    writeGlobals(r, globals, types, globalsSymbol);
    line(out, "\t.cv_filechecksums");
    line(out, "\t.cv_stringtable");
    types.write(out);
}

}
