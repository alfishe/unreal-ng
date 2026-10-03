#pragma once

/// @file isaaccess.h
/// @brief ISA cycles from the automation interfaces (WebAPI `POST control/isa`, MCP, CLI `isa`, Lua / Python
/// `isa_*`; Sprinter ISA tdd §10). Every interface calls Execute, so they agree on what an action does.
///
/// Actions (slot 1 or 2, address the 20-bit ISA address, value 0..255):
///   - `io_read`, `mem_read`: a real cycle (side effects as from the CPU: a data-port read advances a DMA);
///   - `io_write`, `mem_write`: a real cycle;
///   - `io_peek`, `mem_peek`: what the card shows, no side effect;
///   - `reset`: one RESET DRV pulse to both slots (#9FBD bit 7 set, then the latch back as it was);
///   - `latch`: write the #9FBD latch (value).
/// A cycle with an effect is a tool edit (Emulator::EditMemoryFromTool): while TTD records it parks the CPU
/// and leaves a debugger-edit marker, as a memory write from a tool does. The result is the slot report's
/// form: {action, slot, address, value}.

#include <cstdint>
#include <string>

#include "emulator/state/statenode.h"

class EmulatorContext;
class SprinterIsaBus;

namespace IsaAccess
{
/// The machine's ISA bus, or null with the reason
SprinterIsaBus* Find(EmulatorContext* context, std::string* reason = nullptr);

/// "#30A", "0x30A", "30Ah" or decimal; up to #FFFFF
bool ParseAddress(const std::string& text, uint32_t& address);

/// One action; false with `error` (no ISA slots, bad slot / address / value, unknown action)
bool Execute(EmulatorContext* context, const std::string& action, int slot, uint32_t address, int value,
             const char* source, StateNode& result, std::string& error);
}  // namespace IsaAccess
