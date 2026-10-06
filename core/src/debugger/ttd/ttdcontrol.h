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
#include <memory>
#include <cstddef>
#include <string>
#include <vector>

#include "emulator/state/statenode.h"

class EmulatorContext;

namespace ttd
{
class TimeTravelManager;
class TimeTravelController;
struct TTDFileInfo;
struct TTDRecordedMachine;

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
    /// The instance's session: the engine's controller when the emulator runs
    /// one (EmulatorContext::pTimeTravelController), v1's manager otherwise
    explicit TTDControl(EmulatorContext* context);
    /// The verbs over @p controller (tests run the surface contract on both backends)
    TTDControl(EmulatorContext* context, TimeTravelController* controller);
    ~TTDControl();
    TTDControl(const TTDControl&) = delete;
    TTDControl& operator=(const TTDControl&) = delete;

    /// Check the verb and its options, then run it
    TTDReply Execute(const TTDRequest& request);

    static const std::vector<std::string>& Verbs();
    /// The option names @p verb takes; {"*"} = any (port-events forwards them to its parser)
    static const std::vector<std::string>& OptionsFor(const std::string& verb);

    /// The status body (also without time travel: the capability probe)
    static StateNode StatusBody(const TimeTravelManager* manager);
    static StateNode StatusBody(const TimeTravelController* controller);
    static StateNode StatusBody(std::nullptr_t);
    static StateNode FileInfoBody(const TTDFileInfo& info);
    static StateNode RecordedMachineBody(const TTDRecordedMachine& machine);
    /// The write journal's fields of a status body
    static void AddWriteJournal(StateNode& body, const TimeTravelManager& manager);
    static void AddWriteJournal(StateNode& body, const TimeTravelController& controller);

    class Backend;

private:
    std::unique_ptr<Backend> _backend;
};
}  // namespace ttd
