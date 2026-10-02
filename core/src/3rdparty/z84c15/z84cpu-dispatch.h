#pragma once

// z84cpu-dispatch.h - the step entry of the opcode units.
//
// unreal-z80 compiles its opcode units three times (flat, paged and callback
// bus) and selects one per CPU; this fork keeps the callback bus only
// (opcodes-callback.cpp), so there is one Step.

#include "z84cpu-internal.h"

namespace Z84Cb  // the host callback bus (Z84CpuSetMemoryBus, or a null bus)
{
int Step(Z84CPU* cpu);
}
