#pragma once

#include "Ins.h"

#include <vector>

namespace shalimar {

// **A straight-line run of x86 instructions, rewritten before it is spelled.**
// The emitter buffers what it builds and anything it cannot buffer - a label,
// a jump, a call - writes the buffer out first, so a run is by construction a
// stretch with no branch into or out of it.

// What it mends is the shape the emitter has to write: a value is made in the
// accumulator and then moved where it is wanted, because the emitter deals in
// one value at a time. `mov eax, 0 ; mov ecx, eax` is 648 of the pairs in the
// case corpus, and where the accumulator is written again before anything
// reads it, the first move can simply name its destination.
void optimizeRun(std::vector<Ins> &run);

} // namespace shalimar
