#pragma once

/// @file steppingactions.h
/// @brief The actions of run / step / time travel (Stream Deck use cases D1, D2, D3, D5): the paced run
/// (execution::RunPacer: run.pace_*), the runs to a place (run.to_pixel, run.until_pc, run.to_cursor, run.step_line,
/// run.to_line), the time-travel units (ttd.step_*_scanline, ttd.bookmark_prev / _next, ttd.find_last*, the history's
/// start / end, journal build, clip export, the scrub unit of ttd.seek_by) and the debugger's cursors they read.
///
/// The cursors: the Qt client publishes the address under its disassembly cursor ("disasm") and its memory view's
/// cursor ("memory") per instance; run.to_cursor and ttd.find_last_write take them when the call names no address. A
/// headless instance has none: the call then needs its `addr` param.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "emulator/state/statenode.h"

namespace actions
{
class ActionRegistry;
class RegistrationHandle;
struct ActionCall;
struct ActionResult;

/// Register the actions of this file; the registry keeps the handles
std::vector<RegistrationHandle> RegisterSteppingActions(ActionRegistry& registry);

/// The debugger's cursors per instance (thread-safe; the GUI writes, the actions workers read)
class DebuggerCursors
{
public:
    static DebuggerCursors& Instance();
    /// `pane`: "disasm" or "memory"; nullopt clears it
    void Set(const std::string& instanceId, const std::string& pane, std::optional<uint16_t> address);
    std::optional<uint16_t> Get(const std::string& instanceId, const std::string& pane) const;
    void Drop(const std::string& instanceId);

private:
    struct Impl;
    Impl& impl() const;
};

/// region <The scrub unit of ttd.seek_by (D3.1)>

/// "frames" (the acceleration curve), "seconds" (its curve in seconds), "batch" (`count` frames a step, 10),
/// "instructions", "tstates", "scanlines" (`count` a step, 1 / 100 / 1), "bookmarks" (the previous / next one)
const std::vector<std::string>& ScrubUnits();
/// A unit's canonical name for a name or its singular ("frame", "instruction", "tstate", "scanline", "second",
/// "bookmark"); "" when it is none
std::string ScrubUnitName(const std::string& name);
/// The unit the instance's dial scrubs in (ttd.scrub_unit; "frames" until one is chosen) and its count a step
std::string ScrubUnitOf(const std::string& instanceId);
int64_t ScrubCountOf(const std::string& instanceId);
/// The default count a step of a unit moves
int64_t ScrubDefaultCount(const std::string& unit);
/// ttd.seek_by in a unit the curve does not cover (instructions, tstates, scanlines, bookmarks)
ActionResult SeekByUnit(const ActionCall& call, const std::string& unit, int64_t count);
/// run.step_by in a unit other than instructions (frames, scanlines, tstates): forward runs, back steps back
/// through the history
ActionResult StepByUnit(const ActionCall& call, const std::string& unit, int64_t count);

/// endregion </The scrub unit>

/// The `pace` of the run.state readout and topic: {preset, label, unit, count, interval_ms, effective_ms, running,
/// stop_reason} (no tick count: the value changes only when the pace does). Reads the pacer's state, never waits
StateNode PaceReadout(const std::string& instanceId);

/// The instance is gone: its debugger cursors and its scrub unit are forgotten
void DropSteppingState(const std::string& instanceId);

/// An address param: a number, or text in decimal, 0x.., #.., $.. or ..h; false when absent or not one
bool AddressParam(const StateNode& params, const char* name, uint16_t& address, std::string& error);

}  // namespace actions
