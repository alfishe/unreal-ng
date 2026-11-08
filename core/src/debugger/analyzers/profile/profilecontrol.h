#pragma once

/// @file profilecontrol.h
/// @brief The `cpuprofile` verb (docs/emulator/analysis/cpu-profile.md) for every surface: the WebAPI (and MCP and
/// WebSocket calls through it), the CLI, Lua and Python turn their input into an operation plus options and show the
/// reply. Options, defaults, refusals, the answer's fields and its text live here, so the surfaces cannot drift apart
/// (the analysiscontrol.h pattern).
///
/// Operations:
///   start  [stack beam frames ring_kib stream reset]  switch the profile on (the CPU held for the switch); a first
///                          start, reset=true or other stack setting starts a fresh window, otherwise the window goes
///                          on. stack (true): routines found at run time with self / inclusive; beam (false): self
///                          per 1/64 of the frame; frames (true): the per-frame series ring of ring_kib (4096) KiB;
///                          stream: a JSONL file the series is appended to
///   stop                   switch it off; the counters stay readable until the next fresh start
///   reset                  zero everything: a fresh window from now (marks dropped)
///   mark   name [drop]     copy the counters as the baseline `name` (since=name reads the time after it); drop=true
///                          removes it. At most 8
///   status                 the totals (polled: no hold, no copy of the counters)
///   report [since top sort callers beam]  the routines by self (sort=self, the default), inclusive or calls, the top
///                          (24) rows with names (labels, else sub_XXXX, banked code with its page), entry kinds,
///                          callers and the attribution notes (JP entries, dispatches, abandoned frames)
///   frames [last top]      the per-frame series: the last (50) frames, the top (8) routines of each by self
///   export format [path since]  kit (the L1 subset kit/fig_budget.py reads), json (the full profile), entries (the
///                          routine entries found: JSON), sym (label = $ADDR lines), ann (an .ann seed:
///                          "$ADDR c sub_ADDR"); written to path, or answered in "content"
///
/// @code
///   profile::Reply reply = profile::Execute(emulator, "report", {{"top", "10"}});
///   std::cout << reply.text;
/// @endcode

#include <map>
#include <string>
#include <vector>

#include "emulator/state/statenode.h"

class Emulator;

namespace profile
{

using Options = std::map<std::string, std::string>;

enum class Error : uint8_t
{
    None,
    NotAvailable,  ///< no debug manager / analyzer on this instance (HTTP 503)
    BadRequest,    ///< unknown operation or option, a value that does not parse (HTTP 400)
    NotFound,      ///< no such mark (HTTP 404)
    Conflict,      ///< not possible now: no profile, settings while running (HTTP 409)
};

int ErrorHttpStatus(Error error);
const char* ErrorPhrase(Error error);

struct Reply
{
    Error error = Error::None;
    std::string message;
    std::string text;
    StateNode body = StateNode::Object();

    bool Ok() const { return error == Error::None; }
    StateNode ToValue() const;
    int HttpStatus() const { return ErrorHttpStatus(error); }
};

/// "start", "stop", "reset", "mark", "status", "report", "frames", "export"
const std::vector<std::string>& Operations();
/// The option names an operation takes (empty for an unknown one)
const std::vector<std::string>& OptionsFor(const std::string& op);

/// Check the operation and its options, then run it. start / stop / reset hold the CPU for the switch
Reply Execute(Emulator& emulator, const std::string& op, const Options& options = {});

}  // namespace profile
