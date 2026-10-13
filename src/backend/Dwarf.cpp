#include "Dwarf.h"

#include <map>

namespace shalimar {

const DwarfSpelling kElfDwarf = {
    "\t.section\t.debug_abbrev,\"\",@progbits\n",
    "\t.section\t.debug_info,\"\",@progbits\n",
    "\t.section\t.debug_line,\"\",@progbits\n",
    true,
    6,
    "",
};

const DwarfSpelling kMachODwarf = {
    "\t.section\t__DWARF,__debug_abbrev,regular,debug\n",
    "\t.section\t__DWARF,__debug_info,regular,debug\n",
    "\t.section\t__DWARF,__debug_line,regular,debug\n",
    false,
    29,
    "_",
};

namespace {

const int kTagStructure = 0x13, kTagMember = 0x0d, kTagPointer = 0x0f;
const int kTagCompileUnit = 0x11, kTagBase = 0x24, kTagFormalParameter = 0x05;
const int kTagSubprogram = 0x2e, kTagVariable = 0x34, kTagLexicalBlock = 0x0b;

const int kAtLocation = 0x02, kAtName = 0x03, kAtByteSize = 0x0b, kAtStmtList = 0x10;
const int kAtLowPc = 0x11, kAtHighPc = 0x12, kAtLanguage = 0x13, kAtCompDir = 0x1b;
const int kAtProducer = 0x25, kAtDataMemberLocation = 0x38, kAtDeclFile = 0x3a;
const int kAtDeclLine = 0x3b, kAtEncoding = 0x3e, kAtExternal = 0x3f;
const int kAtFrameBase = 0x40, kAtType = 0x49;

const int kFormAddr = 0x01, kFormData2 = 0x05, kFormData4 = 0x06, kFormString = 0x08;
const int kFormData1 = 0x0b, kFormFlag = 0x0c, kFormRef4 = 0x13, kFormSecOffset = 0x17;
const int kFormExprLoc = 0x18;

// DW_LANG_C99: Shalimar is not a language a debugger knows, and C's expression reader is what
// lets `p a->count` and `p a->data[2]` read an array through the structure described below.
const int kLangC99 = 0x0c;
const int kAteFloat = 0x04, kAteSigned = 0x05, kAteUnsignedChar = 0x08;

const int kOpAddr = 0x03, kOpDeref = 0x06, kOpPlusUconst = 0x23, kOpFbreg = 0x91;

void uleb(std::vector<unsigned char> &out, unsigned long long v) {
    do {
        unsigned char byte = static_cast<unsigned char>(v & 0x7f);
        v >>= 7;
        if (v != 0) byte |= 0x80;
        out.push_back(byte);
    } while (v != 0);
}

enum Abbrev {
    kAbCompileUnit = 1,
    kAbSubprogram, kAbSubprogramVoid,
    kAbFormalParameter, kAbVariable, kAbGlobal,
    kAbBase, kAbPointer, kAbStructure, kAbMember,
    kAbLexicalBlock
};

void line(std::string &o, const std::string &text) { o += text; o += '\n'; }

void num(std::string &o, const char *dir, long long v) {
    o += dir;
    o += ' ';
    o += std::to_string(v);
    o += '\n';
}

void str(std::string &o, const std::string &v) {
    o += "\t.asciz\t\"";
    o += v;
    o += "\"\n";
}

void sleb(std::vector<unsigned char> &out, long long v) {
    bool more = true;
    while (more) {
        unsigned char byte = static_cast<unsigned char>(v & 0x7f);
        v >>= 7;
        const bool signBit = (byte & 0x40) != 0;
        if ((v == 0 && !signBit) || (v == -1 && signBit)) more = false;
        else byte |= 0x80;
        out.push_back(byte);
    }
}

void attr(std::string &o, int at, int form) {
    o += "\t.byte\t" + std::to_string(at) + ", " + std::to_string(form) + "\n";
}

void abbrev(std::string &o, int code, int tag, bool children) {
    num(o, "\t.byte", code);
    num(o, "\t.byte", tag);
    num(o, "\t.byte", children ? 1 : 0);
}

void endAbbrev(std::string &o) { o += "\t.byte\t0, 0\n"; }

// **The four types, as a debugger can read them.** A scalar is a base type at its slot. An array
// variable holds a pointer to the runtime's Array - {int32 count; int32 element; void *data} -
// so it is described as exactly that: a pointer to a structure named by the Shalimar type,
// whose `data` points at the elements (or at the rows' pointers, for a rank above one). The
// count and the data are then what the debugger shows, honestly, and no extent is guessed.
class Types {
public:
    enum Kind { Base, Pointer, Structure };
    struct Node {
        Kind kind;
        std::string name;
        int size;
        int encoding;        // a base type's
        int to;              // a pointer's target
        int members[3];      // a structure's: count, element, data
    };

