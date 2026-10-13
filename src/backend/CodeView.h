#pragma once

// **CodeView, the debug information Microsoft's tools read**: cpp11's CodeView.cpp brought
// across (review of 2026-10-12) and fed from DebugInfo.h. Written as assembly text for clang's
// COFF assembler on x86_64-windows; link.exe /DEBUG makes a PDB of it, and cdb and RIDE read that.
// `.debug$S` holds the symbols and the line tables - S_OBJNAME and S_COMPILE3; per function
// S_GPROC32, S_FRAMEPROC, S_REGREL32 off RBP per parameter and local, S_BLOCK32 per block, S_END,
// then `.cv_linetable`; S_GDATA32 per global - and `.debug$T` the types. The line entries are the
// emitter's own `.cv_file`, `.cv_func_id` and `.cv_loc`.

#include "DebugInfo.h"

#include <string>
#include <vector>

namespace shalimar {

// A global is at `globalsSymbol + 8*slot`.
void writeCodeView(std::string &out, const std::string &objectName,
                   const std::vector<DebugFunction> &fns,
                   const std::vector<DebugGlobal> &globals, const std::string &globalsSymbol);

// A source path as `.cv_file` wants it: absolute, so cdb matches a breakpoint by it, backslashes doubled.
std::string codeViewPath(const std::string &compDir, const std::string &name);

}
