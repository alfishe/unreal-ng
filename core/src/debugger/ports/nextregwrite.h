#pragma once

// A tool's NextREG write for every automation surface (WebAPI POST /next/nextreg, MCP invoke_api, CLI `next nextreg`, Lua / Python
// next_nextreg_write): one implementation (docs/inprogress/2026-10-07-zx-next/design-automation-coverage.md R6).
//
// It goes through the board's single write choke point (NextBoard::Write), so the NextREG journal sees it, with the source of the
// chosen door:
//   nextreg   the NEXTREG n,v instruction: the pair goes to the board, no bus cycle, the select latch is untouched
//   port      OUT (#243B),n then OUT (#253B),v through the port decoder: the select latch moves, the port trace sees both
//   internal  the board itself (what a loader or a reset does), journal source "internal"
// It runs where nothing else drives the machine (Emulator::RunAtCoherentMoment: paused, never started, or between two frames),
// never on the HTTP thread while the Z80 runs. No breakpoint fires on it; TTD records it as a tool edit.

#include <cstdint>
#include <string>

#include "emulator/state/statenode.h"

class Emulator;

namespace NextRegWriteControl
{
enum class Door : uint8_t
{
    NextReg,
    Port,
    Internal,
};

struct Result
{
    bool ok = false;
    bool busy = false;     // no coherent moment within the timeout (another client is stepping the emulator)
    bool notNext = false;  // the machine is not a ZX Spectrum Next (WebAPI: 409)
    std::string moment;    // "paused", "stopped" or "frame" when ok
    std::string error;     // non-empty when not ok
    uint8_t reg = 0;
    uint8_t value = 0;     // as written
    uint8_t previous = 0;  // the stored byte before the write
    uint8_t after = 0;     // what a read of the register returns after it
    Door door = Door::NextReg;
};

/// Write `value` to NextREG `reg` through `door`, as a tool edit named `source` ("webapi", "lua", ...)
Result Write(Emulator* emulator, uint8_t reg, uint8_t value, Door door, const char* source);

/// "nextreg" (default when empty), "port", "internal"
bool ParseDoor(const std::string& text, Door& door);
const char* DoorName(Door door);

/// The register and the value as text (hex: "07", "0x07", "#07"); false with the reason
bool Parse(const std::string& regText, const std::string& valueText, uint8_t& reg, uint8_t& value, std::string& error);

/// The reply every plane renders: ok, reg, value, previous, after, door, moment
StateNode ToState(const Result& result);
}  // namespace NextRegWriteControl
