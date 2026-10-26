#pragma once

/// @file analysiscontrol.h
/// @brief The `analysis` verbs of the analysis layers (docs/emulator/analysis/analysis-layers.md) for every surface:
/// the WebAPI (and MCP through it), the CLI, Lua and Python turn their input into an action plus options and show the
/// reply. Options, defaults, refusals, the answer's fields and its text live here, so the surfaces cannot drift apart
/// (the SymbolControl / tracecontrol pattern).
///
/// Actions (L0 access facts, P1; L1 code behaviour, P2; L2 structure, P3):
///   start   [level mode record]  switch on debug mode and memory tracking, set the tracker level (facts, the
///                          default, or counters) and the mode (z80, the default, or physical), and start a fresh
///                          memory session; record=true also starts the TTD recording L1 is built from
///   stop                   stop the session (and the recording start began); the facts stay readable until the next
///                          start, the recording until the next one
///   status                 session state, level, mode, what the facts hold, the recording, the L1 job and result
///   build   [frames path wait]  the long operation: capture the recording (TraceBundle) and replay it on a muted
///                          analysis instance into L1 on a worker thread (analysisjobs.h). Answers at once with the
///                          job id; WebSocket topics analysis.progress / analysis.ready follow it. frames: replay at
///                          most that many; path: write the L1 JSON there; wait: block up to that many ms (<= 30000)
///   cancel                 stop the running build
///   explain address [page what]  a routine's fact sheet (<= 1 KB, <= 24 disassembly lines) when L1 has a routine
///                          running at the address (what=auto, the default, or routine), otherwise what L0 knows of
///                          one byte (what=byte): access kinds, last reader and writer, frames, the values written,
///                          the counters and the symbol names. address: $80BC, #80BC, 0x80BC, 80BCh, decimal or a
///                          label; page: absolute index, ram:N or rom:N (L0: physical mode only, default the page
///                          mapped at the address now; L1: picks the routine of that page)
///   query   [kind range limit]  L2 (P3): the claims of one region kind (all, the default; conflicts: the clashing
///                          pairs) overlapping range (hex start-end), at most limit (200) rows; the text is a compact
///                          table (range, kind, detector, confidence, status, label or title)
///   export  format [path ranges width]  the L2 draft (l2export.h): ctl, skool, fields, geometry, layout, symbols or
///                          map, of the Z80 ranges (hex, comma separated; default 4000-FFFF), DEFB lines of width
///                          bytes (8); written to path, or answered in "content" (and the text)
///   gallery [address kind scale path limit]  L2 (P4): the graphics regions (fonts, sprites, tiles, screens, level
///                          maps; kind filters them) with their geometry; with an address (or a label), the region
///                          starting there (else covering it) drawn as a PNG (l2gallery.h) zoomed scale times (2,
///                          1..8): written to path, or answered base64 in "png"
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

/// "start", "stop", "status", "build", "cancel", "explain", "query", "export", "gallery"
const std::vector<std::string>& Actions();
/// The option names an action takes (empty for an unknown action)
const std::vector<std::string>& OptionsFor(const std::string& action);

/// Check the action and its options, then run it. start / stop / build hold the CPU for the switch or the capture
Reply Execute(Emulator& emulator, const std::string& action, const Options& options = {});

/// "$80BC", "#80BC", "0x80BC", "80BCh", "32956": false when it is none of these or above 0xFFFF
bool ParseAddress(const std::string& text, uint16_t& address);

}  // namespace analysis
