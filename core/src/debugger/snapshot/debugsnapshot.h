#pragma once

// The debugger snapshot (docs/inprogress/2026-10-04-debugger-snapshot/tdd.md §4): one coherent picture of the machine
// for a debugger front end, and the builders its parts share with the separate WebAPI endpoints, so GET /registers,
// GET /disasm and the snapshot cannot drift apart.

#include <cstddef>
#include <cstdint>

#include "emulator/state/statenode.h"

class EmulatorContext;
struct Z80State;

namespace DebugSnapshot
{
/// The main Z80's registers: the GET /registers object (main, alternate, index, special, interrupt, flags)
StateNode Registers(EmulatorContext* context);
/// The same object for a saved register set (the snapshot's prev_regs)
StateNode RegistersOf(const Z80State& state);

/// `count` (1..100) instructions from `address` in the CPU view: the GET /disasm object (address, count,
/// instructions[] with address, bytes, mnemonic, size, label, target, targetLabel, displacement, effectiveAddress,
/// effectiveAddressLabel). Stops at the 64K wrap. {available: false, description} without a disassembler
StateNode Disasm(EmulatorContext* context, uint16_t address, size_t count);
}  // namespace DebugSnapshot
