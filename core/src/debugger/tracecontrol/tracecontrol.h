#pragma once

/// @file tracecontrol.h
/// @brief The profilers, traces and captures of a debugger driven by one verb set for every surface (the catalogue
/// actions prof.*, Lua and Python `trace()`): start, stop, toggle, pause, resume, clear and status of the opcode
/// profiler, memory tracking, the call trace, code coverage, the port trace, the General Sound port trace, the PC
/// history, the AY register log, the audio capture, and the three profilers at once ("all", as POST /profiler/start).
/// The WebAPI keeps its own routes per profiler (/profiler/opcode/start ...); they call the same core objects.
///
/// Starting switches on the features a profiler needs (debug mode, memory tracking, call trace, the opcode profiler,
/// the port trace), as the REST start routes do. Status reads published state only (session flags, atomics), so a
/// control surface may read it at its readout rate.
///
/// @code
///   tracecontrol::Reply reply = tracecontrol::Execute(emulator, "opcode", "start", {});
///   reply = tracecontrol::Execute(emulator, "audio", "start", {{"seconds", "2"}, {"source", "ay1"}});
/// @endcode

#include <map>
#include <optional>
#include <string>

#include "emulator/memory/memoryaccesstracker.h"
#include "emulator/state/statenode.h"

class Emulator;

namespace tracecontrol
{

using Options = std::map<std::string, std::string>;

struct Reply
{
    /// "" on success; else "Bad Request" (unknown name or op, a bad option), "Conflict" (not on this machine, cannot
    /// start now), as the routes answer
    std::string error;
    std::string message;  ///< one sentence a user can act on
    /// {name, state: capturing | paused | stopped | unavailable, ...what the start reports (audio: seconds, source)}
    StateNode body = StateNode::Object();

    bool Ok() const { return error.empty(); }
};

/// "opcode, memory, calltrace, coverage, porttrace, gsporttrace, pchist, aylog, audio, all"
const char* Names();
/// start (options: coverage keep, aylog capacity, audio seconds + source), stop, toggle (start when stopped, else
/// stop; option on: true / false), pause, resume, pause_toggle (option on), clear, status
Reply Execute(Emulator& emulator, const std::string& name, const std::string& op, const Options& options = {});
/// The session state (nullopt: no such profiler, or not on this machine). Published state only
std::optional<ProfilerSessionState> State(Emulator& emulator, const std::string& name);
/// The profiler can pause (opcode, memory, calltrace, porttrace, gsporttrace, all)
bool CanPause(const std::string& name);

}  // namespace tracecontrol
