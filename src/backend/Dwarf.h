#pragma once

// **DWARF, as text for the assembler: cpp11's Dwarf.cpp brought across (review of 2026-10-12),
// fed from DebugInfo.h rather than from cpp11's Type and Local.** The line program is the
// assembler's, from the `.file` and `.loc` directives the emitter writes; this writes the
// abbreviations, the compile unit, a subprogram per function with its parameters, locals and
// lexical blocks, a variable per global, and the types - in `.byte`s, with every reference a
// label difference, so it asks nothing of an assembler but sections, bytes and labels.

#include "DebugInfo.h"

#include <string>
#include <vector>

namespace shalimar {

struct DwarfSpelling {
    const char *abbrev;      // the section directive for each of the three
    const char *info;
    const char *line;
    bool offsetsAreLabels;   // ELF: a section offset is a label; Mach-O: 0, the assembler relocates
    int frameBaseReg;        // DWARF's number for the frame register: rbp 6, x29 29, B15 31
    const char *symbolPrefix;
};

extern const DwarfSpelling kElfDwarf;
extern const DwarfSpelling kMachODwarf;

// Everything after the code: the file is DW_AT_name, compDir DW_AT_comp_dir, and a global is
// at `globalsSymbol + 8*slot` - prefix included where the spelling has one.
void writeDwarf(std::string &out, const DwarfSpelling &sp, const std::string &file,
                const std::string &compDir, const std::vector<DebugFunction> &fns,
                const std::vector<DebugGlobal> &globals, const std::string &globalsSymbol);

}
