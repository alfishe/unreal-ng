#pragma once

/// @file analysiscontrol.h
/// @brief The `analysis` verbs of the analysis layers (docs/emulator/analysis/analysis-layers.md) for every surface:
/// the WebAPI (and MCP through it), the CLI, Lua and Python turn their input into an action plus options and show the
/// reply. Options, defaults, refusals, the answer's fields and its text live here, so the surfaces cannot drift apart
/// (the SymbolControl / tracecontrol pattern).
///
/// Actions (phase P1: L0 access facts only):
///   start   [level mode]   switch on debug mode and memory tracking, set the tracker level (facts, the default, or
///                          counters) and the mode (z80, the default, or physical), and start a fresh memory session
///   stop                   stop the session; the facts stay readable until the next start
///   status                 session state, level, mode, what the facts hold (bytes, touched addresses and pages)
///   explain address [page] what L0 knows of one byte: access kinds, last reader and writer, frames, the values
///                          written, the counters and the symbol names. address: $80BC, #80BC, 0x80BC, 80BCh,
///                          decimal or a label; page (physical mode): absolute index, ram:N or rom:N (default: the
///                          page mapped at the address now)
///
/// @code
///   analysis::Reply reply = analysis::Execute(emulator, "explain", {{"address", "$80BC"}});
///   std::cout << reply.text;                      // the readable answer
///   http.body = StateNodeToJson(reply.ToValue()); // its fields, with "text"
/// @endcode

#include <map>
#include <string>
#include <vector>

#include "emulator/state/statenode.h"

class Emulator;

namespace analysis
{

using Options = std::map<std::string, std::string>;

enum class Error : uint8_t
{
    None,
    NotAvailable,  ///< no memory tracker on this machine (HTTP 503)
    BadRequest,    ///< unknown action or option, a value that does not parse (HTTP 400)
    NotFound,      ///< no such label (HTTP 404)
    Conflict,      ///< not possible now: no facts recorded (HTTP 409)
};

int ErrorHttpStatus(Error error);
const char* ErrorPhrase(Error error);

struct Reply
{
    Error error = Error::None;
    std::string message;  ///< the refusal, one sentence a user can act on (empty on success)
    std::string text;     ///< the readable answer (what the CLI prints and MCP's content[] carries)
    StateNode body = StateNode::Object();

    bool Ok() const { return error == Error::None; }
    /// Success: the body plus "text". Failure: {"error": <phrase>, "message": ...}
    StateNode ToValue() const;
    int HttpStatus() const { return ErrorHttpStatus(error); }
};

/// "start", "stop", "status", "explain"
const std::vector<std::string>& Actions();
/// The option names an action takes (empty for an unknown action)
const std::vector<std::string>& OptionsFor(const std::string& action);

/// Check the action and its options, then run it. start / stop hold the CPU for the switch
Reply Execute(Emulator& emulator, const std::string& action, const Options& options = {});

/// "$80BC", "#80BC", "0x80BC", "80BCh", "32956": false when it is none of these or above 0xFFFF
bool ParseAddress(const std::string& text, uint16_t& address);

}  // namespace analysis
