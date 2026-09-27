#pragma once

// z80cpu-dispatch.h - per-bus-mode entry points.
//
// The opcode units are compiled three times (opcodes-flat.cpp /
// opcodes-paged.cpp / opcodes-callback.cpp)
// so every memory access is specialized at compile time: the flat build inlines
// the attached 64K array, the callback build calls the host bus directly. Both
// variants expose Step(); the active one is selected by bus wiring and stored
// in Z80CPU::stepFn (one stable, perfectly predicted indirect tail-jump per
// instruction, zero per-access mode branches).

#include "z80cpu-internal.h"

namespace Z80Flat  // Z80CpuAttachMemory(memory != null)
{
int Step(Z80CPU* cpu);
}

namespace Z80Paged  // Z80CpuAttachPageTables(read, write)
{
int Step(Z80CPU* cpu);
}

namespace Z80Cb  // Z80CpuSetMemoryBus (or a detached/null bus)
{
int Step(Z80CPU* cpu);
}
