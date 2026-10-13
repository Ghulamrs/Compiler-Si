#pragma once

// **CodeView's type stream, `.debug$T`**: cpp11's CodeViewTypes brought across (review of
// 2026-10-12) and fed Shalimar's four types. A fundamental type and a pointer to one take
// CodeView's own numbers - T_INT4 0x74, T_REAL64 0x41, T_UCHAR 0x20, T_64PINT4 0x674 - which is
// what makes cdb print them as it prints cl's. An array is a pointer to an LF_STRUCTURE
// {count, element, data}, written as a forward reference, its field list, then itself - cl's
// order. Records are bytes, numbered from 0x1000 in the order written.

#include "../Type.h"

#include <map>
#include <string>
#include <vector>

namespace shalimar {

class CodeViewTypes {
public:
    // The index for a variable of Shalimar type `t`.
    unsigned index(const Type *t);
    // The same, for a by-reference parameter: an LF_POINTER in reference mode, so that cdb
    // prints the value referred to, as it does a C++ `int &`.
    unsigned reference(const Type *t);
    // LF_PROCEDURE for a function returning `returns` (null: void) and taking `params`.
    unsigned procedure(const Type *returns, const std::vector<unsigned> &params);

    // Every structure named so far, for S_UDT.
    const std::vector<std::pair<std::string, unsigned> > &named() const { return named_; }

    // `.debug$T`, whole; nothing when no record was needed.
    void write(std::string &out) const;

private:
    typedef std::vector<unsigned char> Bytes;

    unsigned record(int kind, const Bytes &body);
    static unsigned fundamental(const Type *t);
    unsigned pointer(unsigned to, int mode);
    unsigned structure(const Type *array);

    std::vector<Bytes> records_;
    std::map<const Type *, unsigned> done_;
    std::map<unsigned, unsigned> pointers_;
    std::map<std::string, unsigned> procedures_;
    std::vector<std::pair<std::string, unsigned> > named_;
};

}
