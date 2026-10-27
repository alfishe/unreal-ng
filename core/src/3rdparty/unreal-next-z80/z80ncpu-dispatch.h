#pragma once

// z80ncpu-dispatch.h - the step entry of the opcode units.
//
// unreal-z80 compiles its opcode units three times (flat, paged and callback
// bus) and selects one per CPU; this fork keeps the callback bus only
// (opcodes-callback.cpp), so there is one Step.

#include "z80ncpu-internal.h"

namespace Z80nCb  // the host callback bus (Z80nCpuSetMemoryBus, or a null bus)
{
int Step(Z80nCPU* cpu);
}
