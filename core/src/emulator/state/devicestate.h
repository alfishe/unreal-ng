#pragma once

#include <string>

#include "emulator/state/statenode.h"

class EmulatorContext;

/// @file devicestate.h
/// @brief Device state reports for analysis, built once in the core and
/// rendered by every automation interface (WebAPI, Python, Lua, CLI, MCP).
///
/// Each builder returns a StateNode object. When the device is not present
/// the object carries `available: false` and a `description` instead of
/// failing, so interfaces can always return something structured.
///
/// Reports:
/// - AY / SSG: `Ay()` overview of every AY chip (the TSFM's SSG halves
///   included), `AyChip(i)` full register decode of one chip. Shape matches
///   the historical WebAPI `state/audio/ay` responses.
/// - FM (TSFM, 2 x YM2203): `Fm()` board latches + per-chip summary,
///   `FmChip(i)` the full FM half: mode register, timers, busy, and every
///   channel/operator with its registers decoded, the live envelope state
///   and attenuation from ymfm, key-on mask, pitch in Hz, and the last DAC
///   word.
/// - FDC (Beta Disk WD1793): `Fdc()` registers, decoded status bits, last
///   command, FSM state, Beta128 system register, DRQ/INTRQ, and all four
///   drives (inserted image, track, side, motor, write protect, geometry).
namespace DeviceState
{
StateNode Ay(EmulatorContext* context);
StateNode AyChip(EmulatorContext* context, int chip);
StateNode Fm(EmulatorContext* context);
StateNode FmChip(EmulatorContext* context, int chip);
StateNode Fdc(EmulatorContext* context);

/// Human-readable rendering (CLI): "key: value" lines, nested by indentation,
/// arrays as "[index]" blocks
std::string ToText(const StateNode& node, int indent = 0);
}  // namespace DeviceState