    // The node a variable or member of Shalimar type `t` has.
    int id(const Type *t) {
        if (t == nullptr) return 0;
        std::map<const Type *, int>::iterator it = ids_.find(t);
        if (it != ids_.end()) return it->second;
        int n;
        if (t->isArray()) n = pointerTo(structureOf(t));
        else n = base(t);
        ids_[t] = n;
        return n;
    }

    std::size_t size() const { return nodes_.size(); }
    const Node &at(std::size_t i) const { return nodes_[i]; }

private:
    int add(const Node &n) { nodes_.push_back(n); return static_cast<int>(nodes_.size()); }

    int base(const Type *t) {
        Node n;
        n.kind = Base;
        n.name = t->spelling();
        switch (t->kind()) {
        case Type::Kind::Int:  n.size = 4; n.encoding = kAteSigned; break;
        case Type::Kind::Real: n.size = 8; n.encoding = kAteFloat; break;
        default:               n.size = 1; n.encoding = kAteUnsignedChar; break;
        }
        n.to = 0;
        return add(n);
    }

    int pointerTo(int target) {
        std::map<int, int>::iterator it = pointers_.find(target);
        if (it != pointers_.end()) return it->second;
        Node n;
        n.kind = Pointer;
        n.size = 8;
        n.encoding = 0;
        n.to = target;
        return pointers_[target] = add(n);
    }

    int structureOf(const Type *array) {
        const int count = id(Type::intType());
        const int element = count;
        const int data = pointerTo(id(array->element()));
        Node n;
        n.kind = Structure;
        n.name = array->spelling();
        n.size = 16;
        n.encoding = 0;
        n.to = 0;
        n.members[0] = count;
        n.members[1] = element;
        n.members[2] = data;
        return add(n);
    }

