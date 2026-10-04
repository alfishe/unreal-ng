#pragma once

/// @file ttdcontrol.h
/// @brief The one place that implements the time-travel verbs for every surface
/// (Phase 5, Step 1, layer 3: docs/inprogress/2026-09-25-ttd-v2-migration/
/// phase-5-switchover-tdd.md §4.2.4). WebAPI (and MCP through it), CLI, Lua and
/// Python only turn their input into a TTDRequest and the TTDReply into their
/// output: guards, option parsing, defaults, refusals and their texts, and the
/// reply's fields live here, so the surfaces cannot drift apart.
///
/// Same pattern as MediaControl: a verb, options by name as strings (a flag given
/// without a value is "" and means true), a reply with an error code and a
/// StateNode body every surface already converts (JSON, sol::table, py::dict).
///
/// @code
///   TTDReply reply = TTDControl(context).Execute({"history-limit", {{"frames", "3000"}}});
///   http.status = reply.HttpStatus();  http.body = StateNodeToJson(reply.ToValue());
/// @endcode

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "emulator/state/statenode.h"

class EmulatorContext;

namespace ttd
{
class TimeTravelManager;

enum class TTDControlError : uint8_t
{
    None,
    NotAvailable,  ///< time travel is not built in / not constructed for this instance (HTTP 501)
    BadRequest,    ///< unknown verb or option, a value that does not parse (HTTP 400)
    NotFound,      ///< a named thing does not exist: a bookmark label (HTTP 404)
    Conflict,      ///< refused in the session's current state (HTTP 409)
    Internal       ///< should not happen (HTTP 500)
};

int TTDControlErrorHttpStatus(TTDControlError error);
/// The HTTP reason phrase the WebAPI has always put in "error": "Conflict", "Bad Request", ...
const char* TTDControlErrorPhrase(TTDControlError error);

struct TTDRequest
{
    std::string verb;
    std::map<std::string, std::string> options;
};

struct TTDReply
{
    TTDControlError error = TTDControlError::None;
    std::string message;  ///< one sentence a user can act on; every surface shows it verbatim
    StateNode body = StateNode::Object();
    bool created = false;  ///< success that made a new thing (a bookmark): HTTP 201

    bool Ok() const { return error == TTDControlError::None; }
    /// Success: the body. Failure: {"error": <phrase>, "message": ...} then the body's
    /// fields (a body that names its own "error" keeps it)
    StateNode ToValue() const;
    int HttpStatus() const { return Ok() && created ? 201 : TTDControlErrorHttpStatus(error); }
};

class TTDControl
{
public:
    explicit TTDControl(EmulatorContext* context);

    TTDReply Execute(const TTDRequest& request);

    static const std::vector<std::string>& Verbs();
    /// The options a verb accepts (help texts, tests). "*": any name, the verb checks them
    /// itself (port-events: the port query's options)
    static const std::vector<std::string>& OptionsFor(const std::string& verb);

    /// Status fields, also the idle answer when time travel is not constructed
    static StateNode StatusBody(const TimeTravelManager* manager);
    /// The write journal's state (D40): on/off, the spans it covers, a build in progress
    static void AddWriteJournal(StateNode& body, const TimeTravelManager& manager);

private:
    TTDReply Run(const std::string& verb, const TTDRequest& request);
    TTDReply Status();
    TTDReply Start(const TTDRequest& request);
    TTDReply Stop();
    TTDReply Invalidate(const TTDRequest& request);
    TTDReply HistoryLimit(const TTDRequest& request);
    TTDReply Journal(const TTDRequest& request);
    TTDReply JournalBuild(const TTDRequest& request);
    TTDReply JournalBuildCancel();
    TTDReply Position();
    TTDReply Seek(const TTDRequest& request);
    TTDReply StepFrame(bool forward);
    TTDReply Resume(const TTDRequest& request);
    TTDReply StepInstruction(const TTDRequest& request);
    TTDReply ReverseStep(const TTDRequest& request);
    TTDReply Markers();
    TTDReply Bookmarks();
    TTDReply BookmarkAdd(const TTDRequest& request);
    TTDReply BookmarkDelete(const TTDRequest& request);
    TTDReply PortEvents(const TTDRequest& request);
    TTDReply FindLast(const TTDRequest& request);
    TTDReply ReverseContinue(const TTDRequest& request);

    /// Moving in the timeline is refused while recording: the restored state would
    /// overwrite the live machine and the next capture would break the timeline
    bool RefuseWhileRecording(TTDReply& reply) const;
    /// The machine's thread calls in (a breakpoint callback): it is the owner, never wait for it
    bool OnMachineThread() const;

    /// Pause the machine and wait until its thread parks (a TTD mutation must not race a frame)
    void PauseAndConfirm();
    /// Repaint every observer after the framebuffer was rebuilt while paused
    void NotifyFrameRefresh();

    EmulatorContext* _context = nullptr;
    TimeTravelManager* _manager = nullptr;
};
}  // namespace ttd
