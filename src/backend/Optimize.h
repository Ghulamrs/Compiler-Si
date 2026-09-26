#pragma once

#include "Ins.h"

#include <vector>

namespace shalimar {

// **A straight-line run of x86 instructions, rewritten before it is spelled.** The emitter buffers
// what it builds and anything it cannot buffer - a label, a jump, a call - writes the buffer out
// first, so a run is by construction a stretch with no branch into or out of it.

// What it mends is the shape the emitter has to write: a value made in the accumulator and at once moved where it is wanted - `mov eax, 0 ; mov ecx, eax`, 648 of the pairs in the case corpus.
void optimizeRun(std::vector<Ins> &run);

} // namespace shalimar