    std::map<const Type *, int> ids_;
    std::map<int, int> pointers_;
    std::vector<Node> nodes_;
};

std::string label(int id) { return "Ldwarf.t" + std::to_string(id); }

void typeRef(std::string &o, int id) {
    line(o, "\t.long\t" + label(id) + " - Ldebug.cu.begin");
}

void exprLoc(std::string &o, const std::vector<unsigned char> &e) {
    num(o, "\t.byte", static_cast<long long>(e.size()));
    for (std::size_t i = 0; i < e.size(); i++) num(o, "\t.byte", e[i]);
}

void writeAbbrevTable(std::string &o, const DwarfSpelling &sp) {
    o += sp.abbrev;
    if (sp.offsetsAreLabels) line(o, "Ldebug.abbrev.begin:");

    abbrev(o, kAbCompileUnit, kTagCompileUnit, true);
    attr(o, kAtProducer, kFormString);
    attr(o, kAtLanguage, kFormData1);
    attr(o, kAtName, kFormString);
    attr(o, kAtCompDir, kFormString);
    attr(o, kAtLowPc, kFormAddr);
    attr(o, kAtHighPc, kFormAddr);
    attr(o, kAtStmtList, kFormSecOffset);
    endAbbrev(o);

    for (int i = 0; i < 2; i++) {
        abbrev(o, i == 0 ? kAbSubprogram : kAbSubprogramVoid, kTagSubprogram, true);
        attr(o, kAtName, kFormString);
        attr(o, kAtDeclFile, kFormData1);
        attr(o, kAtDeclLine, kFormData2);
        attr(o, kAtLowPc, kFormAddr);
        attr(o, kAtHighPc, kFormAddr);
        attr(o, kAtFrameBase, kFormExprLoc);
        attr(o, kAtExternal, kFormFlag);
        if (i == 0) attr(o, kAtType, kFormRef4);
        endAbbrev(o);
    }

    abbrev(o, kAbFormalParameter, kTagFormalParameter, false);
    attr(o, kAtName, kFormString);
    attr(o, kAtType, kFormRef4);
    attr(o, kAtLocation, kFormExprLoc);
    endAbbrev(o);

    abbrev(o, kAbVariable, kTagVariable, false);
    attr(o, kAtName, kFormString);
    attr(o, kAtType, kFormRef4);
    attr(o, kAtLocation, kFormExprLoc);
    endAbbrev(o);

    abbrev(o, kAbGlobal, kTagVariable, false);
    attr(o, kAtName, kFormString);
    attr(o, kAtType, kFormRef4);
    attr(o, kAtLocation, kFormExprLoc);
    attr(o, kAtExternal, kFormFlag);
    endAbbrev(o);

    abbrev(o, kAbBase, kTagBase, false);
    attr(o, kAtName, kFormString);
    attr(o, kAtEncoding, kFormData1);
    attr(o, kAtByteSize, kFormData1);
    endAbbrev(o);

    abbrev(o, kAbPointer, kTagPointer, false);
    attr(o, kAtByteSize, kFormData1);
    attr(o, kAtType, kFormRef4);
    endAbbrev(o);

    abbrev(o, kAbStructure, kTagStructure, true);
    attr(o, kAtName, kFormString);
    attr(o, kAtByteSize, kFormData4);
    endAbbrev(o);

    abbrev(o, kAbMember, kTagMember, false);
    attr(o, kAtName, kFormString);
    attr(o, kAtType, kFormRef4);
    attr(o, kAtDataMemberLocation, kFormData4);
    endAbbrev(o);

    abbrev(o, kAbLexicalBlock, kTagLexicalBlock, true);
    attr(o, kAtLowPc, kFormAddr);
    attr(o, kAtHighPc, kFormAddr);
    endAbbrev(o);

    o += "\t.byte\t0\n";
}

void writeType(std::string &o, const Types::Node &n, int id) {
    line(o, label(id) + ":");
    switch (n.kind) {
    case Types::Base:
        num(o, "\t.byte", kAbBase);
        str(o, n.name);
        num(o, "\t.byte", n.encoding);
        num(o, "\t.byte", n.size);
        break;
    case Types::Pointer:
        num(o, "\t.byte", kAbPointer);
        num(o, "\t.byte", n.size);
        typeRef(o, n.to);
        break;
    case Types::Structure: {
        num(o, "\t.byte", kAbStructure);
        str(o, n.name);
        num(o, "\t.long", n.size);
        static const char *const names[3] = {"count", "element", "data"};
        static const int offsets[3] = {0, 4, 8};
        for (int m = 0; m < 3; m++) {
            num(o, "\t.byte", kAbMember);
            str(o, names[m]);
            typeRef(o, n.members[m]);
            num(o, "\t.long", offsets[m]);
        }
        o += "\t.byte\t0\n";
        break;
    }
    }
}

void writeObject(std::string &o, const DebugLocal &l, int slotBias, Types &types) {
    num(o, "\t.byte", l.isParam ? kAbFormalParameter : kAbVariable);
    str(o, l.name);
    typeRef(o, types.id(l.type));
    std::vector<unsigned char> e;
    e.push_back(kOpFbreg);
    sleb(e, slotBias + 8LL * l.slot);
    if (l.indirect) e.push_back(kOpDeref);
    exprLoc(o, e);
}

bool declaresAnything(const DebugFunction &f, int scope) {
    for (std::size_t i = 0; i < f.locals.size(); i++)
        if (f.locals[i].scope == scope) return true;
    for (std::size_t b = 1; b < f.blocks.size(); b++)
        if (f.blocks[b].parent == scope && declaresAnything(f, static_cast<int>(b))) return true;
    return false;
}

void writeScope(std::string &o, const DebugFunction &f, int scope, Types &types,
                const DwarfSpelling &sp) {
    for (std::size_t i = 0; i < f.locals.size(); i++)
        if (f.locals[i].scope == scope) writeObject(o, f.locals[i], f.slotBias, types);

    for (std::size_t b = 1; b < f.blocks.size(); b++) {
        if (f.blocks[b].parent != scope) continue;
        const int id = static_cast<int>(b);
        if (!declaresAnything(f, id)) continue;
        if (f.blocks[b].begin.empty() || f.blocks[b].end.empty()) {
            writeScope(o, f, id, types, sp);
            continue;
        }
        num(o, "\t.byte", kAbLexicalBlock);
        line(o, "\t.quad\t" + f.blocks[b].begin);
        line(o, "\t.quad\t" + f.blocks[b].end);
        writeScope(o, f, id, types, sp);
        o += "\t.byte\t0\n";
    }
}

}

void writeDwarf(std::string &out, const DwarfSpelling &sp, const std::string &file,
                const std::string &compDir, const std::vector<DebugFunction> &fns,
                const std::vector<DebugGlobal> &globals, const std::string &globalsSymbol) {
    if (fns.empty()) return;

    writeAbbrevTable(out, sp);

    Types types;

    if (sp.offsetsAreLabels) {
        out += sp.line;
        line(out, "Ldebug.line.begin:");
    }

    out += sp.info;
    line(out, "Ldebug.cu.begin:");
    line(out, "\t.long\tLdebug.cu.end - Ldebug.cu.after.length");
    line(out, "Ldebug.cu.after.length:");
    out += "\t.short\t4\n";
    line(out, sp.offsetsAreLabels ? "\t.long\tLdebug.abbrev.begin" : "\t.long\t0");
    out += "\t.byte\t8\n";

    num(out, "\t.byte", kAbCompileUnit);
    str(out, "shalimar");
    num(out, "\t.byte", kLangC99);
    str(out, file);
    str(out, compDir);
    line(out, "\t.quad\t" + fns.front().begin);
    line(out, "\t.quad\t" + fns.back().end);
    line(out, sp.offsetsAreLabels ? "\t.long\tLdebug.line.begin" : "\t.long\t0");

    std::vector<unsigned char> frameBase;
    frameBase.push_back(static_cast<unsigned char>(0x70 + sp.frameBaseReg));
    sleb(frameBase, 0);

    for (std::size_t i = 0; i < fns.size(); i++) {
        const DebugFunction &f = fns[i];
        const int returns = types.id(f.returns);
        num(out, "\t.byte", returns != 0 ? kAbSubprogram : kAbSubprogramVoid);
        str(out, f.name);
        num(out, "\t.byte", f.unit + 1);
        num(out, "\t.short", f.line);
        line(out, "\t.quad\t" + f.begin);
        line(out, "\t.quad\t" + f.end);
        exprLoc(out, frameBase);
        num(out, "\t.byte", 1);
        if (returns != 0) typeRef(out, returns);
        writeScope(out, f, 0, types, sp);
        out += "\t.byte\t0\n";
    }

    for (std::size_t i = 0; i < globals.size(); i++) {
        num(out, "\t.byte", kAbGlobal);
        str(out, globals[i].name);
        typeRef(out, types.id(globals[i].type));
        // The block's own address, then the slot's offset as an operation: dsymutil keeps a
        // variable whose address it can relocate, and `_shm_globals + 8` it dropped.
        std::vector<unsigned char> offset;
        if (globals[i].slot > 0) {
            offset.push_back(kOpPlusUconst);
            uleb(offset, 8ULL * static_cast<unsigned long long>(globals[i].slot));
        }
        num(out, "\t.byte", 9 + static_cast<long long>(offset.size()));
        num(out, "\t.byte", kOpAddr);
        line(out, "\t.quad\t" + std::string(sp.symbolPrefix) + globalsSymbol);
        for (std::size_t k = 0; k < offset.size(); k++) num(out, "\t.byte", offset[k]);
        num(out, "\t.byte", 1);
    }

    for (std::size_t i = 0; i < types.size(); i++)
        writeType(out, types.at(i), static_cast<int>(i) + 1);

    out += "\t.byte\t0\n";
    line(out, "Ldebug.cu.end:");
}

}
