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
/// - FDC: `Fdc()` reports the machine's disk controller. Beta Disk WD1793:
///   registers, decoded status bits, last command, FSM state, Beta128 system
///   register, DRQ/INTRQ, and all four drives (inserted image, track, side,
///   motor, write protect, geometry). +3 uPD765A (`controller` says which):
///   phase, main status register, the command in hand with its C H R N,
///   result bytes, ST0-ST2 decoded, SPECIFY times, the four units' cylinder
///   and seek state, and drives A and B.
namespace DeviceState
{
StateNode Ay(EmulatorContext* context);
StateNode AyChip(EmulatorContext* context, int chip);
StateNode Fm(EmulatorContext* context);
StateNode FmChip(EmulatorContext* context, int chip);
StateNode Fdc(EmulatorContext* context);

/// Screen reports (Screen::DescribeScreenState):
/// - `Screen(verbose)`: model, video mode, resolution, border, shadow screen,
///   active screen and RAM pages, contention, flash phase; verbose adds each
///   screen's RAM page and Z80 mapping and the decoded #7FFD latch.
/// - `ScreenMode()`: the video mode's picture format (colour depth, bpp,
///   attribute cell, text grid, memory layout), displayed RAM pages and the
///   machine's video latches (#EFF7, #DFFD, #FF77).
/// - `ScreenFlash()`: FLASH phase and timing.
StateNode Screen(EmulatorContext* context, bool verbose);
StateNode ScreenMode(EmulatorContext* context);
StateNode ScreenFlash(EmulatorContext* context);

/// Video memory contention (`Contention()`): the machine's rule (none / ula48 / ula128 / gatearray), whether
/// it applies, the 'contention' switch and whether contention is in effect, the selected memory interface,
/// the I/O rule, per slot its mapping and whether the CPU waits there, the +2A/+3 floating-bus latch, and -
/// while the debugger is on - contended accesses and wait T-states per kind (fetch / read / write / io) for
/// the current frame, the last frame and in total.
StateNode Contention(EmulatorContext* context);

/// Human-readable rendering (CLI): "key: value" lines, nested by indentation,
/// arrays as "[index]" blocks
std::string ToText(const StateNode& node, int indent = 0);
}  // namespace DeviceState
