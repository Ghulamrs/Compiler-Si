#include "CodeViewTypes.h"

namespace shalimar {
namespace {

// Leaf kinds (cvinfo.h).
const int kLfPointer = 0x1002, kLfProcedure = 0x1008, kLfArglist = 0x1201;
const int kLfFieldList = 0x1203, kLfStructure = 0x1505, kLfMember = 0x150d;

const unsigned kTVoid = 0x0003, kT64Pointer = 0x0600;
// LF_POINTER attributes: CV_PTR_64 in bits 0-4, the mode in 5-7, the size in 13-18.
const unsigned kPtr64 = 0x0c, kPtrSize8 = 8u << 13;
const int kModePointer = 0, kModeLValueRef = 1;
const int kPropForwardRef = 0x80;

typedef std::vector<unsigned char> Bytes;

void u8(Bytes &b, unsigned v) { b.push_back(static_cast<unsigned char>(v)); }
void u16(Bytes &b, unsigned v) { u8(b, v & 0xff); u8(b, (v >> 8) & 0xff); }
void u32(Bytes &b, unsigned v) { u16(b, v & 0xffff); u16(b, v >> 16); }
void text(Bytes &b, const std::string &s) {
    for (std::size_t i = 0; i < s.size(); i++) u8(b, static_cast<unsigned char>(s[i]));
    u8(b, 0);
}

// A numeric leaf: the value itself below 0x8000.
void numeric(Bytes &b, unsigned v) { u16(b, v); }

// To four, with LF_PAD bytes that say how many are left (0xf3, 0xf2, 0xf1).
void pad(Bytes &b) {
    while (b.size() % 4 != 0) u8(b, 0xf0 + static_cast<unsigned>(4 - b.size() % 4));
}

}

unsigned CodeViewTypes::record(int kind, const Bytes &body) {
    Bytes r;
    u16(r, 0);
    u16(r, static_cast<unsigned>(kind));
    r.insert(r.end(), body.begin(), body.end());
    pad(r);
    const unsigned length = static_cast<unsigned>(r.size() - 2);
    r[0] = static_cast<unsigned char>(length & 0xff);
    r[1] = static_cast<unsigned char>(length >> 8);
    records_.push_back(r);
    return 0x1000 + static_cast<unsigned>(records_.size()) - 1;
}

// cl's numbers: int is T_INT4, real T_REAL64, and char - unsigned here, one byte - T_UCHAR.
unsigned CodeViewTypes::fundamental(const Type *t) {
    switch (t->kind()) {
    case Type::Kind::Int:  return 0x74;
    case Type::Kind::Real: return 0x41;
    case Type::Kind::Char: return 0x20;
    default:               return 0;
    }
}

unsigned CodeViewTypes::pointer(unsigned to, int mode) {
    // A plain pointer to a fundamental type has a number of its own.
    if (mode == kModePointer && to < 0x1000) return kT64Pointer | to;
    const unsigned key = to | (static_cast<unsigned>(mode) << 24);
    std::map<unsigned, unsigned>::const_iterator seen = pointers_.find(key);
    if (seen != pointers_.end()) return seen->second;
    Bytes b;
    u32(b, to);
    u32(b, kPtr64 | (static_cast<unsigned>(mode) << 5) | kPtrSize8);
    return pointers_[key] = record(kLfPointer, b);
}

// **Forward reference, field list, then the structure**, named by the Shalimar type: {count, element, data}.
unsigned CodeViewTypes::structure(const Type *array) {
    const std::string name = array->spelling();
    const unsigned dataType = pointer(index(array->element()), kModePointer);
    Bytes fields;
    static const char *const names[3] = {"count", "element", "data"};
    const unsigned types[3] = {0x74, 0x74, dataType};
    const unsigned offsets[3] = {0, 4, 8};
    for (int m = 0; m < 3; m++) {
        u16(fields, kLfMember);
        u16(fields, 3);          // public
        u32(fields, types[m]);
        numeric(fields, offsets[m]);
        text(fields, names[m]);
        pad(fields);
    }
    const unsigned list = record(kLfFieldList, fields);
    // count, property, field list, derived-from, vshape, size, name.
    Bytes b;
    u16(b, 3);
    u16(b, 0);
    u32(b, list);
    u32(b, 0);
    u32(b, 0);
    numeric(b, 16);
    text(b, name);
    const unsigned full = record(kLfStructure, b);
    named_.push_back(std::make_pair(name, full));
    return full;
}

unsigned CodeViewTypes::index(const Type *t) {
    if (t == nullptr) return kTVoid;
    std::map<const Type *, unsigned>::const_iterator seen = done_.find(t);
    if (seen != done_.end()) return seen->second;
    unsigned n;
    if (t->isArray()) {
        // The variable holds a pointer to the runtime's Array; the forward reference lets a
        // row's `data`, a pointer to a pointer to the same shape, be numbered first.
        Bytes fwd;
        u16(fwd, 0);
        u16(fwd, kPropForwardRef);
        u32(fwd, 0);
        u32(fwd, 0);
        u32(fwd, 0);
        numeric(fwd, 0);
        text(fwd, t->spelling());
        const unsigned forward = record(kLfStructure, fwd);
        const unsigned pointerToForward = pointer(forward, kModePointer);
        done_[t] = pointerToForward;
        const unsigned full = structure(t);
        n = pointer(full, kModePointer);
        (void)kPropForwardRef;
    } else {
        n = fundamental(t);
    }
    done_[t] = n;
    return n;
}

unsigned CodeViewTypes::reference(const Type *t) {
    return pointer(index(t), kModeLValueRef);
}

unsigned CodeViewTypes::procedure(const Type *returns, const std::vector<unsigned> &args) {
    const unsigned ret = index(returns);
    std::string key = std::to_string(ret);
    for (std::size_t i = 0; i < args.size(); i++) key += "," + std::to_string(args[i]);
    std::map<std::string, unsigned>::const_iterator seen = procedures_.find(key);
    if (seen != procedures_.end()) return seen->second;

    Bytes list;
    u32(list, static_cast<unsigned>(args.size()));
    for (std::size_t i = 0; i < args.size(); i++) u32(list, args[i]);
    const unsigned arglist = record(kLfArglist, list);
    Bytes b;
    u32(b, ret);
    u8(b, 0);
    u8(b, 0);
    u16(b, static_cast<unsigned>(args.size()));
    u32(b, arglist);
    return procedures_[key] = record(kLfProcedure, b);
}

void CodeViewTypes::write(std::string &out) const {
    if (records_.empty()) return;
    out += "\t.section\t.debug$T,\"dr\"\n\t.p2align\t2\n\t.long\t4\n";
    for (std::size_t r = 0; r < records_.size(); r++) {
        const Bytes &bytes = records_[r];
        for (std::size_t i = 0; i < bytes.size(); i += 16) {
            out += "\t.byte\t";
            for (std::size_t k = i; k < bytes.size() && k < i + 16; k++) {
                if (k > i) out += ", ";
                out += std::to_string(bytes[k]);
            }
            out += '\n';
        }
    }
}

}
