#pragma once

// A debugger's port write for every automation surface (WebAPI POST /ports/out, MCP control_execution port_out,
// CLI out, Lua / Python port_out, the Qt debugger): one implementation
// (docs/inprogress/2026-10-04-debugger-additions/tdd.md §1).
//
// The machine's own decoder handles the write, so it has every side effect a CPU OUT has (paging, TS-Conf
// registers, AY, border, a card's port). Unlike a CPU OUT, no port or memory breakpoint fires on it, device waits
// are dropped (the CPU's clock stays where it was), and TTD records it as a tool edit. It runs where nothing else
// drives the machine: paused, never started, or between two frames of a running machine.

#include <cstdint>
#include <string>

class Emulator;

namespace PortWrite
{
struct Result
{
    bool ok = false;
    bool busy = false;   // no coherent moment within the timeout (another client is stepping the emulator)
    std::string moment;  // "paused", "stopped" or "frame" when ok
    std::string error;   // non-empty when not ok
};

/// Write `value` to `port` through the machine's decoder, as a tool edit named `source` ("webapi", "lua", ...)
Result Write(Emulator* emulator, uint16_t port, uint8_t value, const char* source);

/// The port and the value as text ("0x13AF", "#13AF", "13AFh", "5039"; the value up to 255); false with the reason
bool Parse(const std::string& portText, const std::string& valueText, uint16_t& port, uint8_t& value,
           std::string& error);
}  // namespace PortWrite
