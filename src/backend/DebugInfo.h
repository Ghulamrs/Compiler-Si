#pragma once

// **What -g tells a debugger, in the compiler's own terms.** CodeGen fills these from
// Function::symbols_, the Prototype and the globals; an emitter adds the labels and the frame
// facts it alone knows; Dwarf.cpp and CodeView.cpp write them out. The four types are Type's.

#include "../Type.h"

#include <string>
#include <vector>

namespace shalimar {

struct DebugLocal {
    std::string name;
    const Type *type = nullptr;
    int slot = 0;            // the frame slot, eight bytes each, as every emitter addresses it
    bool isParam = false;
    bool indirect = false;   // a by-reference parameter: the slot holds the variable's address
    int scope = 0;           // the block it belongs to, 0 being the function's own
};

struct DebugBlock {
    int parent = 0;
    std::string begin;       // a label at the block's first instruction, and one after its last
    std::string end;
};

struct DebugFunction {
    std::string name;        // as the program spells it
    std::string symbol;      // as the linker sees it, without the platform's prefix
    std::string begin;       // labels at the first and after the last instruction
    std::string end;
    int unit = 0;
    int line = 0;
    const Type *returns = nullptr;   // null for a function with no result, or more than one
    std::vector<DebugLocal> locals;
    std::vector<DebugBlock> blocks;  // index 0 is the function's own scope and is never written

    // Filled in by the emitter once the frame is known: slot k sits at frame base + slotBias + 8*k.
    int slotBias = 0;

    // For CodeView: the labels after the prologue and after the last instruction, and the
    // displacement of slot 0 from RBP.
    std::string prologEnd;
    std::string codeEnd;
    int frameBias = 0;
};

struct DebugGlobal {
    std::string name;
    const Type *type = nullptr;
    int slot = 0;            // at shm_globals + 8*slot
};

}
