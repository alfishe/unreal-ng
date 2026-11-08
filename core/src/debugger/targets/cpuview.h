#pragma once

/// @file cpuview.h
/// @brief A sound card's CPU as every automation surface reaches it (gs-debugger design §7.1): the `cpu` selector
/// (main, gs, ms) of the register, memory, disassembly and memory map commands, and what they read and write on the
/// card. The main CPU keeps its own paths (DebugSnapshot, Memory, Emulator::SetRegisterFromTool); a surface asks
/// here once the selector names a card.
///
/// The answers have the main CPU's shapes (GET /registers, GET /disasm), so a client reads either CPU the same way.
/// Edits go through Emulator::EditMemoryFromTool: refused in the recorded past, recorded by a time travel session
/// (the card is a TTD device: its CPU state and its RAM pages are what the edit's record keeps).

#include <cstdint>
#include <string>
#include <vector>

#include "debugger/targets/debugcpuid.h"
#include "emulator/state/statenode.h"

class Emulator;
class EmulatorContext;
class GeneralSoundCard;

namespace CpuView
{
/// The CPU `name` selects ("" = main): "" on success, else why not ("'cpu': main, gs or ms"; "cpu gs not available:
/// no card with a CPU in its slot")
std::string Select(EmulatorContext* context, const std::string& name, DebugCpuId& cpu);

/// The card whose CPU `cpu` is, while it has one (null: main, or no such card now)
GeneralSoundCard* Card(EmulatorContext* context, DebugCpuId cpu);

/// The card CPU's registers in GET /registers' shape (main, alternate, index, special, interrupt, flags) plus "cpu";
/// special has no "t" (the card counts its own clock: "card_time"). Empty object without the card
StateNode Registers(EmulatorContext* context, DebugCpuId cpu);

/// The same registers flat, as the scripts' get_registers() has the main CPU's: pc, sp, af .. hl, af_ .. hl_, ix, iy,
/// i, r, memptr, q, im, iff1 / iff2 / halted (booleans), boundary, card_time, cpu. Empty object without the card
StateNode FlatRegisters(EmulatorContext* context, DebugCpuId cpu);

/// Set the card CPU's register `name` (the debugger's names, any case: a, af, af', ixh, im, iff1, ...) as a tool edit.
/// "" on success, else why not (an unknown register, a value out of its range, no card, the recorded past)
std::string SetRegister(Emulator* emulator, DebugCpuId cpu, const char* source, const std::string& name,
                        uint16_t value);
/// A register's value now by the same names (`wide`: a 16-bit one); false for an unknown one or without the card
bool GetRegister(EmulatorContext* context, DebugCpuId cpu, const std::string& name, uint16_t& value,
                 bool* wide = nullptr);

/// `length` bytes from `address` as the card CPU sees them now, wrapping at #FFFF; no side effects. False without
/// the card
bool Read(EmulatorContext* context, DebugCpuId cpu, uint16_t address, uint32_t length, std::vector<uint8_t>& out);
/// Write `bytes` from `address` into the card CPU's view as a tool edit, as the card CPU's writes land: a ROM or
/// flash window and a write-protected RAM page (NeoGS RAMRO) keep their bytes. "" on success, else why not;
/// `written` the bytes that landed
std::string Write(Emulator* emulator, DebugCpuId cpu, const char* source, uint16_t address,
                  const std::vector<uint8_t>& bytes, size_t* written = nullptr);

/// `count` (1..256) instructions from `address` of the card CPU: the GET /disasm object (address, count,
/// instructions[] with address, bytes, mnemonic, size, target) plus "cpu"; no labels (they are the main CPU's).
/// `before` / `snap` as DebugSnapshot::Disasm (the virtual window, no anchors but the bytes)
StateNode Disasm(EmulatorContext* context, DebugCpuId cpu, uint16_t address, size_t count, size_t before = 0,
                 bool snap = false);

/// The card CPU's four 16K windows: {cpu, windows: [{start, size, kind ("rom", "ram"), page, offset}]}; a window
/// with nothing mapped is "none"
StateNode Windows(EmulatorContext* context, DebugCpuId cpu);
}  // namespace CpuView
